/*-------------------------------------------------------------------------
 *
 * pg_login_stat.c
 *   Track PostgreSQL login statistics (successes and failures)
 *   per user and database using the ClientAuthentication_hook.
 *
 * Statistics are stored in a fixed-size shared memory hash table, keyed
 * by (username, database_name).  The extension must be loaded via
 * shared_preload_libraries.
 *
 * Configuration GUCs:
 *   pg_login_stat.enable  - enable/disable collection (default: on)
 *   pg_login_stat.max     - max tracked (user, db) pairs (default: 1000)
 *
 * SQL interface:
 *   pg_login_stat()       - returns login statistics as a set of rows
 *   pg_login_stat_reset() - clears all accumulated statistics
 *   pg_login_stats        - convenience view over pg_login_stat()
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <limits.h>

#include "access/htup_details.h"
#include "funcapi.h"
#include "libpq/auth.h"
#include "miscadmin.h"
#include "storage/ipc.h"
#include "storage/lwlock.h"
#include "storage/shmem.h"
#include "storage/spin.h"
#include "utils/builtins.h"
#include "utils/guc.h"
#include "utils/timestamp.h"

PG_MODULE_MAGIC;

#define PLS_MAX_DEFAULT		1000
#define PLS_TRANCHE_NAME	"pg_login_stat"

/*
 * Hash key: (username, database_name) as fixed-length strings.
 * We use names rather than OIDs because at authentication time the catalog
 * may not yet be accessible (authentication can fail before a session is
 * fully established).
 */
typedef struct PlsHashKey
{
	char		username[NAMEDATALEN];
	char		database[NAMEDATALEN];
} PlsHashKey;

/*
 * Per-entry login statistics.
 */
typedef struct PlsEntry
{
	PlsHashKey	key;			/* hash key — MUST BE FIRST */
	int64		login_ok;		/* successful login count */
	int64		login_fail;		/* failed login count */
	TimestampTz last_login_ok;	/* timestamp of last successful login */
	TimestampTz last_login_fail;	/* timestamp of last failed login */
	slock_t		mutex;			/* protects the counters above */
} PlsEntry;

/*
 * Shared-memory state header.
 */
typedef struct PlsSharedState
{
	LWLock	   *lock;			/* protects hash table search/modification */
	TimestampTz stats_reset;	/* timestamp of last reset */
} PlsSharedState;

/* Pointers into shared memory */
static PlsSharedState *pls = NULL;
static HTAB *pls_hash = NULL;

/* GUC variables */
static bool pls_enabled = true;
static int	pls_max = PLS_MAX_DEFAULT;

/* Saved hook values */
static ClientAuthentication_hook_type prev_client_auth_hook = NULL;
static shmem_request_hook_type prev_shmem_request_hook = NULL;
static shmem_startup_hook_type prev_shmem_startup_hook = NULL;

PG_FUNCTION_INFO_V1(pg_login_stat);
PG_FUNCTION_INFO_V1(pg_login_stat_reset);

void		_PG_init(void);

/* Forward declarations */
static void pls_client_auth(Port *port, int status);
static void pls_shmem_request(void);
static void pls_shmem_startup(void);
static Size pls_memsize(void);


/*
 * pls_memsize
 *   Estimate the amount of shared memory we need.
 */
static Size
pls_memsize(void)
{
	Size		size;

	size = MAXALIGN(sizeof(PlsSharedState));
	size = add_size(size, hash_estimate_size(pls_max, sizeof(PlsEntry)));
	return size;
}

/*
 * pls_shmem_request
 *   Request additional shared memory space from the postmaster.
 */
static void
pls_shmem_request(void)
{
	if (prev_shmem_request_hook)
		prev_shmem_request_hook();

	RequestAddinShmemSpace(pls_memsize());
	RequestNamedLWLockTranche(PLS_TRANCHE_NAME, 1);
}

