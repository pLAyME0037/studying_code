window.masterDetailController = {
    init() {
        // Expand / collapse master detail panel
        document.querySelectorAll('.expand-btn').forEach(btn => {
            btn.addEventListener('click', (e) => {
                const id = e.currentTarget.dataset.masterId;
                const panel = document.querySelector('.detail-panel-row[data-master-id="' + id + '"]');
                const open = e.currentTarget.getAttribute('aria-expanded') === 'true';
                e.currentTarget.setAttribute('aria-expanded', String(!open));
                e.currentTarget.textContent = open ? '▼ Detail' : '▲ Detail';
                if (panel) panel.style.display = open ? 'none' : 'table-row';
            });
        });

        // Tab switching within a panel
        document.querySelectorAll('.tab-btn').forEach(btn => {
            btn.addEventListener('click', (e) => {
                const panel = e.currentTarget.closest('.detail-panel');
                if (!panel) return;
                const tab = e.currentTarget.dataset.tab;
                panel.querySelectorAll('.tab-btn').forEach(b => {
                    const active = b === e.currentTarget;
                    b.classList.toggle('text-indigo-600', active);
                    b.classList.toggle('border-b-2', active);
                    b.classList.toggle('border-indigo-600', active);
                });
                panel.querySelectorAll(':scope > .tab-content').forEach(c =>
                    c.classList.toggle('hidden', c.dataset.tab !== tab));
            });
        });

        // + Add: reveal inline add-row (no navigation)
        document.querySelectorAll('.add-child-btn').forEach(btn => {
            btn.addEventListener('click', (e) => {
                const content = e.currentTarget.closest('.tab-content');
                const addRow = content && content.querySelector('.md-add-row');
                if (addRow) addRow.classList.toggle('hidden');
            });
        });

        // Master inline edit: hide master row, show paired edit row
        document.querySelectorAll('.master-edit-btn').forEach(btn => {
            btn.addEventListener('click', (e) => {
                const row = e.currentTarget.closest('tr');
                const editRow = row.nextElementSibling;
                if (editRow && editRow.classList.contains('md-master-edit-row')) {
                    row.classList.add('hidden');
                    editRow.classList.remove('hidden');
                }
            });
        });

        // Edit: hide display row, show its paired edit row
        document.querySelectorAll('.child-edit-btn').forEach(btn => {
            btn.addEventListener('click', (e) => {
                const row = e.currentTarget.closest('tr');
                const editRow = row.nextElementSibling;
                if (editRow && editRow.classList.contains('md-edit-row')) {
                    row.classList.add('hidden');
                    editRow.classList.remove('hidden');
                }
            });
        });

        // Discard: reset form, hide row, unhide display row if paired
        document.querySelectorAll('.md-discard').forEach(btn => {
            btn.addEventListener('click', (e) => {
                const tr = e.currentTarget.closest('tr');
                const form = tr.querySelector('form');
                if (form) form.reset();
                tr.classList.add('hidden');
                const prev = tr.previousElementSibling;
                if (prev && !prev.classList.contains('md-add-row') && !prev.classList.contains('md-edit-row') && !prev.classList.contains('md-master-edit-row')) prev.classList.remove('hidden');
            });
        });

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

        // ---- auto calculations (FX 1 USD = 4000 KHR) ----
        const FX = 4000;

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
    }
};

(() => {
    document.addEventListener('DOMContentLoaded', () => window.masterDetailController.init());
})();