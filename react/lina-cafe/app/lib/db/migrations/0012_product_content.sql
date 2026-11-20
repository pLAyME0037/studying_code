-- ============================================================================
-- 0012_product_content -- storefront content backing the 4-section
-- /product/<id> page: product_images (pictures grouped by fixed kind +
-- caption), product_reviews (YouTube-style comment tree: parent_id replies,
-- root-only rating) and review_likes (one like per user per comment; hard
-- deleted on unlike so the UNIQUE pair stays reusable).
--   * customer_interactions is NOT touched - it stays the POS Activity
--     child (no product_id, JSON payload, live consumers);
--   * kind is a fixed enum MAIN/GALLERY/DETAIL/PACKAGE, CHECKed here and
--     validated in C (mysql has no triggers anyway, C covers all dialects);
--   * soft-delete cascades follow the 0003/0007 pattern: product ->
--     images/reviews, user -> their reviews. Those triggers live in 0003
--     (byte-pinned by the golden test) and 0007, so this file DROPs and
--     re-creates them verbatim + the new lines;
--   * seed rows: demo pictures on sd-prod-001/002/050 (static
--     resource/image files) and a review thread with likes, so all four
--     page sections render non-empty immediately. sqlite is the canonical
--     dialect; mysql.sql carries the same rows, postgres.sql is schema-only
--     (seed data is intentionally not ported there, same as 0004).
-- ============================================================================

CREATE TABLE IF NOT EXISTS product_images (
    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),
    product_id TEXT NOT NULL REFERENCES products(id) ON DELETE RESTRICT,
    kind TEXT NOT NULL CHECK (kind IN ('MAIN', 'GALLERY', 'DETAIL', 'PACKAGE')),
    name TEXT NOT NULL DEFAULT '', -- caption / alt text shown under the picture
    image TEXT NOT NULL, -- served URL (/resource/image/...)
    sort_order INTEGER NOT NULL DEFAULT 0,
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    deleted_at TEXT DEFAULT NULL
);

CREATE INDEX IF NOT EXISTS idx_product_images_product
    ON product_images(product_id, deleted_at);

CREATE TABLE IF NOT EXISTS product_reviews (
    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),
    product_id TEXT NOT NULL REFERENCES products(id) ON DELETE RESTRICT,
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE RESTRICT,
    parent_id TEXT REFERENCES product_reviews(id) ON DELETE RESTRICT, -- NULL = root
    rating INTEGER CHECK (rating >= 1 AND rating <= 5), -- NULL on replies (root only)
    body TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    deleted_at TEXT DEFAULT NULL
);

CREATE INDEX IF NOT EXISTS idx_product_reviews_product
    ON product_reviews(product_id, deleted_at, created_at);
CREATE INDEX IF NOT EXISTS idx_product_reviews_parent
    ON product_reviews(parent_id, deleted_at);

CREATE TABLE IF NOT EXISTS review_likes (
    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),
    review_id TEXT NOT NULL REFERENCES product_reviews(id) ON DELETE RESTRICT,
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE RESTRICT,
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    UNIQUE (review_id, user_id)
);

-- ---------------------------------------------------------------------------
-- Soft-delete cascade triggers: 0003/0007 bodies verbatim + new child lines
-- (those files are byte-pinned / already applied, so re-create them here).
-- ---------------------------------------------------------------------------
DROP TRIGGER IF EXISTS trg_soft_del_products;
DROP TRIGGER IF EXISTS trg_restore_products;
DROP TRIGGER IF EXISTS trg_soft_del_users;
DROP TRIGGER IF EXISTS trg_restore_users;

CREATE TRIGGER trg_soft_del_products
AFTER UPDATE OF deleted_at ON products
WHEN OLD.deleted_at IS NULL AND NEW.deleted_at IS NOT NULL
BEGIN
    UPDATE product_variants SET deleted_at = NEW.deleted_at WHERE product_id = OLD.id AND deleted_at IS NULL;
    UPDATE inventory_stocks SET deleted_at = NEW.deleted_at WHERE product_id = OLD.id AND deleted_at IS NULL;
    UPDATE product_images SET deleted_at = NEW.deleted_at WHERE product_id = OLD.id AND deleted_at IS NULL;
    UPDATE product_reviews SET deleted_at = NEW.deleted_at WHERE product_id = OLD.id AND deleted_at IS NULL;
END;

CREATE TRIGGER trg_restore_products
AFTER UPDATE OF deleted_at ON products
WHEN OLD.deleted_at IS NOT NULL AND NEW.deleted_at IS NULL
BEGIN
    UPDATE product_variants SET deleted_at = NULL WHERE product_id = OLD.id AND deleted_at = OLD.deleted_at;
    UPDATE inventory_stocks SET deleted_at = NULL WHERE product_id = OLD.id AND deleted_at = OLD.deleted_at;
    UPDATE product_images SET deleted_at = NULL WHERE product_id = OLD.id AND deleted_at = OLD.deleted_at;
    UPDATE product_reviews SET deleted_at = NULL WHERE product_id = OLD.id AND deleted_at = OLD.deleted_at;
