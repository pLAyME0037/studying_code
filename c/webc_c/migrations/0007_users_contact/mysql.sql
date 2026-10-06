-- ============================================================================
-- 0007_users_contact -- MySQL port of the contact-rule change (Phase 13):
-- phone becomes NOT NULL (backfilled from the unique username first) and
-- email becomes nullable - UNIQUE still allows any number of NULL emails.
-- MODIFY keeps the existing UNIQUE indexes on both columns.
-- ============================================================================
UPDATE users SET phone = username WHERE phone IS NULL;
ALTER TABLE users
    MODIFY email VARCHAR(191) NULL,
    MODIFY phone VARCHAR(64) NOT NULL;
