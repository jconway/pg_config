/* $PostgreSQL$ */

-- Adjust this setting to control where the objects get dropped.
SET search_path = public;

DROP VIEW pg_config;
DROP FUNCTION pg_config();
DROP FUNCTION pg_config_reset();
