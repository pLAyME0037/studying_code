-- ============================================================================
-- 0005_auth -- staff login sessions (Phase 11: storefront + gated /pos).
-- /pos, /dashboard and /reports require a signed-in ADMIN user (session
-- cookie webc_sid, checked by auth_gate() in core/http/route.c). The
-- storefront, cart and guest checkout stay public - cart state lives in a
-- stateless webc_cart cookie, so only staff sessions need a table.
-- Passwords are salted sha256 stored as "<salt>$<hex>" (core/auth/auth.c).
-- NOTE: no semicolons inside comments (the runner splits statements on the
-- semicolon character).
-- ============================================================================

CREATE TABLE IF NOT EXISTS user_sessions (
    id TEXT PRIMARY KEY DEFAULT (lower(hex(randomblob(4))) || '-' || lower(hex(randomblob(2))) || '-4' || substr(lower(hex(randomblob(2))),2) || '-' || substr('89ab',abs(random()) % 4 + 1, 1) || substr(lower(hex(randomblob(2))),2) || '-' || lower(hex(randomblob(6)))),
    user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    expires_at TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_user_sessions_user ON user_sessions(user_id);
CREATE INDEX IF NOT EXISTS idx_user_sessions_expires ON user_sessions(expires_at);

-- Demo staff login: username 'sd.staff1', password 'posadmin1'
-- (hash = sha256('webc2026' + 'posadmin1') with salt prefix 'webc2026').
-- Idempotent: re-applying rewrites the same value.
UPDATE users SET password_hash = 'webc2026$21a55faf92fddf66cd1d6c1f160c8c39535fdeb5fd1a0858332fb304814ca7ea' WHERE id = 'sd-user-1';

-- Phase 11 list-rank fix: the three newest seed orders picked up a POSITIVE
-- hour offset (+12..14h) in 0004_seed_pos and sat in the future, outranking
-- freshly created rows in the created_at DESC master list. Pin them just
-- behind "now" so a web order lands first. sqlite-only (0004 seed rows never
-- exist on MySQL/Postgres - that file is skipped there).
UPDATE orders
SET created_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now',
                          CASE id WHEN 'sd-ord-040' THEN '-3 hours'
                                  WHEN 'sd-ord-041' THEN '-2 hours'
                                  ELSE '-1 hour' END),
    updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now',
                          CASE id WHEN 'sd-ord-040' THEN '-3 hours'
                                  WHEN 'sd-ord-041' THEN '-2 hours'
                                  ELSE '-1 hour' END)
WHERE id IN ('sd-ord-040', 'sd-ord-041', 'sd-ord-042');
