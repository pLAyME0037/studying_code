-- ============================================================================
-- 0011_seed_config -- same default config row as the sqlite file, with the
-- postgres idempotency syntax. The value is byte-identical to the C fallback
-- literal in src/shop/shop.c and src/report/report.c, so seeding changes no
-- rendered output - it only makes pos.shop_name editable at /pos/config.
-- ============================================================================

INSERT INTO system_configs (id, config_key, config_value,
                            json_payload, is_encrypted, created_at, updated_at)
VALUES ('sd-cfg-shop-name', 'pos.shop_name', 'ហាង POS',
        NULL, 0,
        now() - INTERVAL '300 days', now() - INTERVAL '300 days')
ON CONFLICT DO NOTHING;
