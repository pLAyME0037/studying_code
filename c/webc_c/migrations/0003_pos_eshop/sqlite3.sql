-- ============================================================================
-- SQLITE3 POS + E-SHOP SCHEMA (migration 0003_pos_eshop)
-- Generated from migrations/pos_and_eshop_db_schema.sql with deltas:
--   - PRAGMA lines stripped (db.c runs them at open, not in the txn)
--   - dictionary + language rows seeded (FK defaults reference literal
--     ids such as 'CUSTOMER' and 'TEIR_1')
--   - users swapped from the 0002 demo shape to the POS shape in place
--     (colliding demo usernames/emails de-duplicated during the copy)
--   - role_permissions/user_roles get a surrogate id PK (the engine
--     addresses rows by one id column) + UNIQUE pair kept
--   - v_pos_sales_delivery_report fixed (customers has no name column)
-- sqlite3 is the canonical dialect (FKs, triggers, views)
-- ============================================================================


-- ============================================================================
-- 1. SYSTEM CONFIG, I18N & MASTER DICTIONARIES
-- ============================================================================

CREATE TABLE IF NOT EXISTS system_configs (
    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),
    config_key TEXT NOT NULL UNIQUE,
    config_value TEXT,
    json_payload TEXT,
    is_encrypted INTEGER NOT NULL DEFAULT 0 CHECK (is_encrypted IN (0, 1)),
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    deleted_at TEXT DEFAULT NULL
);

CREATE TABLE IF NOT EXISTS dictionaries (
    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),
    category TEXT NOT NULL, -- PRODUCT_TYPE, CUSTOMER_TYPE, STAFF_TYPE, ORDER_STATUS, PAYMENT_METHOD
    code TEXT NOT NULL,
    label TEXT NOT NULL,
    sort_order INTEGER NOT NULL DEFAULT 0,
    metadata TEXT DEFAULT NULL, -- JSON
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    deleted_at TEXT DEFAULT NULL,
    UNIQUE(category, code)
);

CREATE TABLE IF NOT EXISTS languages (
    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),
    code TEXT NOT NULL UNIQUE, -- en, km, zh
    name TEXT NOT NULL,
    is_default INTEGER NOT NULL DEFAULT 0 CHECK (is_default IN (0, 1)),
    is_active INTEGER NOT NULL DEFAULT 1 CHECK (is_active IN (0, 1)),
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    deleted_at TEXT DEFAULT NULL
);

CREATE TABLE IF NOT EXISTS translations (
    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),
    language_id TEXT NOT NULL REFERENCES languages(id) ON DELETE RESTRICT,
    trans_key TEXT NOT NULL,
    trans_value TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    deleted_at TEXT DEFAULT NULL,
    UNIQUE(language_id, trans_key)
);

-- ============================================================================
-- 2. Location
-- ============================================================================

CREATE TABLE IF NOT EXISTS locations (
    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),
    province TEXT NOT NULL,
    district TEXT NOT NULL,
    commune TEXT NOT NULL,
    village TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    deleted_at TEXT DEFAULT NULL
);

-- ============================================================================
-- 3. ORGANIZATIONAL UNITS & RBAC (USERS, STAFF, ROLES, PERMISSIONS)
-- ============================================================================

CREATE TABLE IF NOT EXISTS org_units (
    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),
    parent_id TEXT REFERENCES org_units(id) ON DELETE RESTRICT,
    ou_code TEXT NOT NULL UNIQUE,
    ou_name TEXT NOT NULL,
    ou_type_dict_id TEXT REFERENCES dictionaries(id) ON DELETE RESTRICT,
    metadata TEXT DEFAULT NULL, -- JSON
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    deleted_at TEXT DEFAULT NULL
);

