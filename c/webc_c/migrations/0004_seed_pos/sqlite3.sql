-- ============================================================================
-- 0004_seed_pos: demo/seed data for the POS tables (Phase 9).
--   * idempotent (INSERT OR IGNORE) + relative timestamps, so a fresh
--     suite DB always gets "orders across the last 14 days";
--   * ALL created_at values sit in the PAST (-150..-1 days) on purpose:
--     demo pages sort created_at DESC, and demo tests must keep seeing
--     their own recent rows on page 1 of the default per_page=20 lists;
--   * order_items are inserted BEFORE orders (FKs are off during
--     migrations), so orders can carry a subtotal that really equals the
--     sum of its lines;
--   * no dictionaries/system_configs/i18n rows: those are covered by
--     0003/0005 test fixtures and keep their own empty-state tests.
-- sqlite3 is the canonical dialect; mysql.sql/postgres.sql document the
-- intentional skip (POS feature tests are sqlite-only).
-- ============================================================================

-- ---------------------------------------------------------------------------
-- 1. Locations (Khmer address chain)
-- ---------------------------------------------------------------------------
INSERT OR IGNORE INTO locations (id, province, district, commune, village, created_at) VALUES
    ('sd-loc-1', 'ភ្នំពេញ', 'ចំការមន', 'ផ្សារថ្មី', 'ភូមិផលិតផល ១', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-300 days')),
    ('sd-loc-2', 'ភ្នំពេញ', 'ដូនពេញ', 'ទួលគោក', 'ភូមិផលិតផល ២', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-300 days')),
    ('sd-loc-3', 'សៀមរាប', 'សៀមរាប', 'សាលាតិច', 'ភូមិផលិតផល ៣', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-300 days')),
    ('sd-loc-4', 'បាត់ដំបង', 'បាត់ដំបង', 'អូរឫស្សី', 'ភូមិផលិតផល ៤', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-300 days')),
    ('sd-loc-5', 'កំពង់ចាម', 'កំពង់ចាម', 'កំពង់ចាម', 'ភូមិផលិតផល ៥', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-300 days'));

-- ---------------------------------------------------------------------------
-- 2. One org unit (branch) -- tests create their own 'pos-ou-1'
-- ---------------------------------------------------------------------------
INSERT OR IGNORE INTO org_units (id, parent_id, ou_code, ou_name, ou_type_dict_id, created_at) VALUES
    ('sd-ou-phm', NULL, 'SD-MAIN', 'ហាងកណ្តាលភ្នំពេញ', 'BRANCH',
     strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-300 days'));

-- ---------------------------------------------------------------------------
-- 3. Categories
-- ---------------------------------------------------------------------------
INSERT OR IGNORE INTO categories (id, parent_id, cat_code, name, created_at) VALUES
    ('sd-cat-1', NULL, 'SD-CAT-01', 'ទូរស័ព្ទ', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-280 days')),
    ('sd-cat-2', NULL, 'SD-CAT-02', 'កុំព្យូទ័រ', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-280 days')),
    ('sd-cat-3', NULL, 'SD-CAT-03', 'គ្រឿងបន្លាស់', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-280 days')),
    ('sd-cat-4', NULL, 'SD-CAT-04', 'អាគុយ និងថ្ម', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-280 days')),
    ('sd-cat-5', NULL, 'SD-CAT-05', 'ឧបករណ៍ការពារ', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-280 days'));

-- ---------------------------------------------------------------------------
-- 4. Products: 50 rows = 10 Khmer nouns x 5 adjectives (unique names via
--    the row number), category cycles, price = 15 + n*37 % 900.
-- ---------------------------------------------------------------------------
INSERT OR IGNORE INTO products (id, category_id, product_type_dict_id, sku, name,
                                base_price, cost_price, tax_rate, created_at)
WITH RECURSIVE seq(n) AS (SELECT 1 UNION ALL SELECT n + 1 FROM seq WHERE n < 50)
SELECT 'sd-prod-' || printf('%03d', n),
       'sd-cat-' || ((n - 1) % 5 + 1),
       'PHYSICAL',
       'SD-P-' || printf('%03d', n),
       (SELECT w.label FROM
            (SELECT 1 AS i, 'ស្តង់ដារ' AS label UNION ALL
             SELECT 2, 'ល្បឿនលឿន' UNION ALL
             SELECT 3, 'ស៊េរីថ្មី' UNION ALL
             SELECT 4, 'រឹងមាំ' UNION ALL
             SELECT 5, 'តូចល្មម') w
        WHERE w.i = (n % 5) + 1)
       || ' ' ||
       (SELECT u.label FROM
            (SELECT 1 AS i, 'ទូរស័ព្ទឆ្លាត' AS label UNION ALL
             SELECT 2, 'កុំព្យូទ័រលើតុ' UNION ALL
             SELECT 3, 'កាសស៊ីស' UNION ALL
             SELECT 4, 'ខ្សែសាក USB' UNION ALL
             SELECT 5, 'ថ្មសាកពាក់លើ' UNION ALL
             SELECT 6, 'គំរបការពារ' UNION ALL
             SELECT 7, 'កញ្ចក់ការពារ' UNION ALL
             SELECT 8, 'ម៉ោនឆ្លុះ' UNION ALL
             SELECT 9, 'បន្ទះឥវ៉ាន់' UNION ALL
             SELECT 10, 'ថាសផ្ទុក') u
        WHERE u.i = (n % 10) + 1)
       || ' ' || n,
       ROUND(15.0 + ((n * 37) % 900), 2),
       ROUND((15.0 + ((n * 37) % 900)) * 0.6, 2),
       CASE WHEN n % 4 = 0 THEN 0.1 ELSE 0.0 END,
       strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-200 days')
FROM seq;

-- ---------------------------------------------------------------------------
-- 5. One variant per product (black/white, some with a price bump)
-- ---------------------------------------------------------------------------
INSERT OR IGNORE INTO product_variants (id, product_id, sku, variant_name,
                                        price_delta, created_at)
WITH RECURSIVE seq(n) AS (SELECT 1 UNION ALL SELECT n + 1 FROM seq WHERE n < 50)
SELECT 'sd-var-' || printf('%03d', n),
       'sd-prod-' || printf('%03d', n),
       'SD-V-' || printf('%03d', n),
       CASE WHEN n % 2 = 0 THEN 'ពណ៌ខ្មៅ' ELSE 'ពណ៌ស' END || ' · ' || printf('%03d', n),
       CASE WHEN n % 4 = 0 THEN 20000.0 ELSE 0.0 END,
       strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-200 days')
FROM seq;

-- ---------------------------------------------------------------------------
-- 6. Stock for every product; the first six sit at/below their min so the
--    low-stock report has rows (quantity <= min_threshold).
-- ---------------------------------------------------------------------------
INSERT OR IGNORE INTO inventory_stocks (id, org_unit_id, product_id, variant_id,
                                        quantity, min_threshold, max_threshold, created_at)
WITH RECURSIVE seq(n) AS (SELECT 1 UNION ALL SELECT n + 1 FROM seq WHERE n < 50)
SELECT 'sd-stock-' || printf('%03d', n),
       'sd-ou-phm',
       'sd-prod-' || printf('%03d', n),
       'sd-var-' || printf('%03d', n),
       CASE WHEN n <= 6 THEN 2 + n ELSE 10 + ((n * 7) % 120) END,
       CASE WHEN n <= 6 THEN 10.0 ELSE 5.0 END,
       200.0,
       strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-190 days')
FROM seq;

-- ---------------------------------------------------------------------------
-- 7. Customers (loyalty varies)
-- ---------------------------------------------------------------------------
INSERT OR IGNORE INTO customers (id, customer_type_dict_id, loyalty_points, metadata, created_at) VALUES
    ('sd-cus-1', 'TEIR_2', 240, NULL, strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-120 days')),
    ('sd-cus-2', 'TEIR_1', 35,  NULL, strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-120 days')),
    ('sd-cus-3', 'TEIR_3', 860, NULL, strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-115 days')),
    ('sd-cus-4', 'TEIR_1', 10,  NULL, strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-115 days')),
    ('sd-cus-5', 'TEIR_2', 415, NULL, strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-110 days')),
    ('sd-cus-6', 'TEIR_3', 720, NULL, strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-110 days')),
    ('sd-cus-7', 'TEIR_1', 55,  NULL, strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-105 days')),
    ('sd-cus-8', 'TEIR_2', 330, NULL, strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-105 days'));

-- ---------------------------------------------------------------------------
-- 8. Users: 3 staff accounts + 5 customer accounts (users.customer_id is
--    what feeds customer_name in the sales report view). All old enough
--    to stay below demo rows in created_at DESC lists.
-- ---------------------------------------------------------------------------
INSERT OR IGNORE INTO users (id, org_unit_id, name, username, email, phone,
                             password_hash, user_type_dict_id, customer_id,
                             location_id, status, created_at) VALUES
    ('sd-user-1', 'sd-ou-phm', 'សុខ ដារា',   'sd.staff1', 'sd.staff1@pos.kh', '0121000001', '', 'ADMIN',   NULL, 'sd-loc-1', 'ACTIVE', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-150 days')),
    ('sd-user-2', 'sd-ou-phm', 'ចាន់ថា ស្រីពៅ', 'sd.staff2', 'sd.staff2@pos.kh', '0121000002', '', 'ADMIN',   NULL, 'sd-loc-1', 'ACTIVE', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-150 days')),
    ('sd-user-3', 'sd-ou-phm', 'វណ្ណារ៉ា សុខ', 'sd.staff3', 'sd.staff3@pos.kh', '0121000003', '', 'ADMIN',   NULL, 'sd-loc-2', 'ACTIVE', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-150 days')),
    ('sd-user-4', NULL,        'ឡាច វិចិត្រ',   'sd.cust1',  'sd.cust1@pos.kh',  '0122000001', '', 'CUSTOMER', 'sd-cus-1', NULL, 'ACTIVE', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-100 days')),
    ('sd-user-5', NULL,        'សែស ម៉ាលី',   'sd.cust2',  'sd.cust2@pos.kh',  '0122000002', '', 'CUSTOMER', 'sd-cus-2', NULL, 'ACTIVE', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-100 days')),
    ('sd-user-6', NULL,        'រតនៈ ស្រីណែត','sd.cust3',  'sd.cust3@pos.kh',  '0122000003', '', 'CUSTOMER', 'sd-cus-3', NULL, 'ACTIVE', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-95 days')),
    ('sd-user-7', NULL,        'គង់ សុផល',    'sd.cust4',  'sd.cust4@pos.kh',  '0122000004', '', 'CUSTOMER', 'sd-cus-5', NULL, 'ACTIVE', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-95 days')),
    ('sd-user-8', NULL,        'ប៉ែន សុជាតា',  'sd.cust5',  'sd.cust5@pos.kh',  '0122000005', '', 'CUSTOMER', 'sd-cus-6', NULL, 'ACTIVE', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-90 days'));

-- ---------------------------------------------------------------------------
-- 9. Staff (tests create their own 'pos-stf-1')
-- ---------------------------------------------------------------------------
INSERT OR IGNORE INTO staff (id, user_id, org_unit_id, staff_code, staff_type_dict_id,
                             first_name, last_name, phone, location_id, hire_date, created_at) VALUES
    ('sd-stf-1', 'sd-user-1', 'sd-ou-phm', 'SD-STF-01', 'MANAGER',   'សុខ',   'ដារា',   '0121000001', 'sd-loc-1', '2025-01-06', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-150 days')),
    ('sd-stf-2', 'sd-user-2', 'sd-ou-phm', 'SD-STF-02', 'CASHIER',   'ចាន់ថា', 'ស្រីពៅ', '0121000002', 'sd-loc-1', '2025-03-03', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-150 days')),
    ('sd-stf-3', 'sd-user-3', 'sd-ou-phm', 'SD-STF-03', 'DRIVER',    'វណ្ណារ៉ា','សុខ',    '0121000003', 'sd-loc-2', '2025-06-02', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-150 days'));

-- ---------------------------------------------------------------------------
-- 10. Roles + permissions + links (tests create their own 'pos-role-*')
-- ---------------------------------------------------------------------------
INSERT OR IGNORE INTO roles (id, org_unit_id, role_code, role_name, description, created_at) VALUES
    ('sd-role-manager',  'sd-ou-phm', 'SD-MANAGER', 'អ្នកគ្រប់គ្រងហាង', 'គ្រប់គ្រងទាំងអស់',       strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-140 days')),
    ('sd-role-cashier',  'sd-ou-phm', 'SD-CASHIER', 'អ្នកគិតលុយ',       'លក់ និងទទួលប្រាក់',       strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-140 days')),
    ('sd-role-driver',   'sd-ou-phm', 'SD-DRIVER',  'អ្នកដឹកជញ្ជូន',     'ដឹកជញ្ជូនកម្មង់',         strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-140 days'));

INSERT OR IGNORE INTO permissions (id, perm_code, perm_name, module_name, created_at) VALUES
    ('sd-perm-1', 'SD.SELL',         'លក់ទំនិញ',       'orders',    strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-140 days')),
    ('sd-perm-2', 'SD.PRODUCT.MGMT', 'គ្រប់គ្រងផលិតផល', 'products',  strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-140 days')),
    ('sd-perm-3', 'SD.STOCK.ADJUST', 'កែប្រែស្តុក',     'stocks',    strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-140 days')),
    ('sd-perm-4', 'SD.REPORT.VIEW',  'មើលរបាយការណ៍',   'reports',   strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-140 days')),
    ('sd-perm-5', 'SD.USER.MGMT',    'គ្រប់គ្រងអ្នកប្រើ', 'users',     strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-140 days')),
    ('sd-perm-6', 'SD.DISCOUNT',     'ផ្តល់បញ្ចុតតម្លៃ', 'orders',    strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-140 days')),
    ('sd-perm-7', 'SD.REFUND',       'សងប្រាក់វិញ',     'payments',  strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-140 days')),
    ('sd-perm-8', 'SD.SETTINGS',     'ការកំណត់ប្រព័ន្ធ', 'config',    strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-140 days'));

INSERT OR IGNORE INTO role_permissions (id, role_id, permission_id, created_at) VALUES
    ('sd-rp-m1', 'sd-role-manager', 'sd-perm-1', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-135 days')),
    ('sd-rp-m2', 'sd-role-manager', 'sd-perm-2', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-135 days')),
    ('sd-rp-m3', 'sd-role-manager', 'sd-perm-3', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-135 days')),
    ('sd-rp-m4', 'sd-role-manager', 'sd-perm-4', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-135 days')),
    ('sd-rp-m5', 'sd-role-manager', 'sd-perm-5', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-135 days')),
    ('sd-rp-m6', 'sd-role-manager', 'sd-perm-6', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-135 days')),
    ('sd-rp-m7', 'sd-role-manager', 'sd-perm-7', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-135 days')),
    ('sd-rp-m8', 'sd-role-manager', 'sd-perm-8', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-135 days')),
    ('sd-rp-c1', 'sd-role-cashier', 'sd-perm-1', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-135 days')),
    ('sd-rp-c2', 'sd-role-cashier', 'sd-perm-6', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-135 days')),
    ('sd-rp-c3', 'sd-role-cashier', 'sd-perm-4', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-135 days'));

INSERT OR IGNORE INTO user_roles (id, user_id, role_id, created_at) VALUES
    ('sd-ur-1', 'sd-user-1', 'sd-role-manager', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-135 days')),
    ('sd-ur-2', 'sd-user-2', 'sd-role-cashier', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-135 days')),
    ('sd-ur-3', 'sd-user-3', 'sd-role-driver',  strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-135 days'));

-- ---------------------------------------------------------------------------
-- 11. Cash shifts (one closed, one still open)
-- ---------------------------------------------------------------------------
INSERT OR IGNORE INTO cash_shifts (id, org_unit_id, staff_id, opened_at, closed_at,
                                   opening_cash, closing_cash, expected_cash,
                                   status, notes, created_at) VALUES
    ('sd-shift-1', 'sd-ou-phm', 'sd-stf-1',
     strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-5 days', '+8 hours'),
     strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-5 days', '+17 hours'),
     200.0, 512.5, 512.5, 'CLOSED', 'សុតសិល',
     strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-5 days')),
    ('sd-shift-2', 'sd-ou-phm', 'sd-stf-2',
     strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-1 days', '+8 hours'),
     NULL, 150.0, NULL, NULL, 'OPEN', NULL,
     strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-1 days'));

-- ---------------------------------------------------------------------------
-- 12. Order lines first (2 per order, 42 orders): FKs are off, so the
--     orders can then carry subtotals that equal SUM(total_line).
-- ---------------------------------------------------------------------------
INSERT OR IGNORE INTO order_items (id, order_id, product_id, variant_id,
                                   unit_price, unit_cost, quantity,
                                   discount_amount, tax_amount, total_line, created_at)
WITH RECURSIVE o(n) AS (SELECT 1 UNION ALL SELECT n + 1 FROM o WHERE n < 42),
               k(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM k WHERE i < 2)
SELECT 'sd-item-' || printf('%03d', o.n) || '-' || k.i,
       'sd-ord-' || printf('%03d', o.n),
       'sd-prod-' || printf('%03d', ((o.n * 3 + k.i * 7) % 50) + 1),
       'sd-var-' || printf('%03d', ((o.n * 3 + k.i * 7) % 50) + 1),
       p.base_price,
       p.cost_price,
       ((o.n + k.i) % 4) + 1,
       0.0,
       0.0,
       ROUND(p.base_price * (((o.n + k.i) % 4) + 1), 2),
       strftime('%Y-%m-%dT%H:%M:%fZ', 'now',
                '-' || ((42 - o.n) / 3) || ' days',
                '+' || ((o.n % 9) + 8) || ' hours')
FROM o, k
JOIN products p
  ON p.id = 'sd-prod-' || printf('%03d', ((o.n * 3 + k.i * 7) % 50) + 1);

-- ---------------------------------------------------------------------------
-- 13. Orders across the last 14 days; subtotal/total = sum of the lines
--     (+ delivery fee every third order).
-- ---------------------------------------------------------------------------
INSERT OR IGNORE INTO orders (id, org_unit_id, shift_id, customer_id, staff_id,
                              order_number, order_status_dict_id,
                              subtotal, discount_amount, tax_amount,
                              delivery_fee, total_amount,
                              created_at, updated_at)
WITH RECURSIVE n(k) AS (SELECT 1 UNION ALL SELECT k + 1 FROM n WHERE k < 42)
SELECT 'sd-ord-' || printf('%03d', k),
       'sd-ou-phm',
       CASE WHEN k % 5 = 0 THEN NULL
            WHEN k % 2 = 0 THEN 'sd-shift-1'
            ELSE 'sd-shift-2' END,
       CASE WHEN k % 5 = 0 THEN NULL
            ELSE 'sd-cus-' || ((k % 8) + 1) END,
       'sd-stf-' || ((k % 3) + 1),
       'SD-ORD-' || printf('%03d', k),
       (SELECT s.id FROM
            (SELECT 1 AS i, 'PENDING' AS id UNION ALL
             SELECT 2, 'PAID' UNION ALL
             SELECT 3, 'PACKED' UNION ALL
             SELECT 4, 'SHIPPED' UNION ALL
             SELECT 5, 'DELIVERED' UNION ALL
             SELECT 6, 'CANCELLED') s
        WHERE s.i = (k % 6) + 1),
       (SELECT ROUND(SUM(i.total_line), 2)
          FROM order_items i
         WHERE i.order_id = 'sd-ord-' || printf('%03d', k)),
       0.0, 0.0,
       CASE WHEN k % 3 = 0 THEN 3.5 ELSE 0.0 END,
       (SELECT ROUND(SUM(i.total_line), 2)
          FROM order_items i
         WHERE i.order_id = 'sd-ord-' || printf('%03d', k))
       + CASE WHEN k % 3 = 0 THEN 3.5 ELSE 0.0 END,
       strftime('%Y-%m-%dT%H:%M:%fZ', 'now',
                '-' || ((42 - k) / 3) || ' days',
                '+' || ((k % 9) + 8) || ' hours'),
       strftime('%Y-%m-%dT%H:%M:%fZ', 'now',
                '-' || ((42 - k) / 3) || ' days',
                '+' || ((k % 9) + 8) || ' hours')
FROM n;

-- ---------------------------------------------------------------------------
-- 14. Payments: one per order; every 4th pays half (total_paid < total),
--     every 7th still PENDING (excluded from the report's paid sum).
-- ---------------------------------------------------------------------------
INSERT OR IGNORE INTO payments (id, order_id, payment_method_dict_id, amount,
                                payment_status, transaction_ref, created_at)
WITH RECURSIVE n(k) AS (SELECT 1 UNION ALL SELECT k + 1 FROM n WHERE k < 42)
SELECT 'sd-pay-' || printf('%03d', k),
       'sd-ord-' || printf('%03d', k),
       (SELECT m.id FROM
            (SELECT 1 AS i, 'CASH' AS id UNION ALL
             SELECT 2, 'CARD' UNION ALL
             SELECT 3, 'KHQR' UNION ALL
             SELECT 4, 'TRANSFER') m
        WHERE m.i = (k % 4) + 1),
       CASE WHEN k % 4 = 0 THEN ROUND(o.total_amount * 0.5, 2)
            ELSE o.total_amount END,
       CASE WHEN k % 7 = 0 THEN 'PENDING' ELSE 'COMPLETED' END,
       'TXN-SD-' || printf('%04d', k),
       o.created_at
FROM n
JOIN orders o ON o.id = 'sd-ord-' || printf('%03d', k);

-- ---------------------------------------------------------------------------
-- 15. Deliveries: every third order is shipped (statuses cycle).
-- ---------------------------------------------------------------------------
INSERT OR IGNORE INTO deliveries (id, order_id, driver_staff_id, delivery_status,
                                  recipient_name, recipient_phone, delivery_address,
                                  delivery_cost, dispatched_at, delivered_at, created_at)
WITH RECURSIVE n(k) AS (SELECT 3 UNION ALL SELECT k + 3 FROM n WHERE k < 42)
SELECT 'sd-del-' || printf('%03d', k),
       'sd-ord-' || printf('%03d', k),
       'sd-stf-3',
       (SELECT s.id FROM
            (SELECT 1 AS i, 'pending' AS id UNION ALL
             SELECT 2, 'in_transit' UNION ALL
             SELECT 3, 'delivered' UNION ALL
             SELECT 4, 'cancelled') s
        WHERE s.i = ((k / 3) % 4) + 1),
       (SELECT r.name FROM
            (SELECT 1 AS i, 'សុខ គឹមហុង' AS name UNION ALL
             SELECT 2, 'ចាន់ ស្រីនាថ' UNION ALL
             SELECT 3, 'វណ្ណ រតនៈ') r
        WHERE r.i = ((k / 3) % 3) + 1),
       '012' || printf('%07d', k),
       (SELECT a.name FROM
            (SELECT 1 AS i, 'ផ្លូវ២៧១ ភ្នំពេញ' AS name UNION ALL
             SELECT 2, 'ផ្លូវសម្តេចជុំ ភ្នំពេញ' UNION ALL
             SELECT 3, 'ផ្លូវ៦០៦ ភ្នំពេញ') a
        WHERE a.i = ((k / 3) % 3) + 1),
       2.0 + (k % 4),
       o.created_at,
       CASE WHEN ((k / 3) % 4) + 1 = 3
            THEN strftime('%Y-%m-%dT%H:%M:%fZ', 'now',
                          '-' || ((42 - k) / 3) || ' days',
                          '+' || ((k % 9) + 10) || ' hours')
            ELSE NULL END,
       o.created_at
FROM n
JOIN orders o ON o.id = 'sd-ord-' || printf('%03d', k);

-- ---------------------------------------------------------------------------
-- 16. Ledger: one CREDIT row per completed payment with a running balance,
--     plus a handful of EXPENSE debits.
-- ---------------------------------------------------------------------------
INSERT OR IGNORE INTO financial_ledgers (id, org_unit_id, reference_type, reference_id,
                                         account_code, entry_type, amount, balance,
                                         description, created_at)
SELECT 'sd-led-' || substr(p.id, 8),
       'sd-ou-phm', 'ORDER_PAYMENT', p.id,
       'CASH', 'CREDIT', p.amount,
       ROUND(SUM(p.amount) OVER (ORDER BY p.created_at, p.id), 2),
       'ប្រាក់ចំណូលពីកម្មង់ ' ||
           (SELECT o.order_number FROM orders o WHERE o.id = p.order_id),
       p.created_at
FROM payments p
WHERE p.payment_status = 'COMPLETED';

INSERT OR IGNORE INTO financial_ledgers (id, org_unit_id, reference_type, reference_id,
                                         account_code, entry_type, amount, balance,
                                         description, created_at)
VALUES
    ('sd-led-exp-1', 'sd-ou-phm', 'EXPENSE', NULL, 'CASH', 'DEBIT', 45.0, -45.0,
     'ថ្នែបផ្ទាល់ខ្លួន', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-12 days', '+9 hours')),
    ('sd-led-exp-2', 'sd-ou-phm', 'EXPENSE', NULL, 'CASH', 'DEBIT', 18.75, -18.75,
     'ទឹក និងភេសជ្ជៈ', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-10 days', '+11 hours')),
    ('sd-led-exp-3', 'sd-ou-phm', 'EXPENSE', NULL, 'CASH', 'DEBIT', 120.0, -120.0,
     'ថ្លែផ្គត់ផ្គង់ការិយាល័យ', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-8 days', '+15 hours')),
    ('sd-led-exp-4', 'sd-ou-phm', 'EXPENSE', NULL, 'CASH', 'DEBIT', 65.0, -65.0,
     'ថ្លែដឹកជញ្ជូន', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-6 days', '+10 hours')),
    ('sd-led-exp-5', 'sd-ou-phm', 'EXPENSE', NULL, 'CASH', 'DEBIT', 30.0, -30.0,
     'សាប៊ូ និងការសម្អាត', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-4 days', '+14 hours')),
    ('sd-led-exp-6', 'sd-ou-phm', 'EXPENSE', NULL, 'CASH', 'DEBIT', 88.2, -88.2,
     'ជួសជុលឧបករណ៍', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-2 days', '+16 hours'));

-- ---------------------------------------------------------------------------
-- 17. Sample alerts (mixed seen/sent flags)
-- ---------------------------------------------------------------------------
INSERT OR IGNORE INTO system_alerts (id, user_id, order_id, alert_type, raw_payload,
                                     is_seen, is_sent, created_at) VALUES
    ('sd-alert-1', 'sd-user-1', 'sd-ord-003', 'MAIL',
     '[{"Alert Mail":"កម្មង់ SD-ORD-003 រង់ចាំការបញ្ជាក់"}]', 1, 1,
     strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-3 days', '+9 hours')),
    ('sd-alert-2', 'sd-user-4', NULL, 'POPUP',
     '[{"Popup_seen":false,"Message":"សុំផ្លាស់ប្តូរទំនិញ"}]', 0, 0,
     strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-2 days', '+13 hours')),
    ('sd-alert-3', 'sd-user-2', 'sd-ord-015', 'ORDER_DELAY',
     '[{"Message":"យឺតជាង ២០ នាទី"}]', 1, 0,
     strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-1 days', '+10 hours')),
    ('sd-alert-4', 'sd-user-3', NULL, 'MAIL',
     '[{"Alert Mail":"ជូនដំណឹងផ្សារថ្មី"}]', 0, 1,
     strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-1 days', '+16 hours'));

-- ---------------------------------------------------------------------------
-- 18. Sample audit rows
-- ---------------------------------------------------------------------------
INSERT OR IGNORE INTO audit_logs (id, user_id, table_name, record_id, action,
                                  old_values, new_values, created_at) VALUES
    ('sd-aud-1', 'sd-user-2', 'products', 'sd-prod-001', 'UPDATE',
     '{"base_price": 45.0}', '{"base_price": 52.0}',
     strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-4 days', '+10 hours')),
    ('sd-aud-2', 'sd-user-1', 'orders', 'sd-ord-001', 'INSERT',
     NULL, '{"order_number": "SD-ORD-001", "total_amount": 96.5}',
     strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-14 days', '+8 hours')),
    ('sd-aud-3', 'sd-user-3', 'inventory_stocks', 'sd-stock-001', 'UPDATE',
     '{"quantity": 20}', '{"quantity": 3}',
     strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-2 days', '+11 hours')),
    ('sd-aud-4', 'sd-user-2', 'customers', 'sd-cus-3', 'UPDATE',
     '{"loyalty_points": 700}', '{"loyalty_points": 860}',
     strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-1 days', '+14 hours'));
