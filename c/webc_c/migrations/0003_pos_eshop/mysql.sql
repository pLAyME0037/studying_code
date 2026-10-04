-- ============================================================================
-- MYSQL/MARIADB PORT OF 0003_pos_eshop (canonical dialect: sqlite3)
-- Deltas vs the sqlite3 file, in the style of the 0001/0002 ports:
--   - TEXT ids to VARCHAR(36), REAL to DOUBLE, INTEGER to INT
--   - TEXT timestamp defaults to DATETIME DEFAULT CURRENT_TIMESTAMP
--   - inline REFERENCES clauses dropped (the sqlite3 file is the
--     canonical FK/trigger dialect)
--   - TRIGGERs dropped (the naive semicolon splitter cannot carry them)
--   - unique or indexed string columns get VARCHAR lengths
--   - v_pos_sales_delivery_report uses CONCAT with the fixed joins
-- NOTE for authors: no semicolons inside comments or string literals
-- (driver_mysql.c splits migration scripts on every semicolon)
-- ==========================================================================


-- ============================================================================
-- 1. SYSTEM CONFIG, I18N & MASTER DICTIONARIES
-- ============================================================================

CREATE TABLE IF NOT EXISTS system_configs (
    id VARCHAR(36) PRIMARY KEY DEFAULT ((UUID())),
    config_key VARCHAR(191) NOT NULL UNIQUE,
    config_value TEXT,
    json_payload TEXT,
    is_encrypted INT NOT NULL DEFAULT 0 CHECK (is_encrypted IN (0, 1)),
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    deleted_at DATETIME NULL
);

CREATE TABLE IF NOT EXISTS dictionaries (
    id VARCHAR(36) PRIMARY KEY DEFAULT ((UUID())),
    category VARCHAR(64) NOT NULL, -- PRODUCT_TYPE, CUSTOMER_TYPE, STAFF_TYPE, ORDER_STATUS, PAYMENT_METHOD
    code VARCHAR(64) NOT NULL,
    label TEXT NOT NULL,
    sort_order INT NOT NULL DEFAULT 0,
    metadata TEXT DEFAULT NULL, -- JSON
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    deleted_at DATETIME NULL,
    UNIQUE(category, code)
);

CREATE TABLE IF NOT EXISTS languages (
    id VARCHAR(36) PRIMARY KEY DEFAULT ((UUID())),
    code VARCHAR(64) NOT NULL UNIQUE, -- en, km, zh
    name TEXT NOT NULL,
    is_default INT NOT NULL DEFAULT 0 CHECK (is_default IN (0, 1)),
    is_active INT NOT NULL DEFAULT 1 CHECK (is_active IN (0, 1)),
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    deleted_at DATETIME NULL
);

-- Seed dictionary rows with literal ids. FK column defaults reference
-- these ids ('CUSTOMER', 'TEIR_1'), so they must exist before any
-- users/customers insert. Codes double as ids (globally unique).
INSERT IGNORE INTO dictionaries (id, category, code, label, sort_order) VALUES
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
INSERT IGNORE INTO languages (id, code, name, is_default, is_active) VALUES
    ('lang_km', 'km', 'ភាសាខ្មែរ', 1, 1),
    ('lang_en', 'en', 'English', 0, 1);

CREATE TABLE IF NOT EXISTS translations (
    id VARCHAR(36) PRIMARY KEY DEFAULT ((UUID())),
    language_id VARCHAR(36) NOT NULL,
    trans_key VARCHAR(191) NOT NULL,
    trans_value TEXT NOT NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    deleted_at DATETIME NULL,
    UNIQUE(language_id, trans_key)
);

-- ============================================================================
-- 2. Location
-- ============================================================================

CREATE TABLE IF NOT EXISTS locations (
    id VARCHAR(36) PRIMARY KEY DEFAULT ((UUID())),
    province TEXT NOT NULL,
    district TEXT NOT NULL,
    commune TEXT NOT NULL,
    village TEXT NOT NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    deleted_at DATETIME NULL
);