CREATE TABLE IF NOT EXISTS users (
    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),
    org_unit_id TEXT NULL REFERENCES org_units(id) ON DELETE RESTRICT,
    name TEXT NOT NULL,
    username TEXT NOT NULL UNIQUE,
    email TEXT NOT NULL UNIQUE,
    phone TEXT NOT NULL UNIQUE,
    password_hash TEXT NOT NULL,
    user_type_dict_id TEXT NOT NULL DEFAULT 'CUSTOMER' REFERENCES dictionaries(id) ON DELETE RESTRICT,
    customer_id TEXT NULL REFERENCES customers(id) ON DELETE RESTRICT,
    location_id TEXT NULL REFERENCES locations(id) ON DELETE RESTRICT,
    status TEXT NOT NULL DEFAULT 'ACTIVE' CHECK (status IN ('ACTIVE', 'INACTIVE', 'SUSPENDED')),
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    deleted_at TEXT DEFAULT NULL
);

CREATE TABLE IF NOT EXISTS staff (
    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),
    user_id TEXT NOT NULL UNIQUE REFERENCES users(id) ON DELETE RESTRICT,
    org_unit_id TEXT NOT NULL REFERENCES org_units(id) ON DELETE RESTRICT,
    staff_code TEXT NOT NULL UNIQUE,
    staff_type_dict_id TEXT REFERENCES dictionaries(id) ON DELETE RESTRICT,
    first_name TEXT NOT NULL,
    last_name TEXT NOT NULL,
    phone TEXT,
    location_id TEXT NOT NULL REFERENCES locations(id) ON DELETE RESTRICT,
    hire_date TEXT,
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    deleted_at TEXT DEFAULT NULL
);

CREATE TABLE IF NOT EXISTS roles (
    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),
    org_unit_id TEXT REFERENCES org_units(id) ON DELETE RESTRICT,
    role_code TEXT NOT NULL UNIQUE,
    role_name TEXT NOT NULL,
    description TEXT,
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    deleted_at TEXT DEFAULT NULL
);

CREATE TABLE IF NOT EXISTS permissions (
    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),
    perm_code TEXT NOT NULL UNIQUE,
    perm_name TEXT NOT NULL,
    module_name TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    deleted_at TEXT DEFAULT NULL
);

CREATE TABLE IF NOT EXISTS role_permissions (
    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),
    role_id TEXT NOT NULL REFERENCES roles(id) ON DELETE RESTRICT,
    permission_id TEXT NOT NULL REFERENCES permissions(id) ON DELETE RESTRICT,
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    deleted_at TEXT DEFAULT NULL,
    UNIQUE(role_id, permission_id)
);

CREATE TABLE IF NOT EXISTS user_roles (
    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE RESTRICT,
    role_id TEXT NOT NULL REFERENCES roles(id) ON DELETE RESTRICT,
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    deleted_at TEXT DEFAULT NULL,
    UNIQUE(user_id, role_id)
);

-- ============================================================================
-- 3. PRODUCT CATALOG & INVENTORY STOCK
-- ============================================================================

CREATE TABLE IF NOT EXISTS categories (
    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),
    parent_id TEXT REFERENCES categories(id) ON DELETE RESTRICT,
    cat_code TEXT NOT NULL UNIQUE,
    name TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    deleted_at TEXT DEFAULT NULL
);

CREATE TABLE IF NOT EXISTS products (
    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),
    category_id TEXT NOT NULL REFERENCES categories(id) ON DELETE RESTRICT,
    product_type_dict_id TEXT REFERENCES dictionaries(id) ON DELETE RESTRICT,
    sku TEXT NOT NULL UNIQUE,
    barcode TEXT,
    name TEXT NOT NULL,
    base_price REAL NOT NULL DEFAULT 0.0,
    cost_price REAL NOT NULL DEFAULT 0.0,
    tax_rate REAL NOT NULL DEFAULT 0.0,
    metadata TEXT DEFAULT NULL, -- JSON
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    deleted_at TEXT DEFAULT NULL
);

