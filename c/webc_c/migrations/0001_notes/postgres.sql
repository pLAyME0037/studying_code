-- no REFERENCES clause here: the users table does not exist yet
CREATE TABLE IF NOT EXISTS notes (
    id TEXT PRIMARY KEY DEFAULT (gen_random_uuid()::text),
    user_id TEXT NULL,
    title TEXT NOT NULL,
    body TEXT,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);