-- ============================================================================
-- 3. ORGANIZATIONAL UNITS & RBAC (USERS, STAFF, ROLES, PERMISSIONS)
-- ============================================================================

CREATE TABLE IF NOT EXISTS org_units (
    id VARCHAR(36) PRIMARY KEY DEFAULT ((UUID())),
    parent_id VARCHAR(36),
    ou_code VARCHAR(64) NOT NULL UNIQUE,
    ou_name TEXT NOT NULL,
    ou_type_dict_id VARCHAR(36),
    metadata TEXT DEFAULT NULL, -- JSON
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    deleted_at DATETIME NULL
);

-- ============================================================================
-- 4. CUSTOMERS & INTERACTION JSON DATA
-- ============================================================================

CREATE TABLE IF NOT EXISTS customers (
    id VARCHAR(36) PRIMARY KEY DEFAULT ((UUID())),
    customer_type_dict_id VARCHAR(36) NOT NULL DEFAULT 'TEIR_1',
    loyalty_points INT NOT NULL DEFAULT 0,
    metadata TEXT DEFAULT NULL, -- JSON
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    deleted_at DATETIME NULL
);

CREATE TABLE IF NOT EXISTS users (
    id VARCHAR(36) PRIMARY KEY DEFAULT ((UUID())),
    org_unit_id VARCHAR(36) NULL,
    name TEXT NOT NULL,
    username VARCHAR(191) NOT NULL UNIQUE,
    email VARCHAR(191) NOT NULL UNIQUE,
    phone VARCHAR(64) NOT NULL UNIQUE,
    password_hash VARCHAR(255) NOT NULL,
    user_type_dict_id VARCHAR(36) NOT NULL DEFAULT 'CUSTOMER',
    customer_id VARCHAR(36) NULL,
    location_id VARCHAR(36) NULL,
    status VARCHAR(32) NOT NULL DEFAULT 'ACTIVE' CHECK (status IN ('ACTIVE', 'INACTIVE', 'SUSPENDED')),
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    deleted_at DATETIME NULL
);

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
    id VARCHAR(36) PRIMARY KEY DEFAULT (UUID()),
    org_unit_id VARCHAR(36) NULL,
    name TEXT NOT NULL,
    username VARCHAR(191) NOT NULL UNIQUE,
    email VARCHAR(191) NOT NULL UNIQUE,
    phone VARCHAR(64) NULL UNIQUE,
    password_hash VARCHAR(255) NOT NULL DEFAULT '',
    user_type_dict_id VARCHAR(36) NOT NULL DEFAULT 'CUSTOMER',
    customer_id VARCHAR(36) NULL,
    location_id VARCHAR(36) NULL,
    status VARCHAR(36) NOT NULL DEFAULT 'ACTIVE' CHECK (status IN ('ACTIVE', 'INACTIVE', 'SUSPENDED')),
    profile_pic BLOB NULL, -- kept from the demo schema (superset of vision)
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    deleted_at DATETIME NULL
);
-- Copy demo rows (timestamps carried over). Colliding usernames/emails
-- are de-duplicated here because the POS shape enforces UNIQUE: the
-- first row by id keeps the value, later rows get '_dup<id>' in the
-- username and '+dup<id>' before the '@' in the email.
INSERT IGNORE INTO users_pos (id, name, username, email, profile_pic, created_at, updated_at)
SELECT src.id, src.name,
       CASE WHEN (SELECT count(*) FROM users d WHERE d.username = src.username) > 1
                 AND src.id <> (SELECT MIN(d.id) FROM users d WHERE d.username = src.username)
            THEN CONCAT(src.username, '_dup', src.id) ELSE src.username END,
       CASE WHEN (SELECT count(*) FROM users d WHERE d.email = src.email) > 1
                 AND src.id <> (SELECT MIN(d.id) FROM users d WHERE d.email = src.email)
            THEN CONCAT(substr(src.email, 1, INSTR(src.email, '@')), 'dup', src.id, substr(src.email, INSTR(src.email, '@'))) ELSE src.email END,
       src.profile_pic, src.created_at, src.updated_at
