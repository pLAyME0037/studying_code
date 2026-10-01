// PaginationSwitcher.js — client-side paging over the server's
// ?fragment=all store, with plain-<a> SSR fallback (works without JS).
//
// Contract with display/component/pagination.h.tt:
//   nav[data-pg-container|data-pg-key|data-pg-base|data-pg-per]
//     page links:    <a data-pg-n="N" href="...">  -> served from the store
//     per-page links: no data-pg-n                 -> always a full SSR reload
//   fragment payload (GET base + search + "&fragment=all"):
//     <div id="pg-store" hidden>
//       <table data-pg-rows="<container>" data-pg-unit="<elements per record>">
//     <template data-pg-pager="<container>" data-pg-n="<page>"><nav>...</nav>
//
// A "record" is one logical row as the template emits it (data-pg-unit):
//   /users, /notes  : 1 (the <tr>)
//   master clusters : 3 (display row, edit row, detail panel)
//   child rows      : 3 (row, hidden edit row, the standalone form)
(function () {
    function sel(v) {
        return String(v).replace(/\\/g, '\\\\').replace(/"/g, '\\"');
    }

    // Hidden holder div holding the fetched fragment. `gen` guards against a
    // fetch that was still in flight when pgReset() dropped the store.
    var store = null;
    var arming = false;
    var gen = 0;

    function storeTable(container) {
        return store
            ? store.querySelector('table[data-pg-rows="' + sel(container) + '"]')
            : null;
    }

    function storePager(container, n) {
        return store
            ? store.querySelector(
                'template[data-pg-pager="' + sel(container) + '"][data-pg-n="' + n + '"]')
            : null;
    }

    function navFor(container) {
        return document.querySelector('nav[data-pg-container="' + sel(container) + '"]');
    }

    // Which master panels are open / which child tab is active, so a flip
    // can put them back afterwards (mirrors mdSwap in ListExpandSwitcher).
    function captureState() {
        var open = [];
        document.querySelectorAll('.expand-btn[aria-expanded="true"]')
            .forEach(function (b) { open.push(b.dataset.masterId); });
        var tabs = {};
        document.querySelectorAll('#mc-tbody tr.detail-panel-row[data-master-id]')
            .forEach(function (r) {
                var active = r.querySelector('.tab-content:not(.hidden)');
                if (active) tabs[r.dataset.masterId] = active.dataset.tab;
            });
        return { open: open, tabs: tabs };
    }

    function restoreState(state) {
        state.open.forEach(function (id) {
            var btn = document.querySelector('.expand-btn[data-master-id="' + sel(id) + '"]');
            if (btn && btn.getAttribute('aria-expanded') !== 'true') btn.click();
        });
        Object.keys(state.tabs).forEach(function (id) {
            var tab = document.querySelector(
                '#mc-tbody tr.detail-panel-row[data-master-id="' + sel(id) + '"]' +
                ' .tab-btn[data-tab="' + sel(state.tabs[id]) + '"]');
            if (tab) tab.click();
        });
    }

    // Mirror the live child tbody + nav into the holder's stored cluster, so
    // a later master flip does not revert a child-page change.
    function patchCluster(container) {
        var live = document.getElementById(container);
        if (!live || !store) return;
        var content = live.closest('.tab-content');
        if (!content || !content.dataset.masterId) return;
        var cluster = store.querySelector(
            'tr.detail-panel-row[data-master-id="' + sel(content.dataset.masterId) + '"]');
        if (!cluster) return;
        var storedBody = cluster.querySelector('tbody[id="' + sel(container) + '"]');
        if (storedBody) storedBody.innerHTML = live.innerHTML;
        var liveNav = navFor(container);
        var storedNav = cluster.querySelector('nav[data-pg-container="' + sel(container) + '"]');
        if (liveNav && storedNav) storedNav.replaceWith(liveNav.cloneNode(true));
    }

    function applyPage(container, n, perPage) {
        var table = storeTable(container);
        var live = document.getElementById(container);
        var nav = navFor(container);
        if (!table || !live || !nav) return false;

        var unit = parseInt(table.dataset.pgUnit, 10) || 1;
        var groups = Array.from(table.tBodies[0].children);
        var maxPage = Math.max(1, Math.ceil(groups.length / (unit * perPage)));
        if (!(n >= 1)) n = 1;
        if (n > maxPage) n = maxPage;

        var from = (n - 1) * perPage * unit;
        var slice = groups.slice(from, from + perPage * unit);

        // the hidden inline add-row (child tables) always stays put
        var addRow = live.querySelector(':scope > .md-add-row');
        live.innerHTML = '';
        if (addRow) live.appendChild(addRow);
        slice.forEach(function (el) {
            live.insertAdjacentHTML('beforeend', el.outerHTML);
        });

        // clusters pulled from the master store embed a nested copy of their
        // child stores; the holder keeps the originals, so drop the live
        // duplicates (they would repeat form ids)
        live.querySelectorAll('[data-pg-rows], template[data-pg-pager]')
            .forEach(function (el) { el.remove(); });

        var pager = storePager(container, n);
        if (pager) nav.replaceWith(document.importNode(pager.content, true));

        patchCluster(container);
        return true;
    }

    function flip(container, n, perPage) {
        var state = captureState();
        if (!applyPage(container, n, perPage)) return false;
        restoreState(state);
        return true;
    }

    // Fetch every row + a server-rendered pager per page for this list.
    // Armed on load and again after ListExpandSwitcher's mdSwap replaced the
    // DOM; a single-page list (no nav) never fetches.
    function arm() {
        if (store || arming) return;
        var nav = document.querySelector('nav[data-pg-container]');
        // no page links -> single-page list, nothing to page through
        if (!nav || !document.querySelector('nav[data-pg-container] a[data-pg-n]')) {
            return;
        }
        var myGen = gen;
        var search = location.search || '';
        var url = nav.dataset.pgBase + search + (search ? '&' : '?') + 'fragment=all';
        arming = true;
        fetch(url, { credentials: 'same-origin' })
            .then(function (res) { return res.ok ? res.text() : ''; })
            .then(function (html) {
                if (myGen !== gen) return;   // superseded by pgReset()
                arming = false;
                if (store) return;
                if (html.indexOf('id="pg-store"') === -1) return;
                var holder = document.createElement('div');
                holder.hidden = true;
                holder.innerHTML = html;
                if (holder.querySelector('#pg-store')) store = holder;
            })
            .catch(function () {
                if (myGen === gen) arming = false;
            });
    }

    // Hooks for ListExpandSwitcher's mdSwap: content was replaced by an SSR
    // page, so drop the stale store and prefetch the fresh one.
    window.pgReset = function () { gen += 1; store = null; arming = false; };
    window.pgArm = arm;

    // Page links: serve from the store; without a store (or when anything
    // goes wrong) let the plain href do a normal SSR navigation.
    document.addEventListener('click', function (e) {
        if (e.defaultPrevented || e.button !== 0) return;
        if (e.metaKey || e.ctrlKey || e.shiftKey || e.altKey) return;
        var a = e.target && e.target.closest
            ? e.target.closest('nav[data-pg-container] a[data-pg-n]')
            : null;
        if (!a || !store) return;
        e.preventDefault();
        var nav = a.closest('nav[data-pg-container]');
        var n = parseInt(a.dataset.pgN, 10);
        var per = parseInt(nav.dataset.pgPer, 10) || 20;
        if (flip(nav.dataset.pgContainer, n, per)) {
            try { history.pushState(null, '', a.href); } catch (err) {}
        } else {
            location.assign(a.href);
        }
    });

    // Back/forward: re-apply each list's page from the URL (per_page never
    // changes inside a document - per-page links are full SSR reloads).
    window.addEventListener('popstate', function () {
        if (!store) return;
        var params = new URLSearchParams(location.search);
        document.querySelectorAll('nav[data-pg-container]').forEach(function (nav) {
            var n = parseInt(params.get(nav.dataset.pgKey), 10) || 1;
            var per = parseInt(nav.dataset.pgPer, 10) || 20;
            flip(nav.dataset.pgContainer, n, per);
        });
    });

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', arm);
    } else {
        arm();
    }
})();
