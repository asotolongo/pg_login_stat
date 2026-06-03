# pg_login_stat

PostgreSQL extension that tracks login statistics (successful and failed authentication attempts) per user and database. Statistics are kept in shared memory and survive for the lifetime of the PostgreSQL server instance (they are reset on server restart or when `pg_login_stat_reset()` is called).

## How it works

The extension installs a `ClientAuthentication_hook` that is invoked after every authentication attempt, following the example of core extension auth_delay. It uses a shared-memory hash table keyed by `(username, database_name)` to accumulate counters. 

## Requirements

- PostgreSQL 17 or later (Previous versions have not been tested)
- The extension **must** be listed in `shared_preload_libraries`

## Installation

```bash
cd pg_login_stat
make
make install
```

You must make sure you can see the binary `pg_config`,
maybe setting PostgreSQL binary path in the OS  or setting `PG_CONFIG=/path_to_pg_config/`  in the makefile or run:

```bash
make PG_CONFIG=/path_to_pg_config/
sudo make install PG_CONFIG=/path_to_pg_config/
```

Then add the extension to `postgresql.conf`:

```
shared_preload_libraries = 'pg_login_stat'
```

Restart PostgreSQL and create the extension in your database:

```sql
CREATE EXTENSION pg_login_stat;
```

## Configuration

| Parameter | Type | Default | Context | Description |
|---|---|---|---|---|
| `pg_login_stat.enable` | bool | `on` | SIGHUP | Enable or disable statistics collection. Can be changed without restart via `pg_reload_conf()`. |
| `pg_login_stat.max` | int | `1000` | Postmaster | Maximum number of `(user, database)` pairs to track. Requires server restart to change. |

Example in `postgresql.conf`:

```
pg_login_stat.enable = on
pg_login_stat.max = 200
```

## Usage

### View accumulated statistics

```sql
SELECT * FROM pg_login_stats;
```

| Column | Type | Description |
|---|---|---|
| `username` | text | PostgreSQL role name |
| `datname` | text | Database name |
| `login_ok` | bigint | Number of successful logins |
| `login_fail` | bigint | Number of failed login attempts |
| `last_login_ok` | timestamptz | Timestamp of last successful login (NULL if none) |
| `last_login_fail` | timestamptz | Timestamp of last failed login attempt (NULL if none) |

Or call the underlying function directly:

```sql
SELECT * FROM pg_login_stat();
```

### Find users with failed logins

```sql
SELECT username, datname, login_ok, login_fail
FROM pg_login_stats
WHERE login_fail > 0
ORDER BY login_fail DESC;
```

### Find users who have never successfully logged in

```sql
SELECT username, datname, login_fail
FROM pg_login_stats
WHERE login_ok = 0
ORDER BY login_fail DESC;
```

### Reset all statistics

```sql
SELECT pg_login_stat_reset();
```

### Enable or disable collection at runtime

```sql
-- Disable
ALTER SYSTEM SET pg_login_stat.enable = off;
SELECT pg_reload_conf();

-- Re-enable
ALTER SYSTEM SET pg_login_stat.enable = on;
SELECT pg_reload_conf();
```

## Permissions

- Superusers can always read the statistics and call the reset function.
- Only superusers can call `pg_login_stat_reset()`.

## Notes

- Statistics are lost on PostgreSQL server restart. They are kept purely in shared memory.
- If `pg_login_stat.max` pairs are already tracked and a new `(user, database)` combination appears, that event is silently dropped (a `WARNING` is logged). Increase `pg_login_stat.max` and restart if this happens.
- Failed logins include wrong passwords, `pg_hba.conf` rejections, and any other authentication error and some time can count double due to [The SSL mode behavior in authentication-hooked extensions](https://ongres.com/blog/ssl_mode_behavior_in_authentication_hooked_extensions/)


## Uninstall

```sql
DROP EXTENSION pg_login_stat;
```

Remove `pg_login_stat` from `shared_preload_libraries` in `postgresql.conf` and restart the server.


## Docker test

You can run and test the extension behavior in the `test_docker` folder

## AI help
I received some help from Claude Code for coding the extension


## License
Permission to use, copy, modify, and distribute this software and its documentation for any purpose, without fee, and without a written agreement is hereby granted, provided that the above copyright notice and this paragraph and the following two paragraphs appear in all copies. IN NO EVENT SHALL THE AUTHORS BE LIABLE TO ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF THE AUTHORS HAVE BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. THE AUTHORS SPECIFICALLY DISCLAIM ANY WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS" BASIS, AND THE AUTHORS HAVE NO OBLIGATIONS TO PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.

## Authors:
This extension is an open project. Feel free to join us and improve this module. To find out how you can get involved, please contact us or write us:

Anthony Sotolongo: asotolongo@gmail.com