FROM users AS src;
DROP TABLE users;
ALTER TABLE users_pos RENAME TO users;


CREATE TABLE IF NOT EXISTS staff (
    id VARCHAR(36) PRIMARY KEY DEFAULT ((UUID())),
    user_id VARCHAR(36) NOT NULL UNIQUE,
    org_unit_id VARCHAR(36) NOT NULL,
    staff_code VARCHAR(64) NOT NULL UNIQUE,
    staff_type_dict_id VARCHAR(36),
    first_name TEXT NOT NULL,
    last_name TEXT NOT NULL,
    phone VARCHAR(64),
    location_id VARCHAR(36) NOT NULL,
    hire_date TEXT,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    deleted_at DATETIME NULL
);

CREATE TABLE IF NOT EXISTS roles (
    id VARCHAR(36) PRIMARY KEY DEFAULT ((UUID())),
    org_unit_id VARCHAR(36),
    role_code VARCHAR(64) NOT NULL UNIQUE,
    role_name TEXT NOT NULL,
    description TEXT,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    deleted_at DATETIME NULL
);

CREATE TABLE IF NOT EXISTS permissions (
    id VARCHAR(36) PRIMARY KEY DEFAULT ((UUID())),
    perm_code VARCHAR(64) NOT NULL UNIQUE,
    perm_name TEXT NOT NULL,
    module_name TEXT NOT NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    deleted_at DATETIME NULL
);

CREATE TABLE IF NOT EXISTS role_permissions (
    id VARCHAR(36) PRIMARY KEY DEFAULT ((UUID())),
    role_id VARCHAR(36) NOT NULL,
    permission_id VARCHAR(36) NOT NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    deleted_at DATETIME NULL,
    UNIQUE(role_id, permission_id)
);

CREATE TABLE IF NOT EXISTS user_roles (
    id VARCHAR(36) PRIMARY KEY DEFAULT ((UUID())),
    user_id VARCHAR(36) NOT NULL,
    role_id VARCHAR(36) NOT NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    deleted_at DATETIME NULL,
    UNIQUE(user_id, role_id)
);

-- ============================================================================
-- 3. PRODUCT CATALOG & INVENTORY STOCK
-- ============================================================================

CREATE TABLE IF NOT EXISTS categories (
    id VARCHAR(36) PRIMARY KEY DEFAULT ((UUID())),
    parent_id VARCHAR(36),
    cat_code VARCHAR(64) NOT NULL UNIQUE,
    name TEXT NOT NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    deleted_at DATETIME NULL
);

CREATE TABLE IF NOT EXISTS products (
    id VARCHAR(36) PRIMARY KEY DEFAULT ((UUID())),
    category_id VARCHAR(36) NOT NULL,
    product_type_dict_id VARCHAR(36),
    sku VARCHAR(64) NOT NULL UNIQUE,
    barcode TEXT,
    name TEXT NOT NULL,
    base_price DOUBLE NOT NULL DEFAULT 0.0,
    cost_price DOUBLE NOT NULL DEFAULT 0.0,
    tax_rate DOUBLE NOT NULL DEFAULT 0.0,
    metadata TEXT DEFAULT NULL, -- JSON
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    deleted_at DATETIME NULL
);

CREATE TABLE IF NOT EXISTS product_variants (
    id VARCHAR(36) PRIMARY KEY DEFAULT ((UUID())),
    product_id VARCHAR(36) NOT NULL,
    sku VARCHAR(64) NOT NULL UNIQUE,
    variant_name TEXT NOT NULL,
    price_delta DOUBLE NOT NULL DEFAULT 0.0,
    attributes TEXT DEFAULT NULL, -- JSON: { "size": "L", "flavor": "Chocolate" }
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    deleted_at DATETIME NULL
);

