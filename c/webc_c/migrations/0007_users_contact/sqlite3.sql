-- ===========================================================================
-- 0007_users_contact -- contact rules (Phase 13): phone is the required
-- handle (NOT NULL UNIQUE) and email becomes optional - it is only filled
-- when someone deliberately creates an account, guest checkout stores no
-- synthetic address any more.  sqlite cannot ALTER nullability, so the
-- 0003 swap pattern repeats: backfill any missing phone from the unique
-- username, build the new shape beside the old, copy every column, drop,
-- rename, then recreate the three users triggers.  Two extra schema
-- objects name `users` inside their own bodies - the org_units cascade
-- triggers and the users views.  DROP TABLE tolerates those dangling
-- references but the RENAME re-validates every trigger and view, so all
-- four are dropped first and recreated verbatim afterwards.  db.c runs
-- migrations with foreign_keys=OFF, so the table swap keeps every
-- REFERENCES users(id) clause valid at transaction end.
-- ===========================================================================

DROP TRIGGER IF EXISTS trg_soft_del_org_units;
DROP TRIGGER IF EXISTS trg_restore_org_units;
DROP VIEW IF EXISTS v_active_users;
DROP VIEW IF EXISTS v_pos_sales_delivery_report;

UPDATE users SET phone = username WHERE phone IS NULL;

CREATE TABLE users_pos (
    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),
    org_unit_id TEXT NULL REFERENCES org_units(id) ON DELETE RESTRICT,
    name TEXT NOT NULL,
    username TEXT NOT NULL UNIQUE,
    email TEXT NULL UNIQUE,
    phone TEXT NOT NULL UNIQUE,
    password_hash TEXT NOT NULL DEFAULT '',
    user_type_dict_id TEXT NOT NULL DEFAULT 'CUSTOMER' REFERENCES dictionaries(id) ON DELETE RESTRICT,
    customer_id TEXT NULL REFERENCES customers(id) ON DELETE RESTRICT,
    location_id TEXT NULL REFERENCES locations(id) ON DELETE RESTRICT,
    status TEXT NOT NULL DEFAULT 'ACTIVE' CHECK (status IN ('ACTIVE', 'INACTIVE', 'SUSPENDED')),
    profile_pic BLOB NULL, -- kept from the demo schema (superset of vision)
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    deleted_at TEXT DEFAULT NULL
);

INSERT INTO users_pos (id, org_unit_id, name, username, email, phone,
                       password_hash, user_type_dict_id, customer_id,
                       location_id, status, profile_pic,
                       created_at, updated_at, deleted_at)
SELECT id, org_unit_id, name, username, email, phone,
       password_hash, user_type_dict_id, customer_id,
       location_id, status, profile_pic,
       created_at, updated_at, deleted_at
FROM users;

DROP TABLE users;
ALTER TABLE users_pos RENAME TO users;

-- users triggers (dropped with the old table, verbatim from 0003)
CREATE TRIGGER IF NOT EXISTS trg_upd_users BEFORE UPDATE ON users BEGIN
    UPDATE users SET updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
    WHERE id = OLD.id;
END;

CREATE TRIGGER IF NOT EXISTS trg_soft_del_users
AFTER UPDATE OF deleted_at ON users
WHEN OLD.deleted_at IS NULL AND NEW.deleted_at IS NOT NULL
BEGIN
    UPDATE user_roles SET deleted_at = NEW.deleted_at WHERE user_id = OLD.id AND deleted_at IS NULL;
    UPDATE system_alerts SET deleted_at = NEW.deleted_at WHERE user_id = OLD.id AND deleted_at IS NULL;
    UPDATE customer_interactions SET deleted_at = NEW.deleted_at WHERE user_id = OLD.id AND deleted_at IS NULL;
END;

CREATE TRIGGER IF NOT EXISTS trg_restore_users
AFTER UPDATE OF deleted_at ON users
WHEN OLD.deleted_at IS NOT NULL AND NEW.deleted_at IS NULL
BEGIN
    UPDATE user_roles SET deleted_at = NULL WHERE user_id = OLD.id AND deleted_at = OLD.deleted_at;
    UPDATE system_alerts SET deleted_at = NULL WHERE user_id = OLD.id AND deleted_at = OLD.deleted_at;
    UPDATE customer_interactions SET deleted_at = NULL WHERE user_id = OLD.id AND deleted_at = OLD.deleted_at;
END;

-- org_units cascade triggers (verbatim from 0003, dropped above)
CREATE TRIGGER IF NOT EXISTS trg_soft_del_org_units
AFTER UPDATE OF deleted_at ON org_units
WHEN OLD.deleted_at IS NULL AND NEW.deleted_at IS NOT NULL
BEGIN
    UPDATE users SET deleted_at = NEW.deleted_at WHERE org_unit_id = OLD.id AND deleted_at IS NULL;
    UPDATE staff SET deleted_at = NEW.deleted_at WHERE org_unit_id = OLD.id AND deleted_at IS NULL;
    UPDATE inventory_stocks SET deleted_at = NEW.deleted_at WHERE org_unit_id = OLD.id AND deleted_at IS NULL;
END;

CREATE TRIGGER IF NOT EXISTS trg_restore_org_units
AFTER UPDATE OF deleted_at ON org_units
WHEN OLD.deleted_at IS NOT NULL AND NEW.deleted_at IS NULL
BEGIN
    UPDATE users SET deleted_at = NULL WHERE org_unit_id = OLD.id AND deleted_at = OLD.deleted_at;
    UPDATE staff SET deleted_at = NULL WHERE org_unit_id = OLD.id AND deleted_at = OLD.deleted_at;
    UPDATE inventory_stocks SET deleted_at = NULL WHERE org_unit_id = OLD.id AND deleted_at = OLD.deleted_at;
END;

-- users views (verbatim from 0003, dropped above)
CREATE VIEW IF NOT EXISTS v_active_users AS
SELECT * FROM users WHERE deleted_at IS NULL;

CREATE VIEW IF NOT EXISTS v_pos_sales_delivery_report AS
SELECT
    o.id AS order_id,
    o.order_number,
    o.created_at AS order_date,
    ou.ou_name AS branch_name,
    cu.name AS customer_name,
    st.first_name || ' ' || st.last_name AS cashier_name,
    o.subtotal,
    o.discount_amount,
    o.tax_amount,
    o.delivery_fee,
    o.total_amount,
    d.delivery_status,
    d.delivery_cost AS driver_payout_cost,
    (o.delivery_fee - IFNULL(d.delivery_cost, 0.0)) AS delivery_net_margin,
    IFNULL(p.paid, 0.0) AS total_paid
FROM orders o
JOIN org_units ou ON o.org_unit_id = ou.id
JOIN staff st ON o.staff_id = st.id
LEFT JOIN (SELECT customer_id, MIN(name) AS name FROM users
           WHERE customer_id IS NOT NULL AND deleted_at IS NULL
           GROUP BY customer_id) cu ON cu.customer_id = o.customer_id
LEFT JOIN deliveries d ON o.id = d.order_id AND d.deleted_at IS NULL
LEFT JOIN (SELECT order_id, SUM(amount) AS paid FROM payments
           WHERE payment_status = 'COMPLETED' AND deleted_at IS NULL
           GROUP BY order_id) p ON p.order_id = o.id
WHERE o.deleted_at IS NULL;