CREATE TABLE IF NOT EXISTS product_variants (
    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),
    product_id TEXT NOT NULL REFERENCES products(id) ON DELETE RESTRICT,
    sku TEXT NOT NULL UNIQUE,
    variant_name TEXT NOT NULL,
    price_delta REAL NOT NULL DEFAULT 0.0,
    attributes TEXT DEFAULT NULL, -- JSON: { "size": "L", "flavor": "Chocolate" }
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    deleted_at TEXT DEFAULT NULL
);

CREATE TABLE IF NOT EXISTS inventory_stocks (
    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),
    org_unit_id TEXT NOT NULL REFERENCES org_units(id) ON DELETE RESTRICT,
    product_id TEXT NOT NULL REFERENCES products(id) ON DELETE RESTRICT,
    variant_id TEXT REFERENCES product_variants(id) ON DELETE RESTRICT,
    quantity REAL NOT NULL DEFAULT 0.0,
    min_threshold REAL NOT NULL DEFAULT 0.0,
    max_threshold REAL NOT NULL DEFAULT 0.0,
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    deleted_at TEXT DEFAULT NULL,
    UNIQUE(org_unit_id, product_id, variant_id)
);

CREATE TABLE IF NOT EXISTS stock_ledger (
    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),
    stock_id TEXT NOT NULL REFERENCES inventory_stocks(id) ON DELETE RESTRICT,
    reference_type TEXT NOT NULL, -- ORDER, PURCHASE, ADJUST, RETURN
    reference_id TEXT,
    quantity_change REAL NOT NULL,
    balance_after REAL NOT NULL,
    note TEXT,
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    deleted_at TEXT DEFAULT NULL
);

-- ============================================================================
-- 4. CUSTOMERS & INTERACTION JSON DATA
-- ============================================================================

CREATE TABLE IF NOT EXISTS customers (
    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),
    customer_type_dict_id TEXT NOT NULL DEFAULT 'TEIR_1' REFERENCES dictionaries(id) ON DELETE RESTRICT,
    loyalty_points INTEGER NOT NULL DEFAULT 0,
    metadata TEXT DEFAULT NULL, -- JSON
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    deleted_at TEXT DEFAULT NULL
);

CREATE TABLE IF NOT EXISTS customer_interactions (
    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),
    user_id TEXT REFERENCES users(id) ON DELETE RESTRICT,
    customer_id TEXT REFERENCES customers(id) ON DELETE RESTRICT,
    interaction_type TEXT NOT NULL, -- REVIEW, LIKE, COMMENT, CART_TOUCH
    raw_payload TEXT NOT NULL, -- JSON: [{ User_id=001, Product: { Cake: id=2, Like=true, Comment="I order this Today!"} }]
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    deleted_at TEXT DEFAULT NULL
);

-- ============================================================================
-- 5. POS SHIFTS, ORDERS, ITEMS, DELIVERY & FINANCIALS
-- ============================================================================

CREATE TABLE IF NOT EXISTS cash_shifts (
    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),
    org_unit_id TEXT NOT NULL REFERENCES org_units(id) ON DELETE RESTRICT,
    staff_id TEXT NOT NULL REFERENCES staff(id) ON DELETE RESTRICT,
    opened_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    closed_at TEXT,
    opening_cash REAL NOT NULL DEFAULT 0.0,
    closing_cash REAL,
    expected_cash REAL,
    status TEXT NOT NULL DEFAULT 'OPEN' CHECK (status IN ('OPEN', 'CLOSED', 'AUDITED')),
    notes TEXT,
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    deleted_at TEXT DEFAULT NULL
);

CREATE TABLE IF NOT EXISTS orders (
    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),
    org_unit_id TEXT NOT NULL REFERENCES org_units(id) ON DELETE RESTRICT,
    shift_id TEXT REFERENCES cash_shifts(id) ON DELETE RESTRICT,
    customer_id TEXT REFERENCES customers(id) ON DELETE RESTRICT,
    staff_id TEXT NOT NULL REFERENCES staff(id) ON DELETE RESTRICT,
    order_number TEXT NOT NULL UNIQUE,
    order_status_dict_id TEXT REFERENCES dictionaries(id) ON DELETE RESTRICT,
    subtotal REAL NOT NULL DEFAULT 0.0,
    discount_amount REAL NOT NULL DEFAULT 0.0,
    tax_amount REAL NOT NULL DEFAULT 0.0,
    delivery_fee REAL NOT NULL DEFAULT 0.0, -- Included for sales/delivery financial reports
    total_amount REAL NOT NULL DEFAULT 0.0,
    order_metadata TEXT DEFAULT NULL, -- JSON
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    deleted_at TEXT DEFAULT NULL
);