CREATE TABLE IF NOT EXISTS inventory_stocks (
    id VARCHAR(36) PRIMARY KEY DEFAULT ((UUID())),
    org_unit_id VARCHAR(36) NOT NULL,
    product_id VARCHAR(36) NOT NULL,
    variant_id VARCHAR(36),
    quantity DOUBLE NOT NULL DEFAULT 0.0,
    min_threshold DOUBLE NOT NULL DEFAULT 0.0,
    max_threshold DOUBLE NOT NULL DEFAULT 0.0,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    deleted_at DATETIME NULL,
    UNIQUE(org_unit_id, product_id, variant_id)
);

CREATE TABLE IF NOT EXISTS stock_ledger (
    id VARCHAR(36) PRIMARY KEY DEFAULT ((UUID())),
    stock_id VARCHAR(36) NOT NULL,
    reference_type TEXT NOT NULL, -- ORDER, PURCHASE, ADJUST, RETURN
    reference_id VARCHAR(36),
    quantity_change DOUBLE NOT NULL,
    balance_after DOUBLE NOT NULL,
    note TEXT,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    deleted_at DATETIME NULL
);

CREATE TABLE IF NOT EXISTS customer_interactions (
    id VARCHAR(36) PRIMARY KEY DEFAULT ((UUID())),
    user_id VARCHAR(36),
    customer_id VARCHAR(36),
    interaction_type TEXT NOT NULL, -- REVIEW, LIKE, COMMENT, CART_TOUCH
    raw_payload TEXT NOT NULL, -- JSON: [{ User_id=001, Product: { Cake: id=2, Like=true, Comment="I order this Today!"} }]
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    deleted_at DATETIME NULL
);

-- ============================================================================
-- 5. POS SHIFTS, ORDERS, ITEMS, DELIVERY & FINANCIALS
-- ============================================================================

CREATE TABLE IF NOT EXISTS cash_shifts (
    id VARCHAR(36) PRIMARY KEY DEFAULT ((UUID())),
    org_unit_id VARCHAR(36) NOT NULL,
    staff_id VARCHAR(36) NOT NULL,
    opened_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    closed_at TEXT,
    opening_cash DOUBLE NOT NULL DEFAULT 0.0,
    closing_cash DOUBLE,
    expected_cash DOUBLE,
    status VARCHAR(32) NOT NULL DEFAULT 'OPEN' CHECK (status IN ('OPEN', 'CLOSED', 'AUDITED')),
    notes TEXT,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    deleted_at DATETIME NULL
);

CREATE TABLE IF NOT EXISTS orders (
    id VARCHAR(36) PRIMARY KEY DEFAULT ((UUID())),
    org_unit_id VARCHAR(36) NOT NULL,
    shift_id VARCHAR(36),
    customer_id VARCHAR(36),
    staff_id VARCHAR(36) NOT NULL,
    order_number VARCHAR(64) NOT NULL UNIQUE,
    order_status_dict_id VARCHAR(36),
    subtotal DOUBLE NOT NULL DEFAULT 0.0,
    discount_amount DOUBLE NOT NULL DEFAULT 0.0,
    tax_amount DOUBLE NOT NULL DEFAULT 0.0,
    delivery_fee DOUBLE NOT NULL DEFAULT 0.0, -- Included for sales/delivery financial reports
    total_amount DOUBLE NOT NULL DEFAULT 0.0,
    order_metadata TEXT DEFAULT NULL, -- JSON
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    deleted_at DATETIME NULL
);

CREATE TABLE IF NOT EXISTS order_items (
    id VARCHAR(36) PRIMARY KEY DEFAULT ((UUID())),
    order_id VARCHAR(36) NOT NULL,
    product_id VARCHAR(36) NOT NULL,
    variant_id VARCHAR(36),
    unit_price DOUBLE NOT NULL DEFAULT 0.0,
    unit_cost DOUBLE NOT NULL DEFAULT 0.0,
    quantity DOUBLE NOT NULL DEFAULT 1.0,
    discount_amount DOUBLE NOT NULL DEFAULT 0.0,
    tax_amount DOUBLE NOT NULL DEFAULT 0.0,
    total_line DOUBLE NOT NULL DEFAULT 0.0,
    item_metadata TEXT DEFAULT NULL, -- JSON
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    deleted_at DATETIME NULL
);

