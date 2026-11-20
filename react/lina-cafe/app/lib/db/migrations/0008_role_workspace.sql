-- ===========================================================================
-- 0008_role_workspace -- Phase 14 (enterprise role workspaces): two extra
-- permission codes so the sidebar nav and the page gate can treat the money
-- area and the audit trail as their own regions, plus the demo staff
-- passwords so the cashier and driver workspaces are actually signable.
-- manager already holds the other eight codes and takes both new ones (the
-- seeded role_permissions keep cashier = SELL/DISCOUNT/REPORT.VIEW and
-- driver = none, which is exactly the least-privilege workspace split).
-- 0005_auth only armed sd.staff1, so sd.staff2/3 get the same demo hash
-- (salt webc2026 + posadmin1) when still blank - INSERT OR IGNORE and the
-- guarded UPDATE keep the file idempotent once applied.
-- ===========================================================================

INSERT OR IGNORE INTO permissions (id, perm_code, perm_name, module_name) VALUES
    ('sd-perm-9',  'SD.FINANCE', 'គ្រប់គ្រងហិរញ្ញវត្ថុ', 'finance'),
    ('sd-perm-10', 'SD.AUDIT',   'តាមដានកំណត់ត្រា', 'audit');

INSERT OR IGNORE INTO role_permissions (id, role_id, permission_id) VALUES
    ('sd-rp-m9',  'sd-role-manager', 'sd-perm-9'),
    ('sd-rp-m10', 'sd-role-manager', 'sd-perm-10');

UPDATE users
   SET password_hash = 'webc2026$21a55faf92fddf66cd1d6c1f160c8c39535fdeb5fd1a0858332fb304814ca7ea'
 WHERE id IN ('sd-user-2', 'sd-user-3')
   AND (password_hash IS NULL OR password_hash = '');