CREATE TABLE IF NOT EXISTS order_items (
    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),
    order_id TEXT NOT NULL REFERENCES orders(id) ON DELETE RESTRICT,
    product_id TEXT NOT NULL REFERENCES products(id) ON DELETE RESTRICT,
    variant_id TEXT REFERENCES product_variants(id) ON DELETE RESTRICT,
    unit_price REAL NOT NULL DEFAULT 0.0,
    unit_cost REAL NOT NULL DEFAULT 0.0,
    quantity REAL NOT NULL DEFAULT 1.0,
    discount_amount REAL NOT NULL DEFAULT 0.0,
    tax_amount REAL NOT NULL DEFAULT 0.0,
    total_line REAL NOT NULL DEFAULT 0.0,
    item_metadata TEXT DEFAULT NULL, -- JSON
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    deleted_at TEXT DEFAULT NULL
);

CREATE TABLE IF NOT EXISTS deliveries (
    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),
    order_id TEXT NOT NULL UNIQUE REFERENCES orders(id) ON DELETE RESTRICT,
    driver_staff_id TEXT REFERENCES staff(id) ON DELETE RESTRICT,
    delivery_status TEXT NOT NULL DEFAULT 'pending' CHECK (delivery_status IN ('pending', 'in_transit', 'delivered', 'cancelled')),
    recipient_name TEXT NOT NULL,
    recipient_phone TEXT NOT NULL,
    delivery_address TEXT NOT NULL,
    delivery_cost REAL NOT NULL DEFAULT 0.0,
    dispatched_at TEXT,
    delivered_at TEXT,
    delivery_metadata TEXT DEFAULT NULL, -- JSON
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    deleted_at TEXT DEFAULT NULL
);

CREATE TABLE IF NOT EXISTS payments (
    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),
    order_id TEXT NOT NULL REFERENCES orders(id) ON DELETE RESTRICT,
    payment_method_dict_id TEXT REFERENCES dictionaries(id) ON DELETE RESTRICT,
    amount REAL NOT NULL DEFAULT 0.0,
    payment_status TEXT NOT NULL DEFAULT 'COMPLETED' CHECK (payment_status IN ('PENDING', 'COMPLETED', 'FAILED', 'REFUNDED')),
    transaction_ref TEXT,
    raw_payload TEXT DEFAULT NULL, -- JSON
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    deleted_at TEXT DEFAULT NULL
);

CREATE TABLE IF NOT EXISTS financial_ledgers (
    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),
    org_unit_id TEXT NOT NULL REFERENCES org_units(id) ON DELETE RESTRICT,
    reference_type TEXT NOT NULL, -- ORDER_PAYMENT, EXPENSE, CASH_IN, CASH_OUT
    reference_id TEXT,
    account_code TEXT NOT NULL,
    entry_type TEXT NOT NULL CHECK (entry_type IN ('DEBIT', 'CREDIT')),
    amount REAL NOT NULL,
    balance REAL NOT NULL,
    description TEXT,
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    deleted_at TEXT DEFAULT NULL
);

-- ============================================================================
-- 6. SYSTEM ALERTS (JSON STORAGE) & AUDIT LOGS
-- ============================================================================