/*
 * pls_shmem_startup
 *   Allocate or attach to the shared memory structures.
 */
static void
pls_shmem_startup(void)
{
	bool		found;
	HASHCTL		info;

	if (prev_shmem_startup_hook)
		prev_shmem_startup_hook();

	LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);

	pls = ShmemInitStruct("pg_login_stat",
						  sizeof(PlsSharedState),
						  &found);
	if (!found)
	{
		pls->lock = &(GetNamedLWLockTranche(PLS_TRANCHE_NAME))->lock;
		pls->stats_reset = GetCurrentTimestamp();
	}

	memset(&info, 0, sizeof(info));
	info.keysize = sizeof(PlsHashKey);
	info.entrysize = sizeof(PlsEntry);
	pls_hash = ShmemInitHash("pg_login_stat hash",
							 pls_max, pls_max,
							 &info,
							 HASH_ELEM | HASH_BLOBS);

	LWLockRelease(AddinShmemInitLock);
}

/*
 * pls_client_auth
 *   ClientAuthentication_hook implementation.  Called after each
 *   authentication attempt with the result status.
 */
static void
pls_client_auth(Port *port, int status)
{
	PlsHashKey	key;
	PlsEntry   *entry;
	bool		found;
	TimestampTz now;

	/* Always chain to previous hook first */
	if (prev_client_auth_hook)
		prev_client_auth_hook(port, status);

    /* verifying pls_enabled/pg_login_stat.enable is enable */ 
	if (!pls_enabled || pls == NULL || pls_hash == NULL)
		return;

	/* Ignore if username or database is missing (shouldn't normally happen) */
	if (port->user_name == NULL || port->database_name == NULL)
		return;

	/* Build hash key — zero padding so HASH_BLOBS works correctly */
	memset(&key, 0, sizeof(key));
	strlcpy(key.username, port->user_name, NAMEDATALEN);
	strlcpy(key.database, port->database_name, NAMEDATALEN);

	now = GetCurrentTimestamp();

	/*
	 * Acquire exclusive lock because we may need to insert a new entry.
	 * Connection rates are low enough that this is not a bottleneck.
	 */
	LWLockAcquire(pls->lock, LW_EXCLUSIVE);

	entry = (PlsEntry *) hash_search(pls_hash, &key, HASH_ENTER_NULL, &found);
	if (entry != NULL)
	{
		if (!found)
		{
			/* Initialize counters for a new entry */
			entry->login_ok = 0;
			entry->login_fail = 0;
			entry->last_login_ok = 0;
			entry->last_login_fail = 0;
			SpinLockInit(&entry->mutex);
		}

		SpinLockAcquire(&entry->mutex);
		if (status == STATUS_OK)
		{
			entry->login_ok++;
			entry->last_login_ok = now;
		}
		else
		{
			entry->login_fail++;
			entry->last_login_fail = now;
		}
		SpinLockRelease(&entry->mutex);
	}
	else
		elog(WARNING, "pg_login_stat: hash table full, cannot record login for user \"%s\" database \"%s\"",
			 port->user_name, port->database_name);

	LWLockRelease(pls->lock);
}

/*
 * _PG_init
 *   Module load callback.  Must be loaded via shared_preload_libraries.
 */
