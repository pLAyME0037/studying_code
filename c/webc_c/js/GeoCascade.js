// GeoCascade: province -> district -> commune -> village picker (vanilla JS).
//
// The three ancestors are embedded once per page as inert JSON in
// <script type="application/json" data-geo-data>, shaped as
//   { "p": [[id, "", name_kh], ...],            (provinces, no parent)
//     "d": [[id, province_id, name_kh], ...],   (districts)
//     "c": [[id, district_id, name_kh], ...],   (communes)
//     "s": { village_id: commune_id, ... } }    (villages chosen on THIS page)
// so province/district/commune filtering is pure array work - zero
// round-trips. Only villages (14,858 of them, ~870 KB as one <select>)
// are fetched, and only for the single commune the user picked:
// GET /geo/villages?commune=<id> answers bare <option> lines.
//
// The three ancestor selects are UI-ONLY navigation: they carry no `name`
// attribute, so the form still posts exactly one field, village_id.
//
// Every handler is document-delegated and re-runs on init(), because
// ListExpandSwitcher's mdSwap() replaces #mc-root wholesale after a
// fetch submit.

(() => {
    'use strict';

    const LEVELS = ['province', 'district', 'commune', 'village'];
    // Which embedded array feeds each nav level, and which tuple slot of
    // that array's rows holds the parent id. 'village' is not here: it is
    // fetched, not embedded.
    const PARENT_SLOT = { d: 1, c: 1 };
    const PLACEHOLDER = {
        province: 'Select Province',
        district: 'Select District',
        commune:  'Select Commune',
        village:  'Select Village',
    };

    let DATA = null;   // parsed dataset, or null when absent/unparseable

    function readDataset() {
        const el = document.querySelector('script[data-geo-data]');
        if (!el) { DATA = null; return; }
        try { DATA = JSON.parse(el.textContent); }
        catch (e) { DATA = null; console.warn('GeoCascade: bad dataset', e); }
    }

    // Index an array of [id, parent, name] rows by their parent id, so a
    // province's districts are one Map lookup instead of a 1,712-row scan.
    // Cached per array; the dataset never changes within a page.
    let byParent = null;
    function indexRows(arrKey) {
        if (byParent && byParent[arrKey]) return byParent[arrKey];
        const map = new Map();
        const rows = (DATA && DATA[arrKey]) || [];
        const slot = PARENT_SLOT[arrKey];
        for (let i = 0; i < rows.length; ++i) {
            const p = rows[i][slot];
            let bucket = map.get(p);
            if (!bucket) { bucket = []; map.set(p, bucket); }
            bucket.push(rows[i]);
        }
        if (!byParent) byParent = {};
        byParent[arrKey] = map;
        return map;
    }

    // Rows of `arrKey` whose parent equals `parentId`. parentId === null
    // means "top level" (provinces, whose tuple slot 1 is "").
    function rowsFor(arrKey, parentId) {
        if (!DATA) return [];
        if (parentId === null || parentId === undefined || parentId === '') {
            return DATA[arrKey] || [];
        }
        return indexRows(arrKey).get(parentId) || [];
    }

    function levelEl(box, level) { return box.querySelector('[data-geo="' + level + '"]'); }

    // Replace a select's options, selecting `keep` when it is present.
    function fill(sel, rows, keep) {
        const keepValue = keep || '';
        sel.textContent = '';
        const ph = document.createElement('option');
        ph.value = '';
        ph.textContent = PLACEHOLDER[sel.dataset.geo] || '';
        sel.appendChild(ph);
        for (let i = 0; i < rows.length; ++i) {
            const o = document.createElement('option');
            o.value = rows[i][0];
            o.textContent = rows[i][2];
            if (rows[i][0] === keepValue) o.selected = true;
            sel.appendChild(o);
        }
    }

    function setEnabled(sel, on) { if (sel) sel.disabled = !on; }

    // ---- lazy village half -------------------------------------------------
    const villageCache = new Map();   // commune_id -> option HTML ('' on failure)

    function applyVillageOptions(sel, html, keep) {
        const prev = keep || '';
        sel.textContent = '';
        const ph = document.createElement('option');
        ph.value = '';
        ph.textContent = PLACEHOLDER.village;
        sel.appendChild(ph);
        if (html) {
            // The endpoint answers trusted server-rendered <option> lines.
            const tpl = document.createElement('template');
            tpl.innerHTML = html;
            const opts = tpl.content.querySelectorAll('option');
            for (let i = 0; i < opts.length; ++i) sel.appendChild(opts[i]);
        }
        // A stored village outside the fetched set must still win, or the
        // form would silently re-point the FK on submit.
        if (prev) {
            let has = false;
            for (let i = 0; i < sel.options.length; ++i) {
                if (sel.options[i].value === prev) { sel.options[i].selected = true; has = true; break; }
            }
            if (!has) {
                const o = document.createElement('option');
                o.value = prev; o.textContent = prev; o.selected = true;
                sel.appendChild(o);
            }
        }
        setEnabled(sel, true);
    }

    function loadVillages(box, communeId, keep) {
        const sel = levelEl(box, 'village');
        if (!sel) return;
        if (!communeId) { applyVillageOptions(sel, '', keep); return; }
        const cached = villageCache.get(communeId);
        if (cached !== undefined) { applyVillageOptions(sel, cached, keep); return; }
        fetch('/geo/villages?commune=' + encodeURIComponent(communeId), {
            cache: 'no-store',
            credentials: 'same-origin',
        })
            .then(r => (r.ok ? r.text() : ''))
            .then(html => {
                villageCache.set(communeId, html);
                // The user may have moved on while the request was in flight.
                if (levelEl(box, 'commune').value === communeId) {
                    applyVillageOptions(sel, html, keep);
                }
            })
            .catch(() => { villageCache.set(communeId, ''); });
    }

    // ---- cascade wiring ----------------------------------------------------
    function clearBelow(box, fromIdx) {
        for (let i = fromIdx; i < LEVELS.length; ++i) {
            const sel = levelEl(box, LEVELS[i]);
            if (!sel) continue;
            if (LEVELS[i] === 'village') { applyVillageOptions(sel, '', ''); }
            else { fill(sel, [], ''); setEnabled(sel, false); }
        }
    }

    function onAncestorChange(box, level) {
        const idx = LEVELS.indexOf(level);
        const val = levelEl(box, level).value;
        clearBelow(box, idx + 1);

        if (level === 'province') {
            const d = levelEl(box, 'district');
            fill(d, rowsFor('d', val), '');
            setEnabled(d, !!val);
        } else if (level === 'district') {
            const c = levelEl(box, 'commune');
            fill(c, rowsFor('c', val), '');
            setEnabled(c, !!val);
        } else if (level === 'commune') {
            loadVillages(box, val, '');
        }
    }

    // Walk a stored village back up: s[village] -> commune -> district ->
    // province, filling and selecting each level so the edit form shows
    // where the row already lives instead of three empty nav selects.
    function restore(box) {
        const village = levelEl(box, 'village');
        if (!village || !DATA) return;
        const stored = village.value;
        const p = levelEl(box, 'province');
        const d = levelEl(box, 'district');
        const c = levelEl(box, 'commune');

        if (!stored) {
            // CREATE form, or a row whose village_id is NULL: there is
            // nothing to walk back up from. But province has NO parent, so
            // it must be live from the very first paint - fill it and
            // enable it, and reset only the levels BELOW it (they light up
            // one at a time as each parent is chosen).
            //
            // This used to be `clearBelow(box, 0)`, which also disabled
            // province - and nothing ever re-enabled it, because the only
            // other enable path lived in the stored-village branch below.
            // Result: on a create form all three nav selects stayed
            // disabled with just their placeholder, so village_id could
            // never be chosen at all. Province has no parent to wait for.
            fill(p, rowsFor('p', null), '');
            setEnabled(p, true);
            clearBelow(box, 1);
            return;
        }

        const communeId = (DATA.s || {})[stored];
        if (!communeId) {
            // Village not resolvable from this page's map: keep the stored
            // value posted as-is, but leave the nav empty rather than guess.
            setEnabled(p, false); setEnabled(d, false); setEnabled(c, false);
            return;
        }
        const commRow = (DATA.c || []).find(r => r[0] === communeId);
        const districtId = commRow ? commRow[1] : null;
        const distRow = districtId ? (DATA.d || []).find(r => r[0] === districtId) : null;
        const provinceId = distRow ? distRow[1] : null;

        fill(p, rowsFor('p', null), provinceId);
        setEnabled(p, true);
        fill(d, rowsFor('d', provinceId), districtId);
        setEnabled(d, true);
        fill(c, rowsFor('c', districtId), communeId);
        setEnabled(c, true);

        // The leaf already renders its stored option; fetch its siblings so
        // the user can change their mind without losing the current pick.
        loadVillages(box, communeId, stored);
    }

    function initBox(box) {
        if (box.dataset.geoReady === '1') return;
        box.dataset.geoReady = '1';
        LEVELS.forEach(level => {
            const sel = levelEl(box, level);
            if (!sel || level === 'village') return;
            setEnabled(sel, false);
            sel.addEventListener('change', () => onAncestorChange(box, level));
        });
        restore(box);
    }

    function initAll() {
        readDataset();
        if (!DATA) return;
        byParent = null;
        document.querySelectorAll('[data-geo-cascade]').forEach(initBox);
    }

    window.geoCascade = { init: initAll };

    document.addEventListener('DOMContentLoaded', initAll);
    // #mc-root is replaced wholesale after a fetch submit; the new DOM has
    // fresh containers, so arm them too.
    document.addEventListener('md:swapped', initAll);
})();
