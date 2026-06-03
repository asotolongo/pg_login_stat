# test_docker — Docker test environment for pg_login_stat

Contains the Docker files to start a PostgreSQL 17 instance with the
`pg_login_stat` extension already installed and ready to test.

## Files

| File | Description |
|---|---|
| `Dockerfile` | Image based on `postgres:17` that compiles and installs the extension |
| `docker-compose.yml` | Defines the service with `shared_preload_libraries=pg_login_stat` |
| `pg_hba.conf` | Requires password for all connections (to allow testing failed logins) |
| `init.sql` | Executed on startup: creates the extension and the `tester` user |

## Requirements

- Docker and Docker Compose installed
- Port 5432 free on the host

## Usage

All commands must be run **from this directory** (`test_docker/`).

### Start the environment

```bash
docker compose up -d
```

The first run builds the image (may take a few minutes while downloading dependencies and compiling the extension). Subsequent runs use the cache and start in seconds.

### Check it is ready

```bash
docker compose logs --tail 5
# Should end with: database system is ready to accept connections
```

### Stop the environment

```bash
docker compose down        # stops, keeps the data volume
docker compose down -v     # stops and deletes the volume (fresh start)
```

### Rebuild the image after changing the source code

After modifying `pg_login_stat.c` or any other extension file:

```bash
docker compose build
docker compose up -d
```

---

## Credentials

| User | Password | Notes |
|---|---|---|
| `postgres` | `postgres` | Superuser |
| `tester` | `secret123` | Unprivileged test user |

---

## Tests

### Connect

```bash
# Superuser
psql -h localhost -p 5432 -U postgres -d testdb

# Test user
psql -h localhost -p 5432 -U tester -d testdb
```

### View accumulated statistics

```sql
SELECT * FROM pg_login_stats;
```

```
 username | datname | login_ok | login_fail |        last_login_ok         |       last_login_fail
----------+---------+----------+------------+------------------------------+------------------------------
 postgres | testdb  |        2 |          0 | 2026-05-29 17:42:58.98174+00 |
 tester   | testdb  |        1 |          1 | 2026-05-29 17:38:43.852627+00| 2026-05-29 17:38:43.892826+00
```

### Generate a failed login

```bash
# Wrong password
PGPASSWORD=wrong psql -h localhost -p 5432 -U tester -d testdb
```

### Generate multiple logins

```bash
# 3 successful
for i in 1 2 3; do
  PGPASSWORD=postgres psql -h localhost -p 5432 -U postgres -d testdb -c "SELECT 1;" > /dev/null
done

# 2 failed
for i in 1 2; do
  PGPASSWORD=bad psql -h localhost -p 5432 -U tester -d testdb -c "SELECT 1;" 2>/dev/null || true
done

# View result
PGPASSWORD=postgres psql -h localhost -p 5432 -U postgres -d testdb -c "SELECT * FROM pg_login_stats;"
```

### Reset statistics

```sql
SELECT pg_login_stat_reset();
```

### Disable collection at runtime (no restart needed)

```sql
ALTER SYSTEM SET pg_login_stat.enable = off;
SELECT pg_reload_conf();

-- Logins from this point are not recorded

ALTER SYSTEM SET pg_login_stat.enable = on;
SELECT pg_reload_conf();
```

### Watch server logs

```bash
docker compose logs -f
```

Logs show each connection and disconnection thanks to `log_connections=on`
and `log_disconnections=on` configured in `docker-compose.yml`.