void
_PG_init(void)
{
	/*
	 * Shared memory hooks can only be registered during postmaster startup.
	 * Allow the extension to be created in the catalog without error even
	 * if not in shared_preload_libraries — the SQL functions will report
	 * a clear error when called.
	 */
	if (!process_shared_preload_libraries_in_progress)
		return;

	DefineCustomBoolVariable("pg_login_stat.enable",
							 "Enable pg_login_stat login statistics collection.",
							 NULL,
							 &pls_enabled,
							 true,
							 PGC_SIGHUP,
							 0,
							 NULL,
							 NULL,
							 NULL);

	/*
	 * pls_max must be PGC_POSTMASTER because it controls the shared memory
	 * allocation which is fixed at postmaster startup.
	 */
	DefineCustomIntVariable("pg_login_stat.max",
							"Maximum number of (user, database) pairs to track.",
							NULL,
							&pls_max,
							PLS_MAX_DEFAULT,
							100,
							INT_MAX / 2,
							PGC_POSTMASTER,
							0,
							NULL,
							NULL,
							NULL);

	MarkGUCPrefixReserved("pg_login_stat");

	/* Install hooks */
	prev_shmem_request_hook = shmem_request_hook;
	shmem_request_hook = pls_shmem_request;

	prev_shmem_startup_hook = shmem_startup_hook;
	shmem_startup_hook = pls_shmem_startup;

	prev_client_auth_hook = ClientAuthentication_hook;
	ClientAuthentication_hook = pls_client_auth;
}

/*
 * pg_login_stat
 *   Returns all accumulated login statistics as a set of rows.
 *
 *   Columns: username, datname, login_ok, login_fail,
 *            last_login_ok, last_login_fail
 */
Datum
pg_login_stat(PG_FUNCTION_ARGS)
{
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	HASH_SEQ_STATUS hash_seq;
	PlsEntry   *entry;

	if (pls == NULL || pls_hash == NULL)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("pg_login_stat is not loaded"),
				 errhint("Add pg_login_stat to shared_preload_libraries.")));

	/* Materialize the full result set into a tuplestore */
	InitMaterializedSRF(fcinfo, 0);

	LWLockAcquire(pls->lock, LW_SHARED);

	hash_seq_init(&hash_seq, pls_hash);
	while ((entry = hash_seq_search(&hash_seq)) != NULL)
	{
		Datum		values[6];
		bool		nulls[6] = {false};
		int64		login_ok,
					login_fail;
		TimestampTz last_ok,
					last_fail;

		/* Read counters under the spinlock to get a consistent snapshot */
		SpinLockAcquire(&entry->mutex);
		login_ok = entry->login_ok;
		login_fail = entry->login_fail;
		last_ok = entry->last_login_ok;
		last_fail = entry->last_login_fail;
		SpinLockRelease(&entry->mutex);

		values[0] = CStringGetTextDatum(entry->key.username);
		values[1] = CStringGetTextDatum(entry->key.database);
		values[2] = Int64GetDatum(login_ok);
		values[3] = Int64GetDatum(login_fail);

		/* Return NULL for timestamps when the corresponding count is zero */
		if (login_ok > 0)
			values[4] = TimestampTzGetDatum(last_ok);
		else
			nulls[4] = true;

		if (login_fail > 0)
			values[5] = TimestampTzGetDatum(last_fail);
		else
			nulls[5] = true;

		tuplestore_putvalues(rsinfo->setResult, rsinfo->setDesc,
							 values, nulls);
	}

	LWLockRelease(pls->lock);

	return (Datum) 0;
}

/*
 * pg_login_stat_reset
 *   Removes all entries from the hash table and records the reset timestamp.
 */
Datum
pg_login_stat_reset(PG_FUNCTION_ARGS)
{
	HASH_SEQ_STATUS hash_seq;
	PlsEntry   *entry;

	if (pls == NULL || pls_hash == NULL)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("pg_login_stat is not loaded"),
				 errhint("Add pg_login_stat to shared_preload_libraries.")));

	LWLockAcquire(pls->lock, LW_EXCLUSIVE);

	/*
	 * hash_seq_search guarantees it is safe to HASH_REMOVE the current entry
	 * during iteration.
	 */
	hash_seq_init(&hash_seq, pls_hash);
	while ((entry = hash_seq_search(&hash_seq)) != NULL)
		hash_search(pls_hash, &entry->key, HASH_REMOVE, NULL);

	pls->stats_reset = GetCurrentTimestamp();

	LWLockRelease(pls->lock);

	PG_RETURN_VOID();
}
