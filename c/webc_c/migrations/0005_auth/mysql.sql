-- ============================================================================
-- MYSQL/MARIADB PORT OF 0005_auth (canonical dialect: sqlite3)
-- Deltas vs the sqlite3 file, in the style of the 0001-0003 ports:
--   - TEXT ids to VARCHAR(36), inline REFERENCES clauses dropped
--   - TEXT timestamp defaults to DATETIME DEFAULT CURRENT_TIMESTAMP
-- NOTE for authors: no semicolons inside comments (driver_mysql.c splits
-- migration scripts on every semicolon)
-- ============================================================================

CREATE TABLE IF NOT EXISTS user_sessions (
    id VARCHAR(36) PRIMARY KEY DEFAULT ((UUID())),
    user_id VARCHAR(36) NOT NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at DATETIME NOT NULL
);

CREATE INDEX idx_user_sessions_user ON user_sessions(user_id);
CREATE INDEX idx_user_sessions_expires ON user_sessions(expires_at);

-- Seeded staff users live in the sqlite-only 0004 file, so this UPDATE is a
-- no-op on MySQL (0 rows). Kept for file parity - mysql_test never logs in.
UPDATE users SET password_hash = 'webc2026$21a55faf92fddf66cd1d6c1f160c8c39535fdeb5fd1a0858332fb304814ca7ea' WHERE id = 'sd-user-1';
UPDATE users SET password_hash = 'webc2026$21a55faf92fddf66cd1d6c1f160c8c39535fdeb5fd1a0858332fb304814ca7ea' WHERE id = 'sd-user-2';
