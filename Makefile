MODULE_big = pg_login_stat
OBJS = pg_login_stat.o

EXTENSION = pg_login_stat
DATA = pg_login_stat--1.0.sql
PGFILEDESC = "pg_login_stat - login statistics per user and database"


REGRESS_OPTS =--temp-config=./pg_login_stat.conf --temp-instance=./tmp_check
REGRESS = pg_login_stat

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
