-- ============================================================================
-- 0011_seed_config -- same default config row as the sqlite file, with the
-- MySQL idempotency syntax. The value is byte-identical to the C fallback
-- literal in src/shop/shop.c and src/report/report.c, so seeding changes no
-- rendered output - it only makes pos.shop_name editable at /pos/config.
-- ============================================================================

INSERT IGNORE INTO system_configs (id, config_key, config_value,
                                   json_payload, is_encrypted, created_at,
                                   updated_at)
VALUES ('sd-cfg-shop-name', 'pos.shop_name', 'ហាង POS',
        NULL, 0,
        DATE_SUB(UTC_TIMESTAMP(), INTERVAL 300 DAY),
        DATE_SUB(UTC_TIMESTAMP(), INTERVAL 300 DAY));