CREATE TABLE IF NOT EXISTS deliveries (
    id VARCHAR(36) PRIMARY KEY DEFAULT ((UUID())),
    order_id VARCHAR(36) NOT NULL UNIQUE,
    driver_staff_id VARCHAR(36),
    delivery_status VARCHAR(32) NOT NULL DEFAULT 'pending' CHECK (delivery_status IN ('pending', 'in_transit', 'delivered', 'cancelled')),
    recipient_name TEXT NOT NULL,
    recipient_phone TEXT NOT NULL,
    delivery_address TEXT NOT NULL,
    delivery_cost DOUBLE NOT NULL DEFAULT 0.0,
    dispatched_at TEXT,
    delivered_at TEXT,
    delivery_metadata TEXT DEFAULT NULL, -- JSON
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    deleted_at DATETIME NULL
);

CREATE TABLE IF NOT EXISTS payments (
    id VARCHAR(36) PRIMARY KEY DEFAULT ((UUID())),
    order_id VARCHAR(36) NOT NULL,
    payment_method_dict_id VARCHAR(36),
    amount DOUBLE NOT NULL DEFAULT 0.0,
    payment_status VARCHAR(32) NOT NULL DEFAULT 'COMPLETED' CHECK (payment_status IN ('PENDING', 'COMPLETED', 'FAILED', 'REFUNDED')),
    transaction_ref TEXT,
    raw_payload TEXT DEFAULT NULL, -- JSON
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    deleted_at DATETIME NULL
);

CREATE TABLE IF NOT EXISTS financial_ledgers (
    id VARCHAR(36) PRIMARY KEY DEFAULT ((UUID())),
    org_unit_id VARCHAR(36) NOT NULL,
    reference_type TEXT NOT NULL, -- ORDER_PAYMENT, EXPENSE, CASH_IN, CASH_OUT
    reference_id VARCHAR(36),
    account_code VARCHAR(64) NOT NULL,
    entry_type TEXT NOT NULL CHECK (entry_type IN ('DEBIT', 'CREDIT')),
    amount DOUBLE NOT NULL,
    balance DOUBLE NOT NULL,
    description TEXT,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    deleted_at DATETIME NULL
);

-- ============================================================================
-- 6. SYSTEM ALERTS (JSON STORAGE) & AUDIT LOGS
-- ============================================================================

CREATE TABLE IF NOT EXISTS system_alerts (
    id VARCHAR(36) PRIMARY KEY DEFAULT ((UUID())),
    user_id VARCHAR(36),
    order_id VARCHAR(36),
    alert_type TEXT NOT NULL, -- MAIL, POPUP, ORDER_DELAY
    raw_payload TEXT NOT NULL, -- JSON: [{ Alert Mail: User_id=001, Alert Popup_seen=true, Message="20 minute take order", Order_id=231, Product [1, 2, 3] }]
    is_seen INT NOT NULL DEFAULT 0 CHECK (is_seen IN (0, 1)),
    is_sent INT NOT NULL DEFAULT 0 CHECK (is_sent IN (0, 1)),
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    deleted_at DATETIME NULL
);

CREATE TABLE IF NOT EXISTS audit_logs (
    id VARCHAR(36) PRIMARY KEY DEFAULT ((UUID())),
    user_id VARCHAR(36),
    table_name TEXT NOT NULL,
    record_id VARCHAR(36) NOT NULL,
    action TEXT NOT NULL CHECK (action IN ('INSERT', 'UPDATE', 'DELETE', 'SOFT_DELETE', 'RESTORE')),
    old_values TEXT, -- JSON
    new_values TEXT, -- JSON
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    deleted_at DATETIME NULL
);


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
    CONCAT(st.first_name, ' ', st.last_name) AS cashier_name,
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
