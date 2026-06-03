-- Install extension pg_login_stat to track login statistics
CREATE EXTENSION pg_login_stat;
CREATE EXTENSION dblink;
show shared_preload_libraries;
\dx pg_login_stat


-- tester user to log in and generate statistics
CREATE USER tester WITH PASSWORD 'secret123' SUPERUSER;
GRANT CONNECT ON DATABASE postgres TO tester;

-- allow to user tester see the statistics
GRANT EXECUTE ON FUNCTION pg_login_stat() TO tester;
GRANT SELECT ON pg_login_stats TO tester;

-- allow to user tester see reset statistics
GRANT EXECUTE ON FUNCTION pg_login_stat_reset() TO tester;
--
--connect to the database with the tester user to generate  OK login statistics
--
\c "dbname=contrib_regression user=tester password=secret123   sslmode=disable"
select current_database();

--
--connect to the database with the tester user to generate  OK login statistics
--
\c "dbname=contrib_regression user=tester password=secret123 sslmode=disable"


--
--connect to the database with the test user to generate  FAIL login statistics using dblink
--
DO $$
BEGIN
    PERFORM dblink_connect(
        'dbname=contrib_regression user=tester password=WRONG'
    );
EXCEPTION WHEN OTHERS THEN
    RAISE NOTICE 'Login failed as expected: Message: %', SQLERRM;
END;
$$;

--
--get login statistics for the tester user
--
SELECT username, datname, login_ok, login_fail FROM pg_login_stats where username='tester';

--
--reset statistics
--
SELECT pg_login_stat_reset();

--
--get login statistics for the tester user after reset
--
SELECT username, datname, login_ok, login_fail FROM pg_login_stats where username='tester';

--
--disable the extension that captures login statistics
--
ALTER SYSTEM SET pg_login_stat.enable = off;
select pg_reload_conf();
SELECT pg_login_stat_reset();

--
--connect to the database with the tester user to check login statistics with extension disabled
--
\c "dbname=contrib_regression user=tester password=secret123 sslmode=disable"

--
--get login statistics for the tester user with the extension disabled
--
SELECT username, datname, login_ok, login_fail FROM pg_login_stats where username='tester';


--cleanup
DROP EXTENSION pg_login_stat;
DROP EXTENSION dblink;