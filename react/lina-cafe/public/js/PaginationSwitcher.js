// PaginationSwitcher.js — on-demand single-page fetching with a small 3-entry
// LRU cache and plain-<a> SSR fallback (works without JS).
//
// Contract with display/component/pagination.h.tt:
//   nav[data-pg-container|data-pg-key|data-pg-per-key|data-pg-base|data-pg-per]
//     page links:     <a data-pg-n="N" href="..."> -> fetched via ?fragment=1
//     per-page links: no data-pg-n                 -> full SSR reload
//   fragment response (?fragment=1):
//     contains <tbody id="<container>"> and <nav data-pg-container="<container>">
//     for only the requested SQL page (no ?fragment=all table dump).
(function () {
    var CACHE_MAX = 3;
    var cache = new Map();
    var gen = 0;

    function sel(v) {
        return String(v).replace(/\\/g, '\\\\').replace(/"/g, '\\"');
    }

    function navFor(container) {
        return document.querySelector('nav[data-pg-container="' + sel(container) + '"]');
    }

    function cacheGet(key) {
        if (!cache.has(key)) {
            return null;
        }
        var entry = cache.get(key);
        cache.delete(key);
        cache.set(key, entry);
        return entry;
    }

    function cacheSet(key, entry) {
        if (cache.has(key)) {
            cache.delete(key);
        } else if (cache.size >= CACHE_MAX) {
            var oldest = cache.keys().next().value;
            cache.delete(oldest);
        }
        cache.set(key, entry);
    }

    function buildFragmentUrl(href) {
        var u = new URL(href, location.origin);
        u.searchParams.set('fragment', '1');
        return u.toString();
    }

    function captureState() {
        var open = [];
        document.querySelectorAll('.expand-btn[aria-expanded="true"]')
            .forEach(function (b) {
                open.push(b.dataset.masterId);
            });
        var tabs = {};
        document.querySelectorAll('#mc-tbody tr.detail-panel-row[data-master-id]')
            .forEach(function (r) {
                var active = r.querySelector('.tab-content:not(.hidden)');
                if (active) {
                    tabs[r.dataset.masterId] = active.dataset.tab;
                }
            });
        return { open: open, tabs: tabs };
    }

    function restoreState(state) {
        state.open.forEach(function (id) {
            var btn = document.querySelector('.expand-btn[data-master-id="' + sel(id) + '"]');
            if (btn && btn.getAttribute('aria-expanded') !== 'true') {
                btn.click();
            }
        });
        Object.keys(state.tabs).forEach(function (id) {
            var tab = document.querySelector(
                '#mc-tbody tr.detail-panel-row[data-master-id="' + sel(id) + '"]' +
                ' .tab-btn[data-tab="' + sel(state.tabs[id]) + '"]');
            if (tab) {
                tab.click();
            }
        });
    }

    function snapshotCurrent(container) {
        var live = document.getElementById(container);
        var nav = navFor(container);
        if (!live || !nav) {
            return;
        }
        var currentUrl = new URL(location.href, location.origin).toString();
        if (!cache.has(currentUrl)) {
            cacheSet(currentUrl, {
                container: container,
                tbodyHtml: live.innerHTML,
                navHtml: nav.outerHTML
            });
        }
    }

    function applyEntry(container, entry) {
        var live = document.getElementById(container);
        var nav = navFor(container);
        if (!live || !nav) {
            return false;
        }
        var isMaster = container === 'mc-tbody';
        var state = null;
        if (isMaster) {
            state = captureState();
        }

        live.innerHTML = entry.tbodyHtml;
        var tpl = document.createElement('template');
        tpl.innerHTML = entry.navHtml;
        var newNav = tpl.content.querySelector('nav[data-pg-container]');
        if (newNav) {
            nav.replaceWith(newNav);
        }

        if (isMaster && state) {
            restoreState(state);
        }
        document.dispatchEvent(new CustomEvent('md:swapped'));
        return true;
    }

    // Hooks called by ListExpandSwitcher.js after #mc-root mutations.
    window.pgReset = function () {
        gen += 1;
        cache.clear();
    };
    window.pgArm = function () {};

    document.addEventListener('click', function (e) {
        if (e.defaultPrevented || e.button !== 0) {
            return;
        }
        if (e.metaKey || e.ctrlKey || e.shiftKey || e.altKey) {
            return;
        }
        var a = null;
        if (e.target && e.target.closest) {
            a = e.target.closest('nav[data-pg-container] a[data-pg-n]');
        }
        if (!a) {
            return;
        }
        var nav = a.closest('nav[data-pg-container]');
        if (!nav) {
            return;
        }
        var container = nav.dataset.pgContainer;
        if (!container) {
            return;
        }

        e.preventDefault();
        var targetUrl = new URL(a.href, location.origin).toString();
        snapshotCurrent(container);

        var cached = cacheGet(targetUrl);
        if (cached) {
            if (applyEntry(container, cached)) {
                try {
                    history.pushState(null, '', targetUrl);
                } catch (err) {}
            } else {
                location.assign(targetUrl);
            }
            return;
        }

        var myGen = gen;
        fetch(buildFragmentUrl(targetUrl), {
            credentials: 'same-origin',
            cache: 'no-store'
        })
            .then(function (res) {
                if (!res.ok) {
                    return '';
                }
                return res.text();
            })
            .then(function (html) {
                if (myGen !== gen) {
                    return;
                }
                if (!html) {
                    location.assign(targetUrl);
                    return;
                }
                var doc = new DOMParser().parseFromString(html, 'text/html');
                var newBody = doc.getElementById(container);
                var newNav = doc.querySelector('nav[data-pg-container="' + sel(container) + '"]');
                if (!newBody || !newNav) {
                    location.assign(targetUrl);
                    return;
                }
                var entry = {
                    container: container,
                    tbodyHtml: newBody.innerHTML,
                    navHtml: newNav.outerHTML
                };
                cacheSet(targetUrl, entry);
                if (applyEntry(container, entry)) {
                    try {
                        history.pushState(null, '', targetUrl);
                    } catch (err) {}
                } else {
                    location.assign(targetUrl);
                }
            })
            .catch(function () {
                if (myGen === gen) {
                    location.assign(targetUrl);
                }
            });
    });

    window.addEventListener('popstate', function () {
        var currentUrl = new URL(location.href, location.origin).toString();
        var cached = cacheGet(currentUrl);
        if (cached) {
            if (!applyEntry(cached.container, cached)) {
                location.reload();
            }
            return;
        }
        location.reload();
    });
})();