END;

-- users triggers: 0007 bodies verbatim + product_reviews (a soft-deleted
-- account's comments go with it and come back on restore)
CREATE TRIGGER trg_soft_del_users
AFTER UPDATE OF deleted_at ON users
WHEN OLD.deleted_at IS NULL AND NEW.deleted_at IS NOT NULL
BEGIN
    UPDATE user_roles SET deleted_at = NEW.deleted_at WHERE user_id = OLD.id AND deleted_at IS NULL;
    UPDATE system_alerts SET deleted_at = NEW.deleted_at WHERE user_id = OLD.id AND deleted_at IS NULL;
    UPDATE customer_interactions SET deleted_at = NEW.deleted_at WHERE user_id = OLD.id AND deleted_at IS NULL;
    UPDATE product_reviews SET deleted_at = NEW.deleted_at WHERE user_id = OLD.id AND deleted_at IS NULL;
END;

CREATE TRIGGER trg_restore_users
AFTER UPDATE OF deleted_at ON users
WHEN OLD.deleted_at IS NOT NULL AND NEW.deleted_at IS NULL
BEGIN
    UPDATE user_roles SET deleted_at = NULL WHERE user_id = OLD.id AND deleted_at = OLD.deleted_at;
    UPDATE system_alerts SET deleted_at = NULL WHERE user_id = OLD.id AND deleted_at = OLD.deleted_at;
    UPDATE customer_interactions SET deleted_at = NULL WHERE user_id = OLD.id AND deleted_at = OLD.deleted_at;
    UPDATE product_reviews SET deleted_at = NULL WHERE user_id = OLD.id AND deleted_at = OLD.deleted_at;
END;

-- ---------------------------------------------------------------------------
-- Demo content: pictures (kind + caption), a review thread with replies and
-- likes. Timestamps sit in the past (0004/0011 rule: demo rows must not
-- crowd out rows tests create).
-- ---------------------------------------------------------------------------
INSERT OR IGNORE INTO product_images (id, product_id, kind, name, image, sort_order, created_at, updated_at) VALUES
    ('sd-pic-001', 'sd-prod-001', 'MAIN',    'រូបមុខផលិតផល',   '/resource/image/user1.png',       0, strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-80 days'), strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-80 days')),
    ('sd-pic-002', 'sd-prod-001', 'DETAIL',  'លម្អិតជិត',       '/resource/image/company_icon.png', 1, strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-80 days'), strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-80 days')),
    ('sd-pic-003', 'sd-prod-002', 'GALLERY', 'បណ្ដុំរូបភាព',     '/resource/image/user1.png',       0, strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-75 days'), strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-75 days')),
    ('sd-pic-004', 'sd-prod-050', 'MAIN',    'រូបមុខផលិតផល',   '/resource/image/know_me.png',     0, strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-70 days'), strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-70 days')),
    ('sd-pic-005', 'sd-prod-050', 'PACKAGE', 'កញ្ចប់ផលិតផល',   '/resource/image/company_icon.png', 1, strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-70 days'), strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-70 days'));

INSERT OR IGNORE INTO product_reviews (id, product_id, user_id, parent_id, rating, body, created_at, updated_at) VALUES
    ('sd-rev-1', 'sd-prod-050', 'sd-user-4', NULL,    5, 'ឆ្ងាញ់ខ្លាំងណាស់ សាកម្តងទៀតជាមិនខាន', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-60 days'), strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-60 days')),
    ('sd-rev-2', 'sd-prod-050', 'sd-user-5', 'sd-rev-1', NULL, 'យល់ស្រប! ខ្ញុំក៏ដូចគ្នា',              strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-59 days'), strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-59 days')),
    ('sd-rev-3', 'sd-prod-050', 'sd-user-5', NULL,    4, 'សេវាលឿន តម្លៃសមរម្យ',                  strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-55 days'), strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-55 days')),
    ('sd-rev-4', 'sd-prod-001', 'sd-user-6', NULL,    3, 'ប្រើបានល្អ ប៉ុន្តែចង់ឃើញពណ៌ផ្សេងទៀត', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-50 days'), strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-50 days'));

INSERT OR IGNORE INTO review_likes (id, review_id, user_id, created_at) VALUES
    ('sd-like-1', 'sd-rev-1', 'sd-user-1', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-58 days')),
    ('sd-like-2', 'sd-rev-1', 'sd-user-5', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-58 days')),
    ('sd-like-3', 'sd-rev-2', 'sd-user-1', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-57 days')),
    ('sd-like-4', 'sd-rev-3', 'sd-user-2', strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-54 days'));
