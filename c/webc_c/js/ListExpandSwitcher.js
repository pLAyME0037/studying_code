// master_child UI controller (vanilla JS, no framework).
//
// Every row/panel handler is event-DELEGATED on `document`, so HTML hot
// swapped in by the fetch submit path (#mc-root) keeps working with no
// re-initialization and no double-binding.
//
// CRUD flow (both phases):
//   1. submit -> snapshot the visible data-row-id values into sessionStorage
//      (MD_FLASH_KEY). Ids are random uuids and row order is NOT insertion
//      order, so the diff is the only exact way to find the new row later.
//   2. fetch  -> POST the form; the browser invisibly follows the server's
//      normal 302 PRG (server and no-JS behavior unchanged).
//   3. swap   -> parse the final page, replace #mc-root, restore open
//      panels/active tabs, then reveal: reopen the scope, diff the ids and
//      flash the exact new/edited row.
//   Fallback  -> network failure: native submit (reveal runs after the
//      redirect); HTTP error: inline banner, input kept, flash dropped.

const MD_FLASH_KEY = 'md-flash';

// Quote a value for use inside an attribute selector (ids are uuid/numeric,
// this is just belt and braces).
function mdSel(value) {
    return String(value).replace(/\\/g, '\\\\').replace(/"/g, '\\"');
}

function mdChildRows(scope, masterId) {
    if (!scope || !masterId) return null;
    const content = document.querySelector(
        '.tab-content[data-tab="' + mdSel(scope) + '"][data-master-id="' + mdSel(masterId) + '"]');
    // Scope to the id'd tbody: after a master-page flip a cluster may carry
    // a nested pagination store (tbody without id) whose rows must not be
    // counted here.
    return content ? Array.from(content.querySelectorAll('tbody[id] > tr[data-row-id]')) : null;
}

function mdMasterRows() {
    return Array.from(document.querySelectorAll('.master-table tbody > tr[data-row-id]'));
}

function mdSnapshot(form) {
    const d = form.dataset;
    const snap = {
        kind: d.mdKind || 'master',
        scope: d.mdScope || '',
        op: d.mdOp || '',
        master: d.mdMaster || '',
        target: d.mdTarget || '',
        masterIds: [],
        childIds: [],
    };
    mdMasterRows().forEach(tr => snap.masterIds.push(tr.dataset.rowId));
    if (snap.kind === 'child') {
        const rows = mdChildRows(snap.scope, snap.master);
        if (rows) rows.forEach(tr => snap.childIds.push(tr.dataset.rowId));
    }
    return snap;
}

function mdFlash(el) {
    try { el.scrollIntoView({ block: 'center', behavior: 'smooth' }); } catch (e) { el.scrollIntoView(); }
    el.style.transition = 'background-color 900ms ease';
    el.style.backgroundColor = 'rgba(52, 211, 153, 0.45)';
    setTimeout(() => { el.style.backgroundColor = 'rgba(52, 211, 153, 0)'; }, 500);
    setTimeout(() => { el.style.transition = ''; el.style.backgroundColor = ''; }, 1600);
}

// Consume a pending flash snapshot (runs on page load for the native
// redirect path, and after every fetch swap).
function mdReveal() {
    let raw = null;
    try { raw = sessionStorage.getItem(MD_FLASH_KEY); } catch (e) { return; }
    if (!raw) return;
    try { sessionStorage.removeItem(MD_FLASH_KEY); } catch (e) {}
    let snap;
    try { snap = JSON.parse(raw); } catch (e) { return; }

    let row = null;
    if (snap.kind === 'child') {
        if (snap.master) {
            const expand = document.querySelector(
                '.expand-btn[data-master-id="' + mdSel(snap.master) + '"]');
            if (expand && expand.getAttribute('aria-expanded') !== 'true') expand.click();
            const tabBtn = document.querySelector(
                '.detail-panel-row[data-master-id="' + mdSel(snap.master) + '"]' +
                ' .tab-btn[data-tab="' + mdSel(snap.scope) + '"]');
            if (tabBtn) tabBtn.click();
        }
        const rows = mdChildRows(snap.scope, snap.master);
        if (snap.op === 'create' && rows) {
            const before = new Set(snap.childIds || []);
            row = rows.find(tr => !before.has(tr.dataset.rowId)) || null;
        } else if (snap.op === 'update' && snap.target && rows) {
            row = rows.find(tr => tr.dataset.rowId === snap.target) || null;
        }
    } else {
        if (snap.op === 'create') {
            const before = new Set(snap.masterIds || []);
            row = mdMasterRows().find(tr => !before.has(tr.dataset.rowId)) || null;
        } else if (snap.op === 'update' && snap.target) {
            row = document.querySelector(
                '.master-table tbody > tr[data-row-id="' + mdSel(snap.target) + '"]');
        }
    }
    if (row) mdFlash(row);
}

// Swap #mc-root for freshly rendered HTML, restoring which master panels
// were open and which child tab was active, then reveal.
function mdSwap(newRoot) {
    const openIds = [];
    document.querySelectorAll('.expand-btn[aria-expanded="true"]')
        .forEach(b => openIds.push(b.dataset.masterId));
    const activeTabs = {};
    document.querySelectorAll('.detail-panel-row').forEach(r => {
        const active = r.querySelector('.tab-content:not(.hidden)');
        if (active && r.dataset.masterId) activeTabs[r.dataset.masterId] = active.dataset.tab;
    });

    const root = document.getElementById('mc-root');
    if (!root) return false;
    root.replaceWith(document.importNode(newRoot, true));

    openIds.forEach(id => {
        const btn = document.querySelector('.expand-btn[data-master-id="' + mdSel(id) + '"]');
        if (btn && btn.getAttribute('aria-expanded') !== 'true') btn.click();
    });
    Object.keys(activeTabs).forEach(id => {
        const tab = document.querySelector(
            '.detail-panel-row[data-master-id="' + mdSel(id) + '"]' +
            ' .tab-btn[data-tab="' + mdSel(activeTabs[id]) + '"]');
        if (tab) tab.click();
    });
    mdReveal();
    return true;
}

function mdBanner(form, status) {
    const div = document.createElement('div');
    div.style.cssText = 'border:1px solid #f87171;background:#fef2f2;color:#b91c1c;' +
        'padding:8px 12px;font-size:14px;margin:8px 0;display:flex;justify-content:space-between;' +
        'align-items:center;gap:12px;max-width:640px;';
    const msg = document.createElement('span');
    msg.textContent = 'Request failed' + (status ? ' (HTTP ' + status + ')' : '') +
        ' - your input was kept.';
    const close = document.createElement('button');
    close.type = 'button';
    close.textContent = 'x';
    close.style.cssText = 'border:none;background:none;color:inherit;font-weight:bold;' +
        'cursor:pointer;padding:0 4px;';
    close.addEventListener('click', () => div.remove());
    div.appendChild(msg);
    div.appendChild(close);
    if (form.parentElement) form.parentElement.insertBefore(div, form);
}

async function mdHandleSubmit(e, form) {
    if ((form.method || 'get').toUpperCase() !== 'POST') return;
    e.preventDefault();

    // 1. snapshot ids before the DOM changes
    const snap = mdSnapshot(form);
    try { sessionStorage.setItem(MD_FLASH_KEY, JSON.stringify(snap)); } catch (err) {}

    // 2. disable confirm buttons while in flight (no double POST)
    const busy = [];
    if (form.id) {
        document.querySelectorAll('button[form="' + form.id + '"]')
            .forEach(b => busy.push(b));
    }
    form.querySelectorAll('button[type="submit"]').forEach(b => {
        if (!busy.includes(b)) busy.push(b);
    });
    busy.forEach(b => { b.disabled = true; });

    try {
        // 3. POST via fetch; browser follows the server's normal 302 PRG
        let res;
        try {
            res = await fetch(form.action, { method: 'POST', body: new FormData(form) });
        } catch (err) {
            form.submit(); // native PRG; mdReveal() runs after the redirect
            return;
        }

        if (res.ok) {
            let newRoot = null;
            try {
                const doc = new DOMParser().parseFromString(await res.text(), 'text/html');
                newRoot = doc.querySelector('#mc-root');
            } catch (err) {}
            if (newRoot && mdSwap(newRoot)) {
                // content came from the redirect target: sync the address bar
                // to it (drop stale ?page state) and refresh the pagination
                // store for the swapped DOM
                try {
                    if (res.url && window.history.replaceState) {
                        history.replaceState(null, '', res.url);
                    }
                } catch (err) {}
                if (window.pgReset) window.pgReset();
                if (window.pgArm) window.pgArm();
                return;
            }
            // redirect landed outside the component: full navigation
            try { sessionStorage.removeItem(MD_FLASH_KEY); } catch (err) {}
            window.location.assign(res.url || form.action);
            return;
        }

        // HTTP error (400/413/500): keep the input, drop the pending flash
        try { sessionStorage.removeItem(MD_FLASH_KEY); } catch (err) {}
        mdBanner(form, res.status);
    } finally {
        busy.forEach(b => { b.disabled = false; });
    }
}

window.masterDetailController = {
    initialized: false,

    init() {
        if (this.initialized) return;
        this.initialized = true;

        // Expand / collapse master detail panel
        document.addEventListener('click', (e) => {
            const btn = e.target.closest && e.target.closest('.expand-btn');
            if (!btn) return;
            const id = btn.dataset.masterId;
            const panel = document.querySelector('.detail-panel-row[data-master-id="' + id + '"]');
            const open = btn.getAttribute('aria-expanded') === 'true';
            btn.setAttribute('aria-expanded', String(!open));
            btn.textContent = open ? '󰈈' : '󰈉';
            if (panel) panel.style.display = open ? 'none' : 'table-row';
        });

        // Tab switching within a panel
        document.addEventListener('click', (e) => {
            const btn = e.target.closest && e.target.closest('.tab-btn');
            if (!btn) return;
            const panel = btn.closest('.detail-panel');
            if (!panel) return;
            const tab = btn.dataset.tab;
            panel.querySelectorAll('.tab-btn').forEach(b => {
                const active = b === btn;
                b.classList.toggle('text-indigo-600', active);
                b.classList.toggle('border-b-2', active);
                b.classList.toggle('border-indigo-600', active);
            });
            panel.querySelectorAll(':scope > .tab-content').forEach(c =>
                c.classList.toggle('hidden', c.dataset.tab !== tab));
        });

        // + Add: reveal inline add-row (no navigation)
        document.addEventListener('click', (e) => {
            const btn = e.target.closest && e.target.closest('.add-child-btn');
            if (!btn) return;
            const content = btn.closest('.tab-content');
            const addRow = content && content.querySelector('.md-add-row');
            if (addRow) addRow.classList.toggle('hidden');
        });

        // Master inline edit: hide master row, show paired edit row
        document.addEventListener('click', (e) => {
            const btn = e.target.closest && e.target.closest('.master-edit-btn');
            if (!btn) return;
            const row = btn.closest('tr');
            const editRow = row && row.nextElementSibling;
            if (editRow && editRow.classList.contains('md-master-edit-row')) {
                row.classList.add('hidden');
                editRow.classList.remove('hidden');
            }
        });

        // Edit: hide display row, show its paired edit row
        document.addEventListener('click', (e) => {
            const btn = e.target.closest && e.target.closest('.child-edit-btn');
            if (!btn) return;
            const row = btn.closest('tr');
            const editRow = row && row.nextElementSibling;
            if (editRow && editRow.classList.contains('md-edit-row')) {
                row.classList.add('hidden');
                editRow.classList.remove('hidden');
            }
        });

        // Discard: reset form, hide row, unhide display row if paired
        document.addEventListener('click', (e) => {
            const btn = e.target.closest && e.target.closest('.md-discard');
            if (!btn) return;
            const tr = btn.closest('tr');
            if (!tr) return;
            const form = tr.querySelector('form');
            if (form) form.reset();
            tr.classList.add('hidden');
            const prev = tr.previousElementSibling;
            if (prev
                && !prev.classList.contains('md-add-row')
                && !prev.classList.contains('md-edit-row')
                && !prev.classList.contains('md-master-edit-row')) {
                prev.classList.remove('hidden');
            }
        });

        // ---- auto calculations (FX 1 USD = 4000 KHR) ----
        const FX = 4000;

        // Auto Days: room_day = end_date - start_date
        document.addEventListener('input', (e) => {
            if (e.target.name !== 'end_date') return;
            const scope = e.target.closest('tr');
            if (!scope) return;
            const start = scope.querySelector('[name="start_date"]');
            const days = scope.querySelector('[name="room_day"]');
            if (!start || !days || !start.value || !e.target.value) return;
            const diff = (new Date(e.target.value) - new Date(start.value)) / 86400000;
            days.value = diff > 0 ? diff : '';
        });

        // amount_in_riel <-> amount_in_dollar inside same form/row
        document.addEventListener('input', (e) => {
            const n = e.target.name;
            if (n !== 'amount_in_riel' && n !== 'amount_in_dollar') return;
            const scope = e.target.closest('tr, form');
            if (!scope) return;
            const v = parseFloat(e.target.value);
            if (isNaN(v)) return;
            const other = scope.querySelector(
                n === 'amount_in_riel' ? '[name="amount_in_dollar"]' : '[name="amount_in_riel"]');
            if (!other || other === e.target) return;
            other.value = (n === 'amount_in_riel' ? v / FX : v * FX).toFixed(2);
        });

        // amount = qty * price on detail rows
        document.addEventListener('input', (e) => {
            const n = e.target.name;
            if (n !== 'qty' && n !== 'price') return;
            const row = e.target.closest('tr');
            if (!row) return;
            const qty = parseFloat(row.querySelector('[name="qty"]')?.value);
            const price = parseFloat(row.querySelector('[name="price"]')?.value);
            const amount = row.querySelector('[name="amount"]');
            if (amount && !isNaN(qty) && !isNaN(price)) amount.value = (qty * price).toFixed(2);
        });

        // master_child CRUD forms: snapshot -> fetch -> swap -> reveal
        document.addEventListener('submit', (e) => {
            const form = e.target;
            if (!form || form.tagName !== 'FORM' || !form.dataset || !form.dataset.mdScope) return;
            mdHandleSubmit(e, form).catch(err => {
                // unexpected error after the POST: never re-post, just report
                console.error('md submit failed', err);
                try { sessionStorage.removeItem(MD_FLASH_KEY); } catch (e2) {}
                mdBanner(form, 0);
            });
        });

        // consume a pending flash from a native PRG redirect (phase 1 path)
        mdReveal();
    }
};

(() => {
    document.addEventListener('DOMContentLoaded', () => window.masterDetailController.init());
})();