CREATE TABLE IF NOT EXISTS system_alerts (
    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),
    user_id TEXT REFERENCES users(id) ON DELETE RESTRICT,
    order_id TEXT REFERENCES orders(id) ON DELETE RESTRICT,
    alert_type TEXT NOT NULL, -- MAIL, POPUP, ORDER_DELAY
    raw_payload TEXT NOT NULL, -- JSON: [{ Alert Mail: User_id=001, Alert Popup_seen=true, Message="20 minute take order", Order_id=231, Product [1, 2, 3] }]
    is_seen INTEGER NOT NULL DEFAULT 0 CHECK (is_seen IN (0, 1)),
    is_sent INTEGER NOT NULL DEFAULT 0 CHECK (is_sent IN (0, 1)),
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    deleted_at TEXT DEFAULT NULL
);

CREATE TABLE IF NOT EXISTS audit_logs (
    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),
    user_id TEXT REFERENCES users(id) ON DELETE RESTRICT,
    table_name TEXT NOT NULL,
    record_id TEXT NOT NULL,
    action TEXT NOT NULL CHECK (action IN ('INSERT', 'UPDATE', 'DELETE', 'SOFT_DELETE', 'RESTORE')),
    old_values TEXT, -- JSON
    new_values TEXT, -- JSON
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    deleted_at TEXT DEFAULT NULL
);

-- Seed dictionary rows with literal ids. FK column defaults reference
-- these ids ('CUSTOMER', 'TEIR_1'), so they must exist before any
-- users/customers insert. Codes double as ids (globally unique).
INSERT INTO dictionaries (id, category, code, label, sort_order) VALUES
    ('CUSTOMER', 'USER_TYPE', 'CUSTOMER', 'អតិថិជន', 1),
    ('ADMIN', 'USER_TYPE', 'ADMIN', 'អ្នកគ្រប់គ្រង', 2),
    ('GUEST', 'USER_TYPE', 'GUEST', 'ភ្ញៀវ', 3),
    ('TEIR_1', 'CUSTOMER_TYPE', 'TEIR_1', 'ថ្នាក់ទី ១', 1),
    ('TEIR_2', 'CUSTOMER_TYPE', 'TEIR_2', 'ថ្នាក់ទី ២', 2),
    ('TEIR_3', 'CUSTOMER_TYPE', 'TEIR_3', 'ថ្នាក់ទី ៣', 3),
    ('MANAGER', 'STAFF_TYPE', 'MANAGER', 'អ្នកគ្រប់គ្រងហាង', 1),
    ('CASHIER', 'STAFF_TYPE', 'CASHIER', 'អ្នកគិតលុយ', 2),
    ('SUPERVISOR', 'STAFF_TYPE', 'SUPERVISOR', 'អ្នកត្រួតពិនិត្យ', 3),
    ('DRIVER', 'STAFF_TYPE', 'DRIVER', 'អ្នកដឹកជញ្ជូន', 4),
    ('BRANCH', 'ORG_TYPE', 'BRANCH', 'សាខា', 1),
    ('WAREHOUSE', 'ORG_TYPE', 'WAREHOUSE', 'ឃ្លាំង', 2),
    ('HEADQUARTER', 'ORG_TYPE', 'HEADQUARTER', 'ការិយាល័យកណ្តាល', 3),
    ('PHYSICAL', 'PRODUCT_TYPE', 'PHYSICAL', 'ទំនិញផ្លែក', 1),
    ('DIGITAL', 'PRODUCT_TYPE', 'DIGITAL', 'ទំនិញឌីជីថល', 2),
    ('SERVICE', 'PRODUCT_TYPE', 'SERVICE', 'សេវាកម្ម', 3),
    ('PENDING', 'ORDER_STATUS', 'PENDING', 'កំពុងរង់ចាំ', 1),
    ('PAID', 'ORDER_STATUS', 'PAID', 'បានបង់ប្រាក់', 2),
    ('PACKED', 'ORDER_STATUS', 'PACKED', 'បានផ្គត់ផ្គង់', 3),
    ('SHIPPED', 'ORDER_STATUS', 'SHIPPED', 'បានផ្ញើ', 4),
    ('DELIVERED', 'ORDER_STATUS', 'DELIVERED', 'បានដឹកជញ្ជូន', 5),
    ('CANCELLED', 'ORDER_STATUS', 'CANCELLED', 'បានលុបចោល', 6),
    ('CASH', 'PAYMENT_METHOD', 'CASH', 'សាច់ប្រាក់', 1),
    ('CARD', 'PAYMENT_METHOD', 'CARD', 'កាត', 2),
    ('KHQR', 'PAYMENT_METHOD', 'KHQR', 'KHQR', 3),
    ('TRANSFER', 'PAYMENT_METHOD', 'TRANSFER', 'ផ្ទេរប្រាក់', 4);

