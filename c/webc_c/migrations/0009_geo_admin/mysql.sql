-- ===========================================================================
-- 0009_geo_admin -- DDL only for MySQL, the seed rows stay sqlite-only
-- (precedent 0004_seed_pos, the geo pages are exercised by the sqlite
-- suites). The locations compatibility mirror and the legacy FK repoint
-- do not apply here - the MySQL test dialect starts with an empty
-- locations table and never writes users.location_id. Style follows the
-- 0002/0003 ports - VARCHAR ids and keyed string columns, inline
-- REFERENCES dropped (sqlite3 is the canonical FK dialect) - and the
-- code columns stay non-unique like the sqlite file because the export
-- carries a renumbered duplicate district.
-- ===========================================================================

CREATE TABLE IF NOT EXISTS provinces (
    id VARCHAR(36) PRIMARY KEY DEFAULT (UUID()),
    prov_id VARCHAR(32) NOT NULL UNIQUE,
    name_kh TEXT NOT NULL,
    name_en TEXT NOT NULL,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    deleted_at DATETIME NULL
);

CREATE TABLE IF NOT EXISTS districts (
    id VARCHAR(36) PRIMARY KEY DEFAULT (UUID()),
    dist_id VARCHAR(32) NOT NULL UNIQUE,
    province_id VARCHAR(36) NOT NULL,
    type TEXT NOT NULL,
    name_kh TEXT NOT NULL,
    name_en TEXT NOT NULL,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    deleted_at DATETIME NULL
);

CREATE TABLE IF NOT EXISTS communes (
    id VARCHAR(36) PRIMARY KEY DEFAULT (UUID()),
    comm_id VARCHAR(32) NOT NULL,
    district_id VARCHAR(36) NOT NULL,
    name_kh TEXT NOT NULL,
    name_en TEXT NOT NULL,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    deleted_at DATETIME NULL
);

CREATE TABLE IF NOT EXISTS villages (
    id VARCHAR(36) PRIMARY KEY DEFAULT (UUID()),
    vill_id VARCHAR(32) NOT NULL,
    commune_id VARCHAR(36) NOT NULL,
    name_kh TEXT NOT NULL,
    name_en TEXT NOT NULL,
    is_not_active TEXT NULL,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    deleted_at DATETIME NULL
);
