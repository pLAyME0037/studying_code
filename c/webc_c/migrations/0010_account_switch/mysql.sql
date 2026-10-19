-- ============================================================================
-- 0010_account_switch -- multi-account login + switching (browser groups).
-- Every session signed-into from the same browser shares one switch_group
-- (a uuid). The active webc_sid anchors the group, so /accounts can list the
-- sibling sessions and /switch can hop between them without a second cookie.
-- The group is stamped at login (auth_mint_session); rows minted before this
-- migration keep NULL and join a group on the next login. MySQL indexes a
-- VARCHAR, not a TEXT (BLOB/TEXT cannot be keyed without a prefix length).
-- ============================================================================

ALTER TABLE user_sessions ADD COLUMN switch_group VARCHAR(40) NULL;

CREATE INDEX idx_user_sessions_group ON user_sessions(switch_group);