-- Seed languages for the i18n page.
INSERT INTO languages (id, code, name, is_default, is_active) VALUES
    ('lang_km', 'km', 'ភាសាខ្មែរ', 1, 1),
    ('lang_en', 'en', 'English', 0, 1);

-- -----------------------------------------------------------------
-- users: demo (0002) shape -> POS shape, migrated IN PLACE.
-- Table swap instead of ALTER so every POS column, constraint and
-- default lands exactly as the vision schema defines them. DROP
-- before RENAME keeps notes.user_id REFERENCES users(id) pointing
-- at the name "users". db.c runs migrations with foreign_keys=OFF
-- (PRAGMA is a no-op inside the transaction), which is what makes
-- the DROP legal while notes still holds user ids. Deviations from
-- the vision: phone is NULL-able and profile_pic is kept, both for
-- demo compatibility, password_hash defaults to ''.
-- -----------------------------------------------------------------
CREATE TABLE users_pos (
    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),
    org_unit_id TEXT NULL REFERENCES org_units(id) ON DELETE RESTRICT,
    name TEXT NOT NULL,
    username TEXT NOT NULL UNIQUE,
    email TEXT NOT NULL UNIQUE,
    phone TEXT NULL UNIQUE,
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
-- Copy demo rows (timestamps carried over). Colliding usernames/emails
-- are de-duplicated here because the POS shape enforces UNIQUE: the
-- first row by id keeps the value, later rows get '_dup<id>' in the
-- username and '+dup<id>' before the '@' in the email.
INSERT INTO users_pos (id, name, username, email, profile_pic, created_at, updated_at)
SELECT src.id, src.name,
       CASE WHEN (SELECT count(*) FROM users d WHERE d.username = src.username) > 1
                 AND src.id <> (SELECT MIN(d.id) FROM users d WHERE d.username = src.username)
            THEN src.username || '_dup' || src.id ELSE src.username END,
       CASE WHEN (SELECT count(*) FROM users d WHERE d.email = src.email) > 1
                 AND src.id <> (SELECT MIN(d.id) FROM users d WHERE d.email = src.email)
            THEN substr(src.email, 1, instr(src.email, '@')) || 'dup' || src.id || substr(src.email, instr(src.email, '@')) ELSE src.email END,
       src.profile_pic, src.created_at, src.updated_at
FROM users AS src;
DROP TABLE users;
ALTER TABLE users_pos RENAME TO users;

-- ============================================================================
-- 7. INDEXES (PERFORMANCE TUNING)
-- ============================================================================

CREATE INDEX IF NOT EXISTS idx_orders_org_unit ON orders(org_unit_id, deleted_at);
CREATE INDEX IF NOT EXISTS idx_orders_customer ON orders(customer_id, deleted_at);
CREATE INDEX IF NOT EXISTS idx_orders_staff ON orders(staff_id, deleted_at);
CREATE INDEX IF NOT EXISTS idx_order_items_order ON order_items(order_id, deleted_at);
CREATE INDEX IF NOT EXISTS idx_payments_order ON payments(order_id, deleted_at);
CREATE INDEX IF NOT EXISTS idx_deliveries_order ON deliveries(order_id, deleted_at);
CREATE INDEX IF NOT EXISTS idx_stocks_product ON inventory_stocks(product_id, org_unit_id, deleted_at);
CREATE INDEX IF NOT EXISTS idx_alerts_user ON system_alerts(user_id, is_seen, deleted_at);
CREATE INDEX IF NOT EXISTS idx_translations_lookup ON translations(language_id, trans_key, deleted_at);

