\echo Use "CREATE EXTENSION pg_login_stat" to load this file. \quit

-- Function that returns the accumulated login statistics from shared memory.
CREATE FUNCTION pg_login_stat(
    OUT username        text,
    OUT datname         text,
    OUT login_ok        bigint,
    OUT login_fail      bigint,
    OUT last_login_ok   timestamptz,
    OUT last_login_fail timestamptz
)
RETURNS SETOF record
AS 'MODULE_PATHNAME', 'pg_login_stat'
LANGUAGE C STRICT;

-- Function to reset (clear) all accumulated statistics.
CREATE FUNCTION pg_login_stat_reset()
RETURNS void
AS 'MODULE_PATHNAME', 'pg_login_stat_reset'
LANGUAGE C STRICT;

-- Convenience view over pg_login_stat().
CREATE VIEW pg_login_stats AS
    SELECT * FROM pg_login_stat();

-- Allow any user with pg_monitor to read statistics.
GRANT SELECT ON pg_login_stats TO pg_monitor;
GRANT EXECUTE ON FUNCTION pg_login_stat() TO pg_monitor;
