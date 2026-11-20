// Storefront helpers (grows in slice 2). shop_name(): the system_configs
// row seeded by migration 0011, byte-identical to the C fallback literal.
import { queryOne } from "./db";

export function shopName(): string {
  const row = queryOne<{ config_value: string | null }>(
    "SELECT config_value FROM system_configs " +
      "WHERE config_key = 'pos.shop_name' AND deleted_at IS NULL;",
  );
  const v = row?.config_value;
  return v ? v : "ហាង POS";
}