-- ============================================================================
-- 8. AUTO UPDATED_AT TRIGGERS
-- ============================================================================

CREATE TRIGGER IF NOT EXISTS trg_upd_users BEFORE UPDATE ON users BEGIN
    UPDATE users SET updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
    WHERE id = OLD.id;
END;
CREATE TRIGGER IF NOT EXISTS trg_upd_products BEFORE UPDATE ON products BEGIN
    UPDATE products SET updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
    WHERE id = OLD.id;
END;
CREATE TRIGGER IF NOT EXISTS trg_upd_orders BEFORE UPDATE ON orders BEGIN
    UPDATE orders SET updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
	WHERE id = OLD.id;
END;
CREATE TRIGGER IF NOT EXISTS trg_upd_deliveries BEFORE UPDATE ON deliveries BEGIN
    UPDATE deliveries SET updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
	WHERE id = OLD.id;
END;
CREATE TRIGGER IF NOT EXISTS trg_upd_stocks BEFORE UPDATE ON inventory_stocks BEGIN
    UPDATE inventory_stocks SET updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
	WHERE id = OLD.id;
END;

-- ============================================================================
-- 9. CASCADE SOFT-DELETE & RESTORE TRIGGERS
-- ============================================================================

-- ORG UNIT SOFT DELETE -> CASCADE TO USERS, STAFF, STOCKS
CREATE TRIGGER IF NOT EXISTS trg_soft_del_org_units
AFTER UPDATE OF deleted_at ON org_units
WHEN OLD.deleted_at IS NULL AND NEW.deleted_at IS NOT NULL
BEGIN
    UPDATE users SET deleted_at = NEW.deleted_at WHERE org_unit_id = OLD.id AND deleted_at IS NULL;
    UPDATE staff SET deleted_at = NEW.deleted_at WHERE org_unit_id = OLD.id AND deleted_at IS NULL;
    UPDATE inventory_stocks SET deleted_at = NEW.deleted_at WHERE org_unit_id = OLD.id AND deleted_at IS NULL;
END;

-- ORG UNIT RESTORE -> CASCADE RESTORE
CREATE TRIGGER IF NOT EXISTS trg_restore_org_units
AFTER UPDATE OF deleted_at ON org_units
WHEN OLD.deleted_at IS NOT NULL AND NEW.deleted_at IS NULL
BEGIN
    UPDATE users SET deleted_at = NULL WHERE org_unit_id = OLD.id AND deleted_at = OLD.deleted_at;
    UPDATE staff SET deleted_at = NULL WHERE org_unit_id = OLD.id AND deleted_at = OLD.deleted_at;
    UPDATE inventory_stocks SET deleted_at = NULL WHERE org_unit_id = OLD.id AND deleted_at = OLD.deleted_at;
END;

-- USER SOFT DELETE -> CASCADE TO USER_ROLES, ALERTS, INTERACTIONS
CREATE TRIGGER IF NOT EXISTS trg_soft_del_users
AFTER UPDATE OF deleted_at ON users
WHEN OLD.deleted_at IS NULL AND NEW.deleted_at IS NOT NULL
BEGIN
    UPDATE user_roles SET deleted_at = NEW.deleted_at WHERE user_id = OLD.id AND deleted_at IS NULL;
    UPDATE system_alerts SET deleted_at = NEW.deleted_at WHERE user_id = OLD.id AND deleted_at IS NULL;
    UPDATE customer_interactions SET deleted_at = NEW.deleted_at WHERE user_id = OLD.id AND deleted_at IS NULL;
END;

