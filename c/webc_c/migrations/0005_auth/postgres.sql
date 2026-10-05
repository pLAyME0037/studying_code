-- ============================================================================
-- POSTGRES PORT OF 0005_auth (canonical dialect: sqlite3)
-- Types translated (TIMESTAMP, gen_random_uuid), inline REFERENCES kept.
-- The postgres driver exec is currently a stub - this file is a
-- specification, not yet exercised by the test suite.
-- ============================================================================

CREATE TABLE IF NOT EXISTS user_sessions (
    id TEXT PRIMARY KEY DEFAULT ((gen_random_uuid()::text)),
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at TIMESTAMP NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_user_sessions_user ON user_sessions(user_id);
CREATE INDEX IF NOT EXISTS idx_user_sessions_expires ON user_sessions(expires_at);

-- Seeded staff users live in the sqlite-only 0004 file, so this UPDATE is a
-- no-op until seed data is ported.
UPDATE users SET password_hash = 'webc2026$21a55faf92fddf66cd1d6c1f160c8c39535fdeb5fd1a0858332fb304814ca7ea' WHERE id = 'sd-user-1';
