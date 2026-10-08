// /db page: server-side directory browser modal.
// Lists GET /db/browse?dir=..., click a directory to enter it, click a file
// to put its path into #db-path-input.
(function () {
    'use strict';

    const modal = document.getElementById('db-browse-modal');
    if (!modal) return;

    const list      = document.getElementById('db-browse-list');
    const dirLabel  = document.getElementById('db-browse-dir');
    const upBtn     = document.getElementById('db-browse-up');
    const pathInput = document.getElementById('db-path-input');

    let currentDir = null; // null = server default ($HOME)

    function openModal() {
        modal.classList.remove('hidden');
        load(currentDir);
    }

    function closeModal() {
        modal.classList.add('hidden');
    }

    function row(text, sub, cls, onClick) {
        const li         = document.createElement('li');
        const btn        = document.createElement('button');
        btn.type         = 'button';
        btn.className    = 'modal-row ' + cls;
        const name       = document.createElement('span');
        name.className   = 'row-name';
        name.textContent = text;
        btn.appendChild(name);
        if (sub) {
            const extra       = document.createElement('span');
            extra.className   = 'row-sub';
            extra.textContent = sub;
            btn.appendChild(extra);
        }
        btn.addEventListener('click', onClick);
        li.appendChild(btn);
        list.appendChild(li);
    }

    function selectPath(path) {
        if (pathInput) pathInput.value = path;
        closeModal();
    }

    async function load(dir) {
        list.innerHTML       = '';
        dirLabel.textContent = 'loading…';
        let data;
        try {
            const url = dir ? '/db/browse?dir=' + encodeURIComponent(dir) : '/db/browse';
            const res = await fetch(url);
            data      = await res.json();
        } catch (e) {
            dirLabel.textContent = 'failed to list directory';
            return;
        }
        if (data.error) {
            dirLabel.textContent = data.error;
            return;
        }

        currentDir           = data.dir;
        dirLabel.textContent = data.dir;

        if (data.parent && data.parent !== data.dir) {
            row('📁 ..', 'parent directory', '',
                function () { load(data.parent); });
        }

        for (const e of data.entries) {
            if (e.dir) {
                row('📁 ' + e.name, '', '',
                    function () { load(e.path); });
            } else if (e.sqlite) {
                row(e.name, 'SQLite · ' + e.size + ' B',
                    'sqlite',
                    function () { selectPath(e.path); });
            } else {
                row(e.name, e.size + ' B', 'dim',
                    function () { selectPath(e.path); });
            }
        }

        if (!data.entries.length) {
            row('(empty directory)', '', 'dim', function () {});
        }
        if (data.overflow > 0) {
            row('+ ' + data.overflow + ' more entries not shown', '',
                'dim', function () {});
        }
    }

    document.querySelectorAll('[data-db-browse]').forEach(function (b) {
        b.addEventListener('click', openModal);
    });
    document.querySelectorAll('[data-db-browse-close]').forEach(function (b) {
        b.addEventListener('click', closeModal);
    });
    if (upBtn) {
        upBtn.addEventListener('click', function () {
            if (currentDir) load(currentDir.replace(/\/+$/, '').replace(/\/[^/]*$/, '') || '/');
        });
    }
    document.addEventListener('keydown', function (ev) {
        if (ev.key === 'Escape') closeModal();
    });
})();
