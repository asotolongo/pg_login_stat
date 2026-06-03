-- Instalar la extensión en la base de datos de prueba
CREATE EXTENSION pg_login_stat;

-- Usuario de prueba para simular logins exitosos y fallidos
CREATE USER tester WITH PASSWORD 'secret123';
GRANT CONNECT ON DATABASE testdb TO tester;

-- Permitir que tester vea las estadísticas
GRANT EXECUTE ON FUNCTION pg_login_stat() TO tester;
GRANT SELECT ON pg_login_stats TO tester;
