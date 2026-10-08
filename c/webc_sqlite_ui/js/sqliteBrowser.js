// Browser-side SQLite (WASI Preview 1). All SQL runs in this page; the server
// only transports file bytes (GET /db/file). WASI filesystem calls fail closed
// (ENOSYS) and the engine opens only an in-memory database, then deserializes
// the transported file into it.
(async function () {
    'use strict';
    const status = document.getElementById('sqlite-wasm-status');
    if (!status) return;
    const treeEl = document.getElementById('sqlite-db-tree');
    const gridEl = document.getElementById('sqlite-db-grid');
    const encoder = new TextEncoder();
    const decoder = new TextDecoder();
    // Change-detection token from GET /db/file; echoed back on save-back.
    let dbMeta = null;
    // True once the in-memory copy diverged from the bytes last loaded/saved.
    let dirty = false;
    // Original file-format bytes [18,19] when the loaded image declared WAL
    // (2); dump() restores them so save/export round-trips the device file's
    // journal mode even though the engine runs a rollback-journal copy.
    let walFormat = null;
    // PRAGMA get form (no '=') is reported writable by sqlite3_stmt_readonly
    // (journal_mode can mutate); such reads must not mark the DB dirty.
    function pragmaGet(sql) {
        return /^\s*PRAGMA\s+[A-Za-z_]\w*\s*(\([^)]*\))?\s*;?\s*$/.test(sql);
    }
    // infoHook re-renders the properties panel, which itself runs pragma
    // reads -> updateDirty; guard so that cycle can never recurse.
    let infoBusy = false;
    function updateDirty() {
        const el = document.getElementById('sqlite-dirty');
        if (el) el.classList.toggle('hidden', !dirty);
        if (infoHook && !infoBusy) {
            infoBusy = true;
            try {
                infoHook();  // P18: keep the properties panel current
            } finally {
                infoBusy = false;
            }
        }
    }
    // Set once the engine is up; re-renders #sqlite-db-info on every change.
    let infoHook = null;

    // Shared result renderer: columns + rows as an HTML table.
    function buildTable(columns, rows) {
        const table = document.createElement('table');
        table.className = 'data-table';
        const thead = document.createElement('thead');
        const trh = document.createElement('tr');
        for (const c of columns) {
            const th = document.createElement('th');
            th.textContent = c;
            th.className = '';
            trh.appendChild(th);
        }
        thead.appendChild(trh);
        const tbody = document.createElement('tbody');
        for (const row of rows) {
            const tr = document.createElement('tr');
            tr.className = '';
            for (const v of row) {
                const td = document.createElement('td');
                td.textContent = v === null ? 'NULL' : v;
                td.className = v === null ? 'null' : '';
                tr.appendChild(td);
            }
            tbody.appendChild(tr);
        }
        table.append(thead, tbody);
        return table;
    }

    // Table designer (P7): CREATE TABLE via the browser engine.
    function bindDesigner(api, refreshTree) {
        const nameInput = document.getElementById('sqlite-tbl-name');
        const colsBox = document.getElementById('sqlite-tbl-cols');
        const addCol = document.getElementById('sqlite-tbl-addcol');
        const createBtn = document.getElementById('sqlite-tbl-create');
        const out = document.getElementById('sqlite-tbl-status');
        if (!nameInput || !colsBox || !addCol || !createBtn) return;
        const cols = [];
        const renderCols = () => {
            colsBox.replaceChildren(...cols.map((c, i) => {
                const row = document.createElement('div');
                row.className = 'col-row';
                const mkText = (value, ph, onInput) => {
                    const inp = document.createElement('input');
                    inp.type = 'text';
                    inp.value = value;
                    inp.placeholder = ph;
                    inp.className = 'input mono w-40';
                    inp.oninput = () => onInput(inp.value);
                    return inp;
                };
                const mkChk = (label, checked, onChange) => {
                    const wrap = document.createElement('label');
                    wrap.className = 'check col-label';
                    const cb = document.createElement('input');
                    cb.type = 'checkbox';
                    cb.checked = checked;
                    cb.onchange = () => onChange(cb.checked);
                    wrap.append(cb, document.createTextNode(label));
                    return wrap;
                };
                const rm = document.createElement('button');
                rm.type = 'button';
                rm.textContent = '✕';
                rm.title = 'remove column';
                rm.className = 'x-btn';
                rm.onclick = () => { cols.splice(i, 1); renderCols(); };
                row.append(
                    mkText(c.name, 'column name', (v) => { c.name = v; }),
                    mkText(c.type, 'type', (v) => { c.type = v.trim(); }),
                    mkChk('not null', c.notnull, (v) => { c.notnull = v; }),
                    mkChk('pk', c.pk, (v) => { c.pk = v; }),
                    rm);
                return row;
            }));
        };
        addCol.onclick = () => {
            cols.push({ name: '', type: '', notnull: false, pk: false });
            renderCols();
        };
        createBtn.onclick = () => {
            try {
                api.createTable(nameInput.value.trim(), cols);
                if (out) {
                    out.className = 'msg ok';
                    out.textContent = 'Created table ' + nameInput.value.trim();
                }
                nameInput.value = '';
                cols.length = 0;
                renderCols();
                refreshTree(api.schema().tree);
                syncTargets();
            } catch (e) {
                if (out) {
                    out.className = 'msg err';
                    out.textContent = e.message;
                }
            }
        };
        const target = document.getElementById('sqlite-tbl-target');
        const alterBtn = document.getElementById('sqlite-tbl-alter');
        const syncTargets = () => {
            if (!target) return;
            const keep = target.value;
            const tables = api.schema().tree
                .filter((t) => t.kind === 'table').map((t) => t.name);
            target.replaceChildren(...tables.map((n) => {
                const o = document.createElement('option');
                o.value = n;
                o.textContent = n;
                return o;
            }));
            if (tables.includes(keep)) target.value = keep;
        };
        syncTargets();
        if (target) {
            target.onfocus = syncTargets;  // refresh options when opened
            target.onchange = () => {
                const t = api.schema().tree.find((x) =>
                    x.kind === 'table' && x.name === target.value);
                if (!t) return;
                cols.length = 0;
                for (const c of t.columns) {
                    cols.push({ name: c.name, type: c.type, notnull: c.notnull,
                                pk: !!c.pk, orig: c.name });
                }
                nameInput.value = t.name;
                renderCols();
            };
        }
        if (alterBtn) alterBtn.onclick = () => {
            try {
                const name = target ? target.value : '';
                const result = api.alterTable(name, cols, (stmts) =>
                    confirm('Apply to ' + name + '?\n\n' + stmts.join('\n')));
                if (out) {
                    if (result === null) {
                        out.className = 'msg muted';
                        out.textContent = 'cancelled';
                    } else if (!result.length) {
                        out.className = 'msg muted';
                        out.textContent = 'no changes';
                    } else {
                        out.className = 'msg ok';
                        out.textContent = 'altered ' + name + ': ' + result.join('; ');
                    }
                }
                if (result && result.length) {
                    cols.length = 0;
                    renderCols();
                    nameInput.value = '';
                    syncTargets();
                    refreshTree(api.schema().tree);
                }
            } catch (e) {
                if (out) {
                    out.className = 'msg err';
                    out.textContent = e.message;
                }
            }
        };
        renderCols();
    }

    // P14: index designer + view editor (shares the schema-status line).
    function bindIndexView(api, refreshTree) {
        const idxTable   = document.getElementById('sqlite-idx-table');
        const idxName    = document.getElementById('sqlite-idx-name');
        const idxCols    = document.getElementById('sqlite-idx-cols');
        const idxUnique  = document.getElementById('sqlite-idx-unique');
        const idxCreate  = document.getElementById('sqlite-idx-create');
        const viewTarget = document.getElementById('sqlite-view-target');
        const viewName   = document.getElementById('sqlite-view-name');
        const viewSqlBox = document.getElementById('sqlite-view-sql');
        const viewSave   = document.getElementById('sqlite-view-save');
        const trigSql    = document.getElementById('sqlite-trigger-sql');
        const trigSave   = document.getElementById('sqlite-trigger-save');
        const out        = document.getElementById('sqlite-schema-status');
        if (!idxCreate && !viewSave && !trigSave) return;
        const done = (msg, ok) => {
            if (!out) return;
            out.className = ok ? 'msg ok' : 'msg err';
            out.textContent = msg;
        };
        const tables = () => api.schema().tree
            .filter((t) => t.kind === 'table');
        const views = () => api.schema().tree
            .filter((t) => t.kind === 'view');
        const fill = (sel, names) => {
            if (!sel) return;
            const keep = sel.value;
            sel.replaceChildren(...names.map((n) => {
                const o = document.createElement('option');
                o.value = n;
                o.textContent = n;
                return o;
            }));
            if (names.includes(keep)) sel.value = keep;
        };
        const renderIdxCols = () => {
            if (!idxCols) return;
            const t = tables().find((x) => x.name === idxTable.value);
            idxCols.replaceChildren();
            for (const c of (t ? t.columns : [])) {
                const lab = document.createElement('label');
                lab.className = 'check';
                const cb = document.createElement('input');
                cb.type = 'checkbox';
                cb.value = c.name;
                cb.className = 'sqlite-idx-col';
                lab.append(cb, document.createTextNode(c.name));
                idxCols.appendChild(lab);
            }
        };
        const syncAll = () => {
            fill(idxTable, tables().map((t) => t.name));
            fill(viewTarget, views().map((t) => t.name));
            renderIdxCols();
        };
        syncAll();
        // Options and checkboxes refresh whenever a select is opened.
        if (idxTable) {
            idxTable.onfocus = syncAll;
            idxTable.onchange = renderIdxCols;
        }
        if (viewTarget) viewTarget.onfocus = syncAll;
        if (idxCreate) idxCreate.onclick = () => {
            try {
                const cols = Array.from(idxCols
                    .querySelectorAll('input.sqlite-idx-col:checked'))
                    .map((cb) => cb.value);
                api.createIndex(idxName.value.trim(), idxTable.value, cols,
                    !!(idxUnique && idxUnique.checked));
                if (idxName) idxName.value = '';
                refreshTree(api.schema().tree);
                done('index created on ' + idxTable.value, true);
            } catch (e) { done(e.message, false); }
        };
        if (viewTarget) viewTarget.onchange = () => {
            if (!viewSqlBox || !viewTarget.value) return;
            if (viewName) viewName.value = viewTarget.value;
            viewSqlBox.value = api.viewSql(viewTarget.value) || '';
        };
        if (viewSave) viewSave.onclick = () => {
            try {
                const name = viewName ? viewName.value.trim() : '';
                api.createView(name, viewSqlBox ? viewSqlBox.value : '');
                refreshTree(api.schema().tree);
                syncAll();
                done('view saved: ' + name, true);
            } catch (e) { done(e.message, false); }
        };
        if (trigSave) trigSave.onclick = () => {
            try {
                api.createTrigger(trigSql ? trigSql.value : '');
                refreshTree(api.schema().tree);
                syncAll();
                done('trigger saved (compile-checked, replaces same name)', true);
            } catch (e) { done(e.message, false); }
        };
    }

    // Import / export (P8): byte files via File API + Blob, SQL scripts via exec.
    function bindImportExport(api, refreshTree, clearGrid) {
        const exportBtn = document.getElementById('sqlite-export-db');
        const importDb = document.getElementById('sqlite-import-db');
        const importSql = document.getElementById('sqlite-import-sql');
        const out = document.getElementById('sqlite-io-status');
        if (!exportBtn || !importDb || !importSql) return;
        const done = (msg, ok) => {
            if (!out) return;
            out.className = ok ? 'msg ok' : 'msg err';
            out.textContent = msg;
        };
        const saveBtn = document.getElementById('sqlite-save-db');
        if (saveBtn) saveBtn.onclick = async () => {
            try {
                if (!dbMeta) {
                    done('no database file open', false);
                    return;
                }
                const bytes = api.dump();
                const res = await fetch('/db/save', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/octet-stream',
                        'X-DB-Meta': dbMeta,
                    },
                    body: bytes,
                });
                const info = JSON.parse(await res.text());
                if (!res.ok) {
                    let msg = 'save failed (' + res.status + '): '
                        + (info.error || 'unknown');
                    if (res.status === 409) {
                        msg += ' — use "Reload from device" to take the latest';
                    }
                    done(msg, false);
                    return;
                }
                dbMeta = info.meta;  // file replaced: refresh the token
                dirty = false;
                updateDirty();
                done('saved ' + info.size + ' bytes to device', true);
            } catch (e) {
                done(e.message, false);
            }
        };
        const reloadBtn = document.getElementById('sqlite-reload-db');
        if (reloadBtn) reloadBtn.onclick = async () => {
            try {
                const res = await fetch('/db/file', { cache: 'no-store' });
                if (!res.ok) {
                    done('no database file open', false);
                    return;
                }
                dbMeta = res.headers.get('X-DB-Meta');
                const bytes = new Uint8Array(await res.arrayBuffer());
                api.load(bytes);
                refreshTree(api.schema().tree);
                clearGrid();
                done('reloaded ' + bytes.length + ' bytes from device'
                    + (res.headers.get('X-DB-Wal')
                        ? ' (device -wal sidecar: view may be stale)' : ''),
                    true);
            } catch (e) {
                done(e.message, false);
            }
        };
        exportBtn.onclick = () => {
            try {
                const bytes = api.dump();
                const url = URL.createObjectURL(
                    new Blob([bytes], { type: 'application/octet-stream' }));
                const a = document.createElement('a');
                a.href = url;
                a.download = 'export.db';
                a.click();
                setTimeout(() => URL.revokeObjectURL(url), 1000);
                done('exported ' + bytes.length + ' bytes', true);
            } catch (e) {
                done(e.message, false);
            }
        };
        importDb.onchange = async () => {
            const file = importDb.files && importDb.files[0];
            if (!file) return;
            try {
                const bytes = new Uint8Array(await file.arrayBuffer());
                api.load(bytes);
                refreshTree(api.schema().tree);
                clearGrid();
                done('imported ' + file.name + ' (' + bytes.length + ' bytes)', true);
            } catch (e) {
                done(e.message, false);
            }
            importDb.value = '';
        };
        importSql.onchange = async () => {
            const file = importSql.files && importSql.files[0];
            if (!file) return;
            try {
                api.execAll(await file.text());
                refreshTree(api.schema().tree);
                clearGrid();
                done('ran SQL script ' + file.name, true);
            } catch (e) {
                done(e.message, false);
            }
            importSql.value = '';
        };
    }

    let memory;
    const wasi = {
        args_sizes_get(argc, size) {
            const view = new DataView(memory.buffer);
            view.setUint32(argc, 0, true);
            view.setUint32(size, 0, true);
            return 0;
        },
        args_get() { return 0; },
        environ_sizes_get(count, size) {
            const view = new DataView(memory.buffer);
            view.setUint32(count, 0, true);
            view.setUint32(size, 0, true);
            return 0;
        },
        environ_get() { return 0; },
        clock_time_get(clock, precision, result) {
            const nanos = clock === 1
                ? BigInt(Math.floor(performance.now() * 1e6))
                : BigInt(Date.now()) * 1000000n;
            new DataView(memory.buffer).setBigUint64(result, nanos, true);
            return 0;
        },
        random_get(ptr, len) {
            const bytes = new Uint8Array(memory.buffer, ptr, len);
            for (let i = 0; i < len; i += 65536) {
                crypto.getRandomValues(bytes.subarray(i, Math.min(i + 65536, len)));
            }
            return 0;
        },
        // No host filesystem is exposed. EBADF for every fd ends wasi-libc's
        // preopen scan cleanly; later path lookups then fail with ENOENT
        // instead of aborting the instance (SQLite falls back to its own
        // entropy when /dev/urandom cannot be opened).
        fd_prestat_get() { return 8; },
        fd_write(fd, iovs, count, written) {
            const view = new DataView(memory.buffer);
            let bytes = 0;
            for (let i = 0; i < count; i++) {
                const o = iovs + i * 8;
                bytes += view.getUint32(o + 4, true);
            }
            view.setUint32(written, bytes, true);
            return fd === 1 || fd === 2 ? 0 : 8; // bad descriptor
        },
        proc_exit(code) { throw new Error('WASI exit: ' + code); },
    };
    const imports = new Proxy(wasi, {
        get(target, name) { return target[name] || (() => 52); } // ENOSYS
    });

    async function boot() {
        const response = await fetch('/wasm/sqlite.wasm', { cache: 'no-store' });
        if (!response.ok) throw new Error('SQLite WASM unavailable (run ./build-wasm.sh)');
        const { instance } = await WebAssembly.instantiate(await response.arrayBuffer(), {
            wasi_snapshot_preview1: imports
        });
        const wasm = instance.exports;
        memory = wasm.memory;
        wasm._initialize();
        const view = () => new DataView(memory.buffer);

        function cstring(value) {
            const data = encoder.encode(value + '\0');
            const ptr = wasm.malloc(data.length);
            if (!ptr) throw new Error('WASM out of memory');
            new Uint8Array(memory.buffer, ptr, data.length).set(data);
            return ptr;
        }
        function read(ptr) {
            if (!ptr) return '';
            const bytes = new Uint8Array(memory.buffer);
            let end = ptr;
            while (end < bytes.length && bytes[end] !== 0) end++;
            return decoder.decode(bytes.subarray(ptr, end));
        }
        function check(rc, db, what) {
            if (rc !== 0 && rc !== 100) throw new Error(what + ': ' + read(wasm.sqlite3_errmsg(db)));
        }

        const dbSlot = wasm.malloc(4);
        const path = cstring(':memory:');
        let db = 0;
        try {
            check(wasm.sqlite3_open(path, dbSlot), 0, 'sqlite3_open');
            db = view().getUint32(dbSlot, true);
        } finally {
            wasm.free(path);
            wasm.free(dbSlot);
        }

        // Shared typed parameter binding (null → NULL, string → TEXT,
        // integer → i64, other number → REAL, boolean → 0/1).
        function bindParams(stmt, params) {
            for (let i = 0; i < params.length; i++) {
                const v = params[i];
                let rc;
                if (v === null || v === undefined) {
                    rc = wasm.sqlite3_bind_null(stmt, i + 1);
                } else if (typeof v === 'string') {
                    const data = encoder.encode(v);
                    const ptr = wasm.malloc(data.length || 1);
                    if (!ptr) throw new Error('WASM out of memory');
                    new Uint8Array(memory.buffer, ptr, data.length).set(data);
                    // -1 = SQLITE_TRANSIENT: sqlite copies at bind time.
                    rc = wasm.sqlite3_bind_text(stmt, i + 1, ptr, data.length, -1);
                    wasm.free(ptr);
                } else if (typeof v === 'number') {
                    rc = Number.isInteger(v)
                        ? wasm.sqlite3_bind_int64(stmt, i + 1, BigInt(v))
                        : wasm.sqlite3_bind_double(stmt, i + 1, v);
                } else if (typeof v === 'boolean') {
                    rc = wasm.sqlite3_bind_int64(stmt, i + 1, v ? 1n : 0n);
                } else {
                    throw new Error('unsupported parameter type: ' + typeof v);
                }
                if (rc !== 0) check(rc, db, 'bind ' + (i + 1));
            }
        }

        function queryRows(sql, params, maxRows) {
            params = params || [];
            const stmtSlot = wasm.malloc(4);
            const sqlPtr = cstring(sql);
            try {
                check(wasm.sqlite3_prepare_v2(db, sqlPtr, -1, stmtSlot, 0), db, 'prepare');
                const stmt = view().getUint32(stmtSlot, true);
                const columns = [];
                const rows = [];
                let truncated = false;
                try {
                    bindParams(stmt, params);
                    if (!wasm.sqlite3_stmt_readonly(stmt) && !pragmaGet(sql)) {
                        dirty = true;
                        updateDirty();
                    }
                    const count = wasm.sqlite3_column_count(stmt);
                    for (let i = 0; i < count; i++) {
                        columns.push(read(wasm.sqlite3_column_name(stmt, i)));
                    }
                    let rc;
                    while ((rc = wasm.sqlite3_step(stmt)) === 100) {
                        const row = [];
                        for (let i = 0; i < count; i++) {
                            row.push(read(wasm.sqlite3_column_text(stmt, i)));
                        }
                        rows.push(row);
                        if (maxRows && rows.length >= maxRows) {
                            truncated = true;
                            break;
                        }
                    }
                    if (!truncated && rc !== 101) check(rc, db, 'step'); // 101 = SQLITE_DONE
                } finally {
                    wasm.sqlite3_finalize(stmt);
                }
                return { columns, rows, truncated };
            } finally {
                wasm.free(stmtSlot);
                wasm.free(sqlPtr);
            }
        }

        const quoteIdent = (name) => '"' + name.replace(/"/g, '""') + '"';
        const quoteStr = (name) => "'" + name.replace(/'/g, "''") + "'";

        // Designer column spec → SQL fragment (validated).
        function columnDef(c) {
            if (!c.name) throw new Error('column name required');
            if (c.type && !/^[A-Za-z][A-Za-z0-9() ,]*$/.test(c.type)) {
                throw new Error('invalid column type: ' + c.type);
            }
            let s = quoteIdent(c.name) + (c.type ? ' ' + c.type : '');
            if (c.notnull) s += ' NOT NULL';
            if (c.pk) s += ' PRIMARY KEY';
            return s;
        }

        const api = {
            version() { return queryRows('SELECT sqlite_version()').rows[0][0]; },
            // Escape hatch for the SQL console (P5): full result set.
            // maxRows caps the result (P13); `truncated` reports hitting it.
            query(sql, params, maxRows) { return queryRows(sql, params, maxRows); },
            // RFC 4180-ish CSV (all fields quoted, "" escaping) for exports.
            csv(columns, rows) {
                const esc = (v) => '"' + (v === null || v === undefined
                    ? '' : String(v)).replace(/"/g, '""') + '"';
                return [columns.map(esc).join(',')]
                    .concat(rows.map((r) => r.map(esc).join(',')))
                    .join('\r\n') + '\r\n';
            },
            // Run every statement in a script (P8 SQL import); returns true.
            execAll(sql) {
                const sqlPtr = cstring(sql);
                const errPtr = wasm.malloc(4);
                view().setUint32(errPtr, 0, true);
                try {
                    const rc = wasm.sqlite3_exec(db, sqlPtr, 0, 0, errPtr);
                    if (rc !== 0) {
                        const msgPtr = view().getUint32(errPtr, true);
                        throw new Error(msgPtr ? read(msgPtr)
                            : read(wasm.sqlite3_errmsg(db)));
                    }
                    dirty = true;
                    updateDirty();
                    return true;
                } finally {
                    const msgPtr = view().getUint32(errPtr, true);
                    if (msgPtr) wasm.sqlite3_free(msgPtr); // sqlite3_exec allocates errmsg
                    wasm.free(errPtr);
                    wasm.free(sqlPtr);
                }
            },
            // Serialize the attached database to a fresh byte array (P8 export).
            dump() {
                const sizePtr = wasm.malloc(8);
                const schemaPtr = cstring('main');
                try {
                    const ptr = wasm.sqlite3_serialize(db, schemaPtr, sizePtr, 0);
                    if (!ptr) {
                        throw new Error('sqlite3_serialize failed: '
                            + read(wasm.sqlite3_errmsg(db)));
                    }
                    const len = Number(view().getBigUint64(sizePtr, true));
                    const bytes = new Uint8Array(memory.buffer, ptr, len).slice();
                    wasm.sqlite3_free(ptr);
                    // The engine holds a rollback-journal copy of a
                    // WAL-format file (see load()); put the device's format
                    // bytes back so the saved/exported file keeps its
                    // original journal mode declaration.
                    if (walFormat && bytes.length > 19) {
                        bytes[18] = walFormat[0];
                        bytes[19] = walFormat[1];
                    }
                    return bytes;
                } finally {
                    wasm.free(sizePtr);
                    wasm.free(schemaPtr);
                }
            },
            // Run a statement with bound parameters (P6 editing).
            // Returns sqlite3_changes(). JS null → SQL NULL.
            execute(sql, params) {
                params = params || [];
                const stmtSlot = wasm.malloc(4);
                const sqlPtr = cstring(sql);
                try {
                    check(wasm.sqlite3_prepare_v2(db, sqlPtr, -1, stmtSlot, 0), db, 'prepare');
                    const stmt = view().getUint32(stmtSlot, true);
                    try {
                        bindParams(stmt, params);
                        let rc;
                        while ((rc = wasm.sqlite3_step(stmt)) === 100) { /* consume */ }
                        if (rc !== 101) check(rc, db, 'step'); // 101 = SQLITE_DONE
                        dirty = true;
                        updateDirty();
                        return wasm.sqlite3_changes(db);
                    } finally {
                        wasm.sqlite3_finalize(stmt);
                    }
                } finally {
                    wasm.free(stmtSlot);
                    wasm.free(sqlPtr);
                }
            },
            // Paged, filterable, sortable table dump for the data grid (P4/P10).
            // opts: { filter, order, desc } — filter is a bound LIKE pattern
            // across every column; order must be an actual column name (else
            // ignored). Prefixed rowid column (index 0) identifies rows for
            // P6 edits; WITHOUT ROWID tables and views fall back to SELECT *.
            tableRows(name, offset, limit, opts) {
                opts = opts || {};
                const from = ' FROM ' + quoteIdent(name);
                const cols = queryRows('PRAGMA table_info(' + quoteIdent(name) + ')')
                    .rows.map((r) => r[1]);
                const params = [];
                let where = '';
                const filter = opts.filter ? String(opts.filter) : '';
                if (filter) {
                    const pattern = '%' + filter.replace(/\\/g, '\\\\')
                        .replace(/%/g, '\\%').replace(/_/g, '\\_') + '%';
                    if (cols.length) {
                        where = ' WHERE ' + cols.map((c) =>
                            quoteIdent(c) + " LIKE ? ESCAPE '\\'").join(' OR ');
                        for (let i = 0; i < cols.length; i++) params.push(pattern);
                    } else {
                        where = ' WHERE 0';  // no columns: match nothing
                    }
                }
                let order = '';
                if (opts.order && cols.includes(opts.order)) {
                    order = ' ORDER BY ' + quoteIdent(opts.order)
                        + (opts.desc ? ' DESC' : ' ASC');
                }
                const total = parseInt(
                    queryRows('SELECT count(*)' + from + where, params).rows[0][0], 10);
                const tail = where + order
                    + ' LIMIT ' + (limit | 0) + ' OFFSET ' + (offset | 0);
                let hasRowid = true;
                let q;
                try {
                    q = queryRows('SELECT rowid AS __webc_rowid, *' + from + tail, params);
                } catch (e) {
                    hasRowid = false;
                    q = queryRows('SELECT *' + from + tail, params);
                }
                return { columns: q.columns, rows: q.rows, total, hasRowid };
            },
            // Build and run CREATE TABLE from designer field specs (P7).
            // type: loose but sanitized; notnull/pk as booleans.
            createTable(name, cols) {
                if (!name) throw new Error('table name required');
                const parts = [];
                for (const c of cols) {
                    if (!c.name) continue;
                    parts.push(columnDef(c));
                }
                if (!parts.length) throw new Error('at least one column required');
                return api.execute('CREATE TABLE ' + quoteIdent(name) + ' ('
                    + parts.join(', ') + ')', []);
            },
            // P12: diff `nextCols` against the live table and apply
            // RENAME COLUMN / ADD COLUMN / DROP COLUMN statements.
            // `orig` marks a column as loaded from the table (original name);
            // entries without orig are additions; live columns with no
            // orig-marked entry are dropped. confirmFn(stmts) may veto.
            // Returns applied statements, [] when nothing to do, or null if
            // the caller cancelled.
            alterTable(name, nextCols, confirmFn) {
                const existing = queryRows('PRAGMA table_info(' + quoteIdent(name) + ')')
                    .rows.map((r) => ({
                        name: r[1], type: r[2],
                        notnull: r[3] === '1', pk: parseInt(r[5], 10) || 0,
                    }));
                if (!existing.length) throw new Error('table not found: ' + name);
                if (!nextCols.length) throw new Error('no columns');
                const byOrig = new Map();
                for (const c of nextCols) if (c.orig) byOrig.set(c.orig, c);
                for (const c of nextCols) {
                    if (c.orig && !existing.some((e) => e.name === c.orig)) {
                        throw new Error('original column vanished: ' + c.orig);
                    }
                }
                const stmts = [];
                for (const e of existing) {
                    const c = byOrig.get(e.name);
                    if (c && c.name !== e.name) {
                        stmts.push('ALTER TABLE ' + quoteIdent(name)
                            + ' RENAME COLUMN ' + quoteIdent(e.name)
                            + ' TO ' + quoteIdent(c.name));
                    }
                }
                for (const c of nextCols) {
                    if (!c.orig) {
                        stmts.push('ALTER TABLE ' + quoteIdent(name)
                            + ' ADD COLUMN ' + columnDef(c));
                    }
                }
                for (const e of existing) {
                    if (!byOrig.has(e.name)) {
                        stmts.push('ALTER TABLE ' + quoteIdent(name)
                            + ' DROP COLUMN ' + quoteIdent(e.name));
                    }
                }
                if (!stmts.length) return [];
                if (confirmFn && !confirmFn(stmts)) return null;
                for (const s of stmts) api.execute(s, []);
                return stmts;
            },
            isDirty() { return dirty; },
            // P16: explicit transactions for the console.
            begin() { return api.execute('BEGIN', []); },
            commit() { return api.execute('COMMIT', []); },
            rollback() { return api.execute('ROLLBACK', []); },
            inTransaction() { return wasm.sqlite3_get_autocommit(db) === 0; },
            // P18: properties panel data (cheap PRAGMAs + object counts).
            dbInfo() {
                const one = (sql) => String(queryRows(sql).rows[0][0]);
                const s = api.schema();
                const kinds = (k) => s.tree.filter((t) => t.kind === k).length;
                return {
                    'sqlite version': api.version(),
                    page_size: one('PRAGMA page_size'),
                    page_count: one('PRAGMA page_count'),
                    encoding: one('PRAGMA encoding'),
                    journal_mode: one('PRAGMA journal_mode'),
                    foreign_keys: one('PRAGMA foreign_keys'),
                    freelist: one('PRAGMA freelist_count'),
                    'tables/views/triggers': kinds('table') + '/'
                        + kinds('view') + '/' + kinds('trigger'),
                };
            },
            // P17: bulk delete of selected rows; rowids must parse as safe
            // integers (no injection surface beyond the placeholders).
            deleteRows(name, rowids) {
                const ids = (rowids || []).map((r) => {
                    let n;
                    if (typeof r === 'number') {
                        n = r;
                    } else if (/^-?\d+$/.test(String(r))) {
                        n = Number(r);
                    } else {
                        n = NaN;
                    }
                    if (!Number.isSafeInteger(n)) throw new Error('bad rowid: ' + r);
                    return n;
                });
                if (!ids.length) throw new Error('no rows selected');
                return api.execute('DELETE FROM ' + quoteIdent(name)
                    + ' WHERE rowid IN (' + ids.map(() => '?').join(', ') + ')',
                    ids);
            },
            // P14: index designer — columns validated against table_info so a
            // typo cannot silently build a broken CREATE INDEX.
            createIndex(name, table, cols, unique) {
                if (!name) throw new Error('index name required');
                if (!cols || !cols.length) {
                    throw new Error('index needs at least one column');
                }
                const have = queryRows('PRAGMA table_info(' + quoteIdent(table) + ')')
                    .rows.map((r) => r[1]);
                if (!have.length) throw new Error('table not found: ' + table);
                for (const c of cols) {
                    if (!have.includes(c)) throw new Error('no such column: ' + c);
                }
                return api.execute('CREATE ' + (unique ? 'UNIQUE ' : '')
                    + 'INDEX ' + quoteIdent(name) + ' ON ' + quoteIdent(table)
                    + ' (' + cols.map(quoteIdent).join(', ') + ')', []);
            },
            // P14: view save = compile-check, then drop-if-exists + create
            // (SQLite has no CREATE OR REPLACE VIEW). Body must read as
            // SELECT/WITH; wrapping it in a subquery also rejects sneaky
            // `WITH … DELETE` bodies at compile time — before the old view
            // is dropped.
            createView(name, selectSql) {
                if (!name) throw new Error('view name required');
                const body = String(selectSql || '').trim().replace(/;+\s*$/, '');
                if (!/^(SELECT|WITH)\b/i.test(body)) {
                    throw new Error('view body must be a SELECT (or WITH) statement');
                }
                queryRows('SELECT * FROM (' + body + ') LIMIT 1', [], 1);
                api.execute('DROP VIEW IF EXISTS ' + quoteIdent(name), []);
                return api.execute('CREATE VIEW ' + quoteIdent(name)
                    + ' AS ' + body, []);
            },
            // Saved CREATE statement of an existing view ('' when missing).
            viewSql(name) {
                const r = queryRows('SELECT sql FROM sqlite_master'
                    + " WHERE type = 'view' AND name = ?", [name]);
                return r.rows.length ? r.rows[0][0] : '';
            },
            // Saved CREATE statement of any schema object (P15: triggers,
            // grid detail pane).
            objectSql(name) {
                const r = queryRows('SELECT sql FROM sqlite_master'
                    + ' WHERE name = ?', [name]);
                return r.rows.length ? r.rows[0][0] : '';
            },
            // Compile a statement without running it (P15).
            checkSql(sql) {
                const stmtSlot = wasm.malloc(4);
                const sqlPtr = cstring(String(sql));
                try {
                    check(wasm.sqlite3_prepare_v2(db, sqlPtr, -1, stmtSlot, 0),
                          db, 'compile');
                    const stmt = view().getUint32(stmtSlot, true);
                    if (stmt) wasm.sqlite3_finalize(stmt);
                    return true;
                } finally {
                    wasm.free(stmtSlot);
                    wasm.free(sqlPtr);
                }
            },
            // P15: trigger editor takes the FULL CREATE TRIGGER statement.
            // Compile first, then replace any same-named trigger.
            createTrigger(sql) {
                const stmt = String(sql || '').trim().replace(/;+\s*$/, '');
                const nameRe = /^CREATE\s+(?:TEMP(?:ARY)?\s+)?TRIGGER\s+("(?:[^"]|"")+"|\[[^\]]+\]|`[^`]+`|[A-Za-z_][A-Za-z0-9_$]*)/i;
                const m = stmt.match(nameRe);
                if (!m) {
                    throw new Error(
                        'statement must start with CREATE [TEMP] TRIGGER <name>');
                }
                const raw = m[1];
                const name = /^["`\[]/.test(raw) ? raw.slice(1, -1) : raw;
                try {
                    api.checkSql(stmt);  // parse + semantic pass, no side effects
                } catch (e) {
                    // "already exists" still proves the statement parsed
                    // (name conflicts are reported at codegen, after parse).
                    // Anything else keeps the old trigger untouched.
                    if (!/already exists/i.test(e.message)) throw e;
                }
                const old = api.objectSql(name);
                api.execute('DROP TRIGGER IF EXISTS ' + quoteIdent(name), []);
                try {
                    return api.execute(stmt, []);
                } catch (e) {
                    // Semantic error surfaced only after the drop (masked by
                    // the exists check): put the previous trigger back.
                    if (old) {
                        try { api.execute(old, []); } catch (ignored) { /* best effort */ }
                    }
                    throw e;
                }
            },
            // Read the schema of the currently attached database.
            schema() {
                const tables = queryRows(
                    "SELECT name FROM sqlite_master WHERE type='table' "
                    + "AND name NOT LIKE 'sqlite_%' ORDER BY name").rows.flat();
                const tree = tables.map((name) => {
                    // cid, name, type, notnull, dflt_value, pk
                    const info = queryRows('PRAGMA table_info(' + quoteIdent(name) + ')');
                    const indexes = queryRows(
                        "SELECT name FROM sqlite_master WHERE type='index' "
                        + 'AND tbl_name=' + quoteStr(name)
                        + "AND name NOT LIKE 'sqlite_%' ORDER BY name").rows.flat();
                    return {
                        kind: 'table', name, indexes,
                        columns: info.rows.map((r) => ({
                            name: r[1], type: r[2],
                            notnull: r[3] === '1', pk: parseInt(r[5], 10) || 0,
                        })),
                    };
                });
                const views = queryRows(
                    "SELECT name FROM sqlite_master WHERE type='view' "
                    + "AND name NOT LIKE 'sqlite_%' ORDER BY name").rows.flat();
                for (const name of views) {
                    tree.push({ kind: 'view', name, indexes: [], columns: [] });
                }
                const triggers = queryRows(
                    "SELECT name FROM sqlite_master WHERE type='trigger' "
                    + "AND name NOT LIKE 'sqlite_%' ORDER BY name").rows.flat();
                for (const name of triggers) {
                    tree.push({ kind: 'trigger', name, indexes: [], columns: [] });
                }
                return { tables, tree };
            },
            // Deserialize file bytes into the connection, then read the schema.
            load(bytes) {
                // sqlite3_free() assumes sqlite3_malloc()'s 8-byte size
                // header (it calls free(p-8)); a raw malloc() buffer would
                // make close/realloc read garbage before the block and
                // corrupt the heap. Buffers sqlite owns must come from
                // sqlite's allocator — including the failure path, where
                // sqlite3_deserialize() frees pData itself.
                const ptr = wasm.sqlite3_malloc(bytes.length);
                if (!ptr) throw new Error('WASM out of memory');
                const copy = new Uint8Array(memory.buffer, ptr, bytes.length);
                copy.set(bytes);
                // File-format byte 19 = 2 (WAL) makes lockBtree() open the
                // -wal sidecar on first access; this engine has no filesystem,
                // so that fails with SQLITE_NOTADB and boot would die before
                // any handler binds — every button dead. Content pages are
                // identical in both formats: present the image as
                // rollback-journal (bytes 18/19 = 1) and let dump() restore
                // the original pair (a WAL image without sidecars is exactly
                // what a cleanly closed WAL file looks like).
                const patched = bytes.length > 19
                    && (bytes[18] === 2 || bytes[19] === 2);
                if (patched) copy[18] = copy[19] = 1;
                const schemaPtr = cstring('main');
                // FREEONCLOSE | RESIZEABLE: sqlite owns and may grow the buffer.
                const rc = wasm.sqlite3_deserialize(db, schemaPtr, ptr,
                    BigInt(bytes.length), BigInt(bytes.length), 3);
                wasm.free(schemaPtr);
                if (rc !== 0) {
                    // Ownership on failure is sqlite-defined; never free here.
                    throw new Error('sqlite3_deserialize: ' + read(wasm.sqlite3_errmsg(db)));
                }
                walFormat = patched ? [bytes[18], bytes[19]] : null;
                dirty = false;  // fresh bytes: nothing unsaved yet
                updateDirty();
                return api.schema();
            },
        };
        return api;
    }

    try {
        const api = await boot();
        let suffix = '';
        if (location.pathname === '/db') {
            const res = await fetch('/db/file', { cache: 'no-store' });
            dbMeta = res.ok ? res.headers.get('X-DB-Meta') : null;
            if (res.ok) {
                const bytes = new Uint8Array(await res.arrayBuffer());
                let { tree } = api.load(bytes);
                const tableCount = tree.filter((t) => t.kind === 'table').length;
                suffix = ', loaded ' + bytes.length + ' bytes (' + tableCount + ' tables)';
                if (res.headers.get('X-DB-Wal')) {
                    suffix += ' | device -wal sidecar: view may be stale,'
                        + ' save blocked while other writers hold it';
                }
                if (treeEl) {
                    const emptyEl = document.getElementById('db-empty');
                    const hideEmpty = () => {
                        if (emptyEl) emptyEl.classList.add('hidden');
                    };
                    const showEmpty = () => {
                        if (emptyEl) emptyEl.classList.remove('hidden');
                    };
                    const PAGE = 50;
                    const q = (n) => '"' + n.replace(/"/g, '""') + '"';
                    const val = (inp) => (inp.value === '' ? null : inp.value);
                    const mkInput = (value) => {
                        const inp = document.createElement('input');
                        inp.type = 'text';
                        inp.value = value === null || value === undefined ? '' : value;
                        inp.placeholder = 'NULL';
                        inp.className = 'cell-input';
                        return inp;
                    };
                    const mkBtn = (text, disabled, onClick) => {
                        const b = document.createElement('button');
                        b.type = 'button';
                        b.textContent = text;
                        b.disabled = disabled;
                        b.className = 'btn btn-sm';
                        b.onclick = onClick;
                        return b;
                    };
                    const fail = (message) => {
                        const d = document.createElement('div');
                        d.className = 'grid-error';
                        d.textContent = message;
                        gridEl.prepend(d);
                    };
                    const renderGrid = (name, offset, opts) => {
                        if (!gridEl) return;
                        opts = opts || { filter: '', order: '', desc: false };
                        try {
                            const { columns, rows, total, hasRowid } =
                                api.tableRows(name, offset, PAGE, opts);
                            const dataCols = hasRowid ? columns.slice(1) : columns;
                            const cellRows = hasRowid ? rows.map((r) => r.slice(1)) : rows;
                            // Edits address rows by rowid; views / WITHOUT ROWID
                            // tables fall back to read-only display.
                            const editable = hasRowid;
                            const page = Math.floor(offset / PAGE) + 1;
                            const pages = Math.max(1, Math.ceil(total / PAGE));
                            // Re-render this table keeping filter/sort state.
                            const again = (off) =>
                                renderGrid(name, off === undefined ? offset : off, opts);
                            gridEl.replaceChildren();
                            const head = document.createElement('div');
                            head.className = 'grid-toolbar';
                            const row1 = document.createElement('div');
                            row1.className = 'grid-row1';
                            const label = document.createElement('span');
                            label.textContent = name + ' — '
                                + (total ? (offset + 1) + '-' + Math.min(offset + rows.length, total) : 0)
                                + ' of ' + total + ' (page ' + page + '/' + pages + ')';
                            const nav = document.createElement('span');
                            nav.className = 'btn-row';
                            if (editable) nav.append(mkBtn('+ add row', false, () => {
                                const tr = document.createElement('tr');
                                const inputs = dataCols.map(() => mkInput(null));
                                for (const inp of inputs) {
                                    const td = document.createElement('td');
                                    td.className = '';
                                    td.appendChild(inp);
                                    tr.appendChild(td);
                                }
                                const act = document.createElement('td');
                                act.className = 'cell-act';
                                act.append(
                                    mkBtn('insert', false, () => {
                                        try {
                                            api.execute('INSERT INTO ' + q(name) + ' ('
                                                + dataCols.map(q).join(', ') + ') VALUES ('
                                                + dataCols.map(() => '?').join(', ') + ')',
                                                inputs.map(val));
                                            again();
                                        } catch (e) { fail(e.message); }
                                    }),
                                    mkBtn('cancel', false, () => again()));
                                tr.appendChild(act);
                                const tbody = gridEl.querySelector('tbody');
                                if (tbody) tbody.appendChild(tr);
                                if (inputs[0]) inputs[0].focus();
                            }));
                            nav.append(
                                // Whole filtered/sorted set as CSV (P13), capped
                                // at 100k rows so huge tables cannot hang a tab.
                                mkBtn('export csv', false, () => {
                                    try {
                                        const all = api.tableRows(name, 0,
                                            Math.min(Math.max(total, 1), 100000),
                                            opts);
                                        const cs = all.hasRowid
                                            ? all.columns.slice(1) : all.columns;
                                        const rs = all.hasRowid
                                            ? all.rows.map((r) => r.slice(1))
                                            : all.rows;
                                        const url = URL.createObjectURL(
                                            new Blob([api.csv(cs, rs)],
                                                { type: 'text/csv' }));
                                        const a = document.createElement('a');
                                        a.href = url;
                                        a.download = name + '.csv';
                                        a.click();
                                        setTimeout(() =>
                                            URL.revokeObjectURL(url), 1000);
                                    } catch (e) { fail(e.message); }
                                }),
                                mkBtn('← prev', offset === 0, () => again(Math.max(0, offset - PAGE))),
                                mkBtn('next →', offset + rows.length >= total,
                                    () => again(offset + PAGE)));
                            row1.append(label, nav);

                            // Text filter + page jump (P10).
                            const row2 = document.createElement('div');
                            row2.className = 'grid-row2';
                            const fInput = document.createElement('input');
                            fInput.type = 'text';
                            fInput.value = opts.filter;
                            fInput.placeholder = 'filter all columns…';
                            fInput.className = 'input w-56';
                            const applyFilter = () => { opts.filter = fInput.value; again(0); };
                            fInput.addEventListener('keydown', (e) => {
                                if (e.key === 'Enter') { e.preventDefault(); applyFilter(); }
                            });
                            const pInput = document.createElement('input');
                            pInput.type = 'number';
                            pInput.min = 1;
                            pInput.max = pages;
                            pInput.value = page;
                            pInput.className = 'input w-20';
                            const goPage = () => {
                                let p = parseInt(pInput.value, 10);
                                if (!Number.isFinite(p)) return;
                                p = Math.min(Math.max(p, 1), pages);
                                again((p - 1) * PAGE);
                            };
                            pInput.addEventListener('keydown', (e) => {
                                if (e.key === 'Enter') { e.preventDefault(); goPage(); }
                            });
                            // Row selection for bulk delete (P17).
                            const delSelBtn = mkBtn('delete selected (0)', true, () => {
                                const ids = Array.from(gridEl
                                    .querySelectorAll('tbody input.sqlite-sel:checked'))
                                    .map((cb) => cb.dataset.rowid);
                                try {
                                    if (!confirm('Delete ' + ids.length + ' row(s)?')) {
                                        return;
                                    }
                                    api.deleteRows(name, ids);
                                    again();
                                } catch (e) { fail(e.message); }
                            });
                            const updateSel = () => {
                                const n = gridEl
                                    .querySelectorAll('tbody input.sqlite-sel:checked').length;
                                delSelBtn.textContent = 'delete selected (' + n + ')';
                                delSelBtn.disabled = n === 0;
                            };
                            row2.append(
                                fInput,
                                mkBtn('filter', false, applyFilter),
                                mkBtn('clear', !opts.filter,
                                    () => { opts.filter = ''; again(0); }),
                                pInput,
                                mkBtn('go', false, goPage));
                            if (editable) row2.append(delSelBtn);
                            head.append(row1, row2);

                            const table = document.createElement('table');
                            table.className = 'data-table';
                            const thead = document.createElement('thead');
                            const trh = document.createElement('tr');
                            for (const c of dataCols) {
                                const th = document.createElement('th');
                                th.textContent = c + (opts.order === c
                                    ? (opts.desc ? ' ▼' : ' ▲') : '');
                                th.className = 'col-head'
                                    + (opts.order === c ? ' sorted' : '');
                                th.onclick = () => {
                                    if (opts.order === c) opts.desc = !opts.desc;
                                    else { opts.order = c; opts.desc = false; }
                                    again();
                                };
                                trh.appendChild(th);
                            }
                            if (editable) {
                                const th = document.createElement('th');
                                th.textContent = 'actions';
                                th.className = '';
                                trh.appendChild(th);
                            }
                            if (editable) {
                                const th = document.createElement('th');
                                th.className = '';
                                const allCb = document.createElement('input');
                                allCb.type = 'checkbox';
                                allCb.title = 'select all rows on this page';
                                allCb.onchange = () => {
                                    gridEl.querySelectorAll('tbody input.sqlite-sel')
                                        .forEach((cb) => { cb.checked = allCb.checked; });
                                    updateSel();
                                };
                                th.appendChild(allCb);
                                trh.insertBefore(th, trh.firstChild);
                            }
                            thead.appendChild(trh);
                            const tbody = document.createElement('tbody');
                            cellRows.forEach((cells, r) => {
                                const tr = document.createElement('tr');
                                tr.className = '';
                                if (editable) {
                                    const selTd = document.createElement('td');
                                    selTd.className = 'grid-sel';
                                    const cb = document.createElement('input');
                                    cb.type = 'checkbox';
                                    cb.className = 'sqlite-sel';
                                    cb.dataset.rowid = rows[r][0];
                                    cb.onchange = updateSel;
                                    selTd.appendChild(cb);
                                    tr.appendChild(selTd);
                                }
                                for (const v of cells) {
                                    const td = document.createElement('td');
                                    td.textContent = v === null ? 'NULL' : v;
                                    td.className = v === null ? 'null' : '';
                                    tr.appendChild(td);
                                }
                                if (editable) {
                                    const rowid = rows[r][0];
                                    const act = document.createElement('td');
                                    act.className = 'cell-act';
                                    act.append(
                                        mkBtn('edit', false, () => {
                                            tr.replaceChildren();
                                            const inputs = cells.map((v) => mkInput(v));
                                            for (const inp of inputs) {
                                                const td = document.createElement('td');
                                                td.className = '';
                                                td.appendChild(inp);
                                                tr.appendChild(td);
                                            }
                                            const rowAct = document.createElement('td');
                                            rowAct.className = 'cell-act';
                                            rowAct.append(
                                                mkBtn('save', false, () => {
                                                    try {
                                                        const sets = dataCols
                                                            .map((c) => q(c) + ' = ?').join(', ');
                                                        api.execute('UPDATE ' + q(name)
                                                            + ' SET ' + sets + ' WHERE rowid = ?',
                                                            inputs.map(val).concat([rowid]));
                                                        again();
                                                    } catch (e) { fail(e.message); }
                                                }),
                                                mkBtn('cancel', false, () => again()));
                                            tr.appendChild(rowAct);
                                            if (inputs[0]) inputs[0].focus();
                                        }),
                                        mkBtn('delete', false, () => {
                                            try {
                                                if (!confirm('Delete row ' + rowid + '?')) return;
                                                api.execute('DELETE FROM ' + q(name)
                                                    + ' WHERE rowid = ?', [rowid]);
                                                again();
                                            } catch (e) { fail(e.message); }
                                        }));
                                    tr.appendChild(act);
                                }
                                tbody.appendChild(tr);
                            });
                            table.append(thead, tbody);
                            gridEl.append(head, table);
                            gridEl.classList.remove('hidden');
                            hideEmpty();
                        } catch (e) {
                            gridEl.textContent = 'Grid error: ' + e.message;
                            gridEl.classList.remove('hidden');
                            hideEmpty();
                        }
                    };
                    const renderTree = (items) => {
                        treeEl.replaceChildren(...items.map((item) => {
                            const details = document.createElement('details');
                            details.className = 'tree-node';
                            details.addEventListener('toggle', () => {
                                if (!details.open) return;
                                if (globalThis.dbShowTab) globalThis.dbShowTab('browse');
                                if (item.kind === 'trigger') {
                                    // Not a queryable relation: show the DDL.
                                    gridEl.replaceChildren();
                                    const pre = document.createElement('pre');
                                    pre.className = 'ddl-view';
                                    pre.textContent = api.objectSql(item.name) || '';
                                    gridEl.appendChild(pre);
                                    gridEl.classList.remove('hidden');
                                    hideEmpty();
                                } else {
                                    renderGrid(item.name, 0);
                                }
                            });
                            const summary = document.createElement('summary');
                            const kindEl = document.createElement('span');
                            kindEl.className = 'tkind tkind-' + item.kind;
                            kindEl.textContent = item.kind;
                            const nameEl = document.createElement('span');
                            nameEl.className = 'tname';
                            nameEl.textContent = item.name;
                            summary.append(kindEl, nameEl);
                            const dropBtn = document.createElement('button');
                            dropBtn.type = 'button';
                            dropBtn.textContent = '✕';
                            dropBtn.title = 'drop ' + item.kind + ' ' + item.name;
                            dropBtn.className = 'x-btn';
                            dropBtn.onclick = (e) => {
                                e.stopPropagation();  // keep the <details> open
                                if (!confirm('Drop ' + item.kind + ' ' + item.name + '?')) {
                                    return;
                                }
                                try {
                                    api.execute('DROP '
                                        + (item.kind === 'view' ? 'VIEW'
                                            : item.kind === 'trigger' ? 'TRIGGER'
                                                : 'TABLE')
                                        + ' ' + q(item.name), []);
                                    renderTree(api.schema().tree);
                                    gridEl.replaceChildren();
                                    gridEl.classList.add('hidden');
                                    showEmpty();
                                } catch (err) {
                                    fail(err.message);
                                }
                            };
                            summary.appendChild(dropBtn);
                            details.appendChild(summary);
                            const ul = document.createElement('ul');
                            ul.className = 'tree-list';
                            for (const col of item.columns) {
                                const li = document.createElement('li');
                                li.className = 'tree-li';
                                li.textContent = col.name + ' ' + col.type
                                    + (col.pk ? ' [PK]' : '')
                                    + (col.notnull ? ' not null' : '');
                                ul.appendChild(li);
                            }
                            for (const idx of item.indexes) {
                                const li = document.createElement('li');
                                li.className = 'tree-li';
                                li.textContent = 'index ' + idx;
                                const idxDrop = document.createElement('button');
                                idxDrop.type = 'button';
                                idxDrop.textContent = '✕';
                                idxDrop.title = 'drop index ' + idx;
                                idxDrop.className = 'x-btn';
                                idxDrop.onclick = (e) => {
                                    e.stopPropagation();
                                    if (!confirm('Drop index ' + idx + '?')) return;
                                    try {
                                        api.execute('DROP INDEX ' + q(idx), []);
                                        renderTree(api.schema().tree);
                                    } catch (err) { fail(err.message); }
                                };
                                li.appendChild(idxDrop);
                                ul.appendChild(li);
                            }
                            details.appendChild(ul);
                            return details;
                        }));
                        treeEl.classList.remove('hidden');
                    };
                    renderTree(tree);
                    bindDesigner(api, renderTree);
                    bindIndexView(api, renderTree);
                    bindImportExport(api, renderTree, () => {
                        gridEl.replaceChildren();
                        gridEl.classList.add('hidden');
                        showEmpty();
                    });
                }
            } else {
                suffix = ', no user database file open';
            }
        }
        status.textContent = 'Browser WASM SQLite ' + api.version() + ' ready' + suffix;

        // SQL console (P5): execute in the browser, render here.
        // P13: result rows capped at 500; statements kept in localStorage
        // history (last 20) surfaced through a <select>.
        if (location.pathname === '/db') {
            const input = document.getElementById('sqlite-sql-input');
            const runBtn = document.getElementById('sqlite-sql-run');
            const out = document.getElementById('sqlite-sql-result');
            const histSel = document.getElementById('sqlite-sql-history');
            const HIST_KEY = 'webc-sql-history';
            const CONSOLE_MAX_ROWS = 500;
            const loadHist = () => {
                try {
                    const raw = globalThis.localStorage
                        && globalThis.localStorage.getItem(HIST_KEY);
                    return raw ? JSON.parse(raw) : [];
                } catch (e) { return []; }
            };
            const renderHist = () => {
                if (!histSel) return;
                const items = loadHist();
                const ph = document.createElement('option');
                ph.value = '';
                ph.textContent = 'history…';
                histSel.replaceChildren(ph);
                for (const s of items) {
                    const o = document.createElement('option');
                    o.value = s;
                    o.textContent = s.length > 60 ? s.slice(0, 57) + '…' : s;
                    histSel.appendChild(o);
                }
            };
            const pushHist = (sql) => {
                const items = loadHist().filter((s) => s !== sql);
                items.unshift(sql);
                try {
                    globalThis.localStorage
                        && globalThis.localStorage.setItem(
                            HIST_KEY, JSON.stringify(items.slice(0, 20)));
                } catch (e) { /* quota / private mode: history is optional */ }
                renderHist();
            };
            if (histSel) histSel.onchange = () => {
                if (histSel.value && input) input.value = histSel.value;
            };
            renderHist();
            // P16: transaction controls + state indicator.
            const txStatus = document.getElementById('sqlite-tx-status');
            const updateTx = () => {
                if (!txStatus) return;
                const inTx = api.inTransaction();
                txStatus.textContent = inTx
                    ? '● in transaction (uncommitted)' : 'autocommit';
                txStatus.className = inTx ? 'tx-ind in-tx' : 'tx-ind';
            };
            for (const [id, action] of [['sqlite-tx-begin', 'begin'],
                ['sqlite-tx-commit', 'commit'],
                ['sqlite-tx-rollback', 'rollback']]) {
                const btn = document.getElementById(id);
                if (btn) btn.onclick = () => {
                    try {
                        api[action]();
                        if (out) out.replaceChildren();
                    } catch (e) {
                        if (out) {
                            out.className = 'msg err';
                            out.textContent = 'Error: ' + e.message;
                        }
                    }
                    updateTx();
                };
            }
            updateTx();
            const run = () => {
                if (!input || !out) return;
                const sql = input.value.trim();
                if (!sql) return;
                pushHist(sql);
                out.replaceChildren();
                try {
                    const { columns, rows, truncated } =
                        api.query(sql, [], CONSOLE_MAX_ROWS);
                    out.className = 'msg muted';
                    if (!columns.length) {
                        out.textContent = 'OK — statement executed, no result set.';
                        return;
                    }
                    const count = document.createElement('div');
                    count.className = 'count-line';
                    count.textContent = rows.length + ' row(s)'
                        + (truncated
                            ? ' — capped at ' + CONSOLE_MAX_ROWS + ' (refine the query)'
                            : '');
                    out.append(count, buildTable(columns, rows));
                } catch (e) {
                    out.className = 'msg err';
                    out.textContent = 'Error: ' + e.message;
                }
                updateTx();
            };
            if (runBtn) runBtn.onclick = run;
            if (input) input.addEventListener('keydown', (e) => {
                if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) {
                    e.preventDefault();
                    run();
                }
            });
        }
        globalThis.webcSqlite = api;

        // Database properties panel + FK toggle + Ctrl+S (P18).
        if (location.pathname === '/db') {
            const dl = document.getElementById('sqlite-db-info');
            const fk = document.getElementById('sqlite-fk-toggle');
            const renderInfo = () => {
                if (!dl) return;
                const info = api.dbInfo();
                dl.replaceChildren();
                for (const [k, v] of Object.entries(info)) {
                    const kv = document.createElement('div');
                    const dt = document.createElement('dt');
                    dt.className = 'info-k';
                    dt.textContent = k;
                    const dd = document.createElement('dd');
                    dd.className = 'info-v';
                    dd.textContent = String(v);
                    kv.append(dt, dd);
                    dl.appendChild(kv);
                }
                if (fk) fk.checked = info.foreign_keys === '1';
            };
            infoHook = renderInfo;
            renderInfo();
            if (fk) fk.onchange = () => {
                try {
                    api.execute('PRAGMA foreign_keys = '
                        + (fk.checked ? 'ON' : 'OFF'), []);
                } catch (e) {
                    fk.checked = !fk.checked;
                }
                renderInfo();
            };
            // Ctrl/Cmd+S → Save to device (browser save dialog suppressed).
            document.addEventListener('keydown', (e) => {
                if ((e.ctrlKey || e.metaKey) && e.key === 's') {
                    const save = document.getElementById('sqlite-save-db');
                    if (save) {
                        e.preventDefault();
                        save.click();
                    }
                }
            });
        }

        // Workspace tabs: Browse / SQL / Structure / Connection / Info.
        if (location.pathname === '/db') {
            const tabsEl = document.getElementById('db-tabs');
            const panesEl = document.getElementById('db-panes');
            const showTab = (name) => {
                if (!tabsEl) return;
                tabsEl.querySelectorAll('.tab').forEach((t) =>
                    t.classList.toggle('active', t.dataset.tab === name));
                document.querySelectorAll('#db-panes .pane').forEach((p) =>
                    p.classList.toggle('active', p.dataset.pane === name));
                if (panesEl) panesEl.scrollTop = 0;
            };
            globalThis.dbShowTab = showTab;
            if (tabsEl) {
                tabsEl.querySelectorAll('.tab').forEach((t) => {
                    t.onclick = () => showTab(t.dataset.tab);
                });
            }
            const newTable = document.getElementById('db-new-table');
            if (newTable) newTable.onclick = () => {
                showTab('structure');
                const nameInput = document.getElementById('sqlite-tbl-name');
                if (nameInput) nameInput.focus();
            };
        }
    } catch (error) {
        status.textContent = 'Browser SQLite unavailable: ' + error.message;
        status.setAttribute('role', 'alert');
    }
})();
