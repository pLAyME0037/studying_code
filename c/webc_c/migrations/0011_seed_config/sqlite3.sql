-- ============================================================================
-- 0011_seed_config: default system_configs row (pos.shop_name).
--   * idempotent (INSERT OR IGNORE on the UNIQUE config_key);
--   * the value is byte-identical to the C fallback literal in
--     src/shop/shop.c (shop_name) and src/report/report.c
--     (report_shop_name), so seeding changes NO rendered output - it only
--     makes the row exist so /pos/config has something to edit and the
--     storefront/receipt title stops depending on a code literal;
--   * created_at sits in the PAST on purpose: /pos/config sorts
--     created_at DESC and demo pages must keep their own recent rows on
--     page 1 of the default per_page=20 list (same rule as 0004_seed_pos).
-- sqlite3 is the canonical dialect; mysql.sql/postgres.sql carry the same
-- row with their own idempotency syntax.
-- ============================================================================

INSERT OR IGNORE INTO system_configs (id, config_key, config_value,
                                      json_payload, is_encrypted, created_at,
                                      updated_at)
VALUES ('sd-cfg-shop-name', 'pos.shop_name', 'ហាង POS',
        NULL, 0,
        strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-300 days'),
        strftime('%Y-%m-%dT%H:%M:%fZ', 'now', '-300 days'));