-- USER RESTORE -> CASCADE RESTORE
CREATE TRIGGER IF NOT EXISTS trg_restore_users
AFTER UPDATE OF deleted_at ON users
WHEN OLD.deleted_at IS NOT NULL AND NEW.deleted_at IS NULL
BEGIN
    UPDATE user_roles SET deleted_at = NULL WHERE user_id = OLD.id AND deleted_at = OLD.deleted_at;
    UPDATE system_alerts SET deleted_at = NULL WHERE user_id = OLD.id AND deleted_at = OLD.deleted_at;
    UPDATE customer_interactions SET deleted_at = NULL WHERE user_id = OLD.id AND deleted_at = OLD.deleted_at;
END;

-- PRODUCT SOFT DELETE -> CASCADE TO VARIANTS, STOCKS
CREATE TRIGGER IF NOT EXISTS trg_soft_del_products
AFTER UPDATE OF deleted_at ON products
WHEN OLD.deleted_at IS NULL AND NEW.deleted_at IS NOT NULL
BEGIN
    UPDATE product_variants SET deleted_at = NEW.deleted_at WHERE product_id = OLD.id AND deleted_at IS NULL;
    UPDATE inventory_stocks SET deleted_at = NEW.deleted_at WHERE product_id = OLD.id AND deleted_at IS NULL;
END;

-- PRODUCT RESTORE -> CASCADE RESTORE
CREATE TRIGGER IF NOT EXISTS trg_restore_products
AFTER UPDATE OF deleted_at ON products
WHEN OLD.deleted_at IS NOT NULL AND NEW.deleted_at IS NULL
BEGIN
    UPDATE product_variants SET deleted_at = NULL WHERE product_id = OLD.id AND deleted_at = OLD.deleted_at;
    UPDATE inventory_stocks SET deleted_at = NULL WHERE product_id = OLD.id AND deleted_at = OLD.deleted_at;
END;

-- ORDER SOFT DELETE -> CASCADE TO ORDER_ITEMS, DELIVERIES, PAYMENTS
CREATE TRIGGER IF NOT EXISTS trg_soft_del_orders
AFTER UPDATE OF deleted_at ON orders
WHEN OLD.deleted_at IS NULL AND NEW.deleted_at IS NOT NULL
BEGIN
    UPDATE order_items SET deleted_at = NEW.deleted_at WHERE order_id = OLD.id AND deleted_at IS NULL;
    UPDATE deliveries SET deleted_at = NEW.deleted_at WHERE order_id = OLD.id AND deleted_at IS NULL;
    UPDATE payments SET deleted_at = NEW.deleted_at WHERE order_id = OLD.id AND deleted_at IS NULL;
END;

-- ORDER RESTORE -> CASCADE RESTORE
CREATE TRIGGER IF NOT EXISTS trg_restore_orders
AFTER UPDATE OF deleted_at ON orders
WHEN OLD.deleted_at IS NOT NULL AND NEW.deleted_at IS NULL
BEGIN
    UPDATE order_items SET deleted_at = NULL WHERE order_id = OLD.id AND deleted_at = OLD.deleted_at;
    UPDATE deliveries SET deleted_at = NULL WHERE order_id = OLD.id AND deleted_at = OLD.deleted_at;
    UPDATE payments SET deleted_at = NULL WHERE order_id = OLD.id AND deleted_at = OLD.deleted_at;
END;

-- ============================================================================
-- 10. ACTIVE VIEWS & FINANCIAL REPORTING VIEW
-- ============================================================================

CREATE VIEW IF NOT EXISTS v_active_users AS 
SELECT * FROM users WHERE deleted_at IS NULL;

CREATE VIEW IF NOT EXISTS v_active_products AS 
SELECT * FROM products WHERE deleted_at IS NULL;

CREATE VIEW IF NOT EXISTS v_active_orders AS 
SELECT * FROM orders WHERE deleted_at IS NULL;

-- Comprehensive POS & Delivery Financial Report View.
-- Fixed vs the raw vision file: customers has no name column (person
-- names live in users.customer_id) and payment sums use a derived
-- table, so no GROUP BY over bare columns is needed.
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
