// Test browser WASI loader without browser dependencies or real database files.
import assert from 'node:assert/strict';
import { webcrypto } from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import vm from 'node:vm';
import { DatabaseSync } from 'node:sqlite';

const bytes = fs.readFileSync(new URL('../build/sqlite.wasm', import.meta.url));
const module = new WebAssembly.Module(bytes);
assert.ok(WebAssembly.Module.imports(module).every((item) =>
    item.module === 'wasi_snapshot_preview1'));

const status = { textContent: '', setAttribute() {} };
const script = fs.readFileSync(new URL('../js/sqliteBrowser.js', import.meta.url), 'utf8')
    .replace(/\}\)\(\);\s*$/, '})()');
const context = {
    document: { getElementById: (id) => (id === 'sqlite-wasm-status' ? status : null) },
    fetch: async () => ({ ok: true, arrayBuffer: async () => bytes }),
    location: { pathname: '/' },
    crypto: webcrypto, performance, TextDecoder, TextEncoder, DataView,
    Uint8Array, WebAssembly, BigInt, Date, Error,
};
const run = vm.runInNewContext(script, context);
await run;
assert.match(status.textContent, /^Browser WASM SQLite \d+\.\d+\.\d+ ready$/);
console.log(status.textContent);

// Deserialization path (P2): load a real file's bytes, list its tables.
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'wasm-fixture-'));
try {
    const dbPath = path.join(tmp, 'fixture.db');
    const db = new DatabaseSync(dbPath);
    db.exec("CREATE TABLE people(id INTEGER PRIMARY KEY, name TEXT NOT NULL);"
        + "INSERT INTO people(name) VALUES ('alice');");
    db.exec("CREATE INDEX people_name ON people(name);");
    db.exec("CREATE VIEW adults AS SELECT * FROM people;");
    db.close();
    const loaded = context.webcSqlite.load(new Uint8Array(fs.readFileSync(dbPath)));
    const j = JSON.stringify;
    // Arrays from the vm realm have a different prototype; compare by value.
    assert.equal(j(loaded.tables), j(['people']));
    assert.equal(j(loaded.tree.map((t) => [t.kind, t.name])),
        j([['table', 'people'], ['view', 'adults']]));
    assert.equal(j(loaded.tree[0].columns.map((c) => [c.name, c.type, c.pk, c.notnull])),
        j([['id', 'INTEGER', 1, false], ['name', 'TEXT', 0, true]]));
    assert.equal(j(loaded.tree[0].indexes), j(['people_name']));
    console.log('schema tree: ' + loaded.tree.map((t) =>
        t.kind + ' ' + t.name + '(' + t.columns.map((c) => c.name).join(',') + ')').join(' | '));

    // Data grid path (P4/P6): paged dump with rowid prefix + edit ops (P6).
    const grid = context.webcSqlite.tableRows('people', 0, 50);
    assert.equal(j([grid.columns, grid.rows, grid.total, grid.hasRowid]),
        j([['__webc_rowid', 'id', 'name'], [['1', '1', 'alice']], 1, true]));
    console.log('grid: ' + grid.columns.join(',') + ' rows=' + grid.rows.length + '/' + grid.total);

    const exec = (sql, params) => context.webcSqlite.execute(sql, params);
    assert.equal(exec('INSERT INTO people(name) VALUES (?)', ['bob']), 1);
    assert.equal(exec('UPDATE people SET name = ? WHERE rowid = ?', ['robert', 2]), 1);
    assert.equal(context.webcSqlite.tableRows('people', 0, 50).total, 2);
    assert.equal(exec('DELETE FROM people WHERE rowid = ?', [2]), 1);
    assert.equal(context.webcSqlite.tableRows('people', 0, 50).rows[0][2], 'alice');
    assert.throws(() => exec('INSERT INTO people(name) VALUES (?)', [null]), /NOT NULL/);
    console.log('edits: insert/update/delete + NOT NULL rejected');

    // SQL console path (P5): ad-hoc query + error propagation.
    const one = context.webcSqlite.query("SELECT 1 AS one, 'x' AS two");
    assert.equal(j([one.columns, one.rows]), j([['one', 'two'], [['1', 'x']]]));
    assert.throws(() => context.webcSqlite.query('SELECT * FROM missing'),
        /no such table: missing/);
    console.log('console: one,two + error propagated');

    // Table designer path (P7): CREATE TABLE + validation.
    context.webcSqlite.createTable('notes', [
        { name: 'id', type: 'INTEGER', pk: true },
        { name: 'body', type: 'TEXT', notnull: true },
    ]);
    const created = context.webcSqlite.tableRows('notes', 0, 10);
    assert.equal(j([created.columns, created.total]),
        j([['__webc_rowid', 'id', 'body'], 0]));
    const ddl = context.webcSqlite
        .query("SELECT sql FROM sqlite_master WHERE name = 'notes'").rows[0][0];
    assert.match(ddl, /CREATE TABLE "notes" \("id" INTEGER PRIMARY KEY, "body" TEXT NOT NULL\)/);
    assert.throws(() => context.webcSqlite.createTable('', []),
        /table name required/);
    assert.throws(() => context.webcSqlite.createTable('x', []),
        /at least one column/);
    assert.throws(() => context.webcSqlite.createTable('x', [{ name: 'a', type: 'TEXT); DROP' }]),
        /invalid column type/);
    // schema() re-reads without resetting the attached database.
    const after = context.webcSqlite.schema();
    assert.equal(j(after.tables), j(['notes', 'people']));
    console.log('designer: CREATE TABLE + validation + schema refresh');

    // Import / export path (P8): serialize, SQL script, round-trip load.
    const blob = context.webcSqlite.dump();
    assert.equal(Buffer.from(blob.slice(0, 16)).toString('latin1')
        .slice(0, 15), 'SQLite format 3');
    context.webcSqlite.execAll(
        "INSERT INTO notes(body) VALUES ('hello');"
        + "CREATE INDEX notes_body ON notes(body);"
        + "INSERT INTO notes(body) VALUES ('world');");
    assert.equal(context.webcSqlite.tableRows('notes', 0, 10).total, 2);
    assert.throws(() => context.webcSqlite.execAll('CREATE TABLE; broken'),
        /syntax error/);
    context.webcSqlite.load(blob); // round-trip: snapshot predates the inserts
    assert.equal(j(context.webcSqlite.schema().tables), j(['notes', 'people']));
    assert.equal(context.webcSqlite.tableRows('notes', 0, 10).total, 0);
    console.log('io: dump + execAll + round-trip load');

    // Sort / filter / page (P10) + identifier guard + drop (P11).
    exec("INSERT INTO people(name) VALUES ('bob')", []);
    exec("INSERT INTO people(name) VALUES ('alfred')", []);
    let r = context.webcSqlite.tableRows('people', 0, 10, { order: 'name', desc: true });
    assert.equal(j(r.rows.map((x) => x[2])), j(['bob', 'alice', 'alfred']));
    r = context.webcSqlite.tableRows('people', 0, 10, { order: 'name' });
    assert.equal(j(r.rows.map((x) => x[2])), j(['alfred', 'alice', 'bob']));
    r = context.webcSqlite.tableRows('people', 0, 10, { filter: 'al' });
    assert.equal(j([r.total, r.rows.map((x) => x[2])]), j([2, ['alice', 'alfred']]));
    r = context.webcSqlite.tableRows('people', 0, 10, { filter: '%' });
    assert.equal(r.total, 0);  // literal % escaped, not a wildcard
    r = context.webcSqlite.tableRows('people', 2, 1);
    assert.equal(r.rows.length, 1);  // page offset
    // order must be a real column name; junk is ignored, not executed.
    r = context.webcSqlite.tableRows('people', 0, 10,
        { order: 'name; DROP TABLE people' });
    assert.equal(j([r.total, r.rows.length]), j([3, 3]));
    assert.ok(context.webcSqlite.schema().tables.includes('people'));
    exec('DROP VIEW adults', []);
    assert.equal(j(context.webcSqlite.schema().tree.map((t) => t.kind)),
        j(['table', 'table']));
    console.log('grid+: sort/filter/page + injection guard; drop view');

    // Dirty flag + alter existing table (P12).
    context.webcSqlite.load(blob);  // clean state
    assert.equal(context.webcSqlite.isDirty(), false);
    context.webcSqlite.query('SELECT * FROM people');  // readonly → clean
    assert.equal(context.webcSqlite.isDirty(), false);
    context.webcSqlite.query('CREATE TABLE t2(a)');  // console mutation → dirty
    assert.equal(context.webcSqlite.isDirty(), true);
    context.webcSqlite.load(blob);  // reload clears
    assert.equal(context.webcSqlite.isDirty(), false);

    const peopleCols = () => context.webcSqlite.schema().tree
        .find((t) => t.name === 'people').columns.map((c) => c.name);
    let plan = context.webcSqlite.alterTable('people', [
        { name: 'id', type: 'INTEGER', pk: true, orig: 'id' },
        { name: 'nick', type: 'TEXT', notnull: true, orig: 'name' },
        { name: 'extra', type: 'TEXT' },
    ]);
    assert.equal(j(plan), j([
        'ALTER TABLE "people" RENAME COLUMN "name" TO "nick"',
        'ALTER TABLE "people" ADD COLUMN "extra" TEXT',
    ]));
    assert.equal(j(peopleCols()), j(['id', 'nick', 'extra']));
    assert.equal(context.webcSqlite.tableRows('people', 0, 10).rows[0][2], 'alice');
    plan = context.webcSqlite.alterTable('people', [
        { name: 'id', type: 'INTEGER', pk: true, orig: 'id' },
        { name: 'nick', type: 'TEXT', notnull: true, orig: 'nick' },
    ]);
    assert.equal(j(plan), j(['ALTER TABLE "people" DROP COLUMN "extra"']));
    assert.equal(j(peopleCols()), j(['id', 'nick']));
    // confirmFn veto: nothing applied
    assert.equal(context.webcSqlite.alterTable('people', [
        { name: 'id', type: 'INTEGER', pk: true, orig: 'id' },
        { name: 'nick', type: 'TEXT', notnull: true, orig: 'nick' },
        { name: 'x', type: 'TEXT' },
    ], () => false), null);
    assert.equal(j(peopleCols()), j(['id', 'nick']));
    console.log('p12: dirty flag + alter rename/add/drop + veto');

    // Console row cap + CSV encoder (P13).
    exec('CREATE TABLE many(a INTEGER)', []);
    for (let i = 0; i < 10; i++) exec('INSERT INTO many(a) VALUES (?)', [i]);
    const cap = context.webcSqlite.query('SELECT a FROM many', [], 3);
    assert.equal(j([cap.rows.length, cap.truncated]), j([3, true]));
    const full = context.webcSqlite.query('SELECT a FROM many');
    assert.equal(j([full.rows.length, full.truncated]), j([10, false]));
    assert.equal(context.webcSqlite.csv(['x', 'y'], [['a"b', null], ['c', 'd']]),
        '"x","y"\r\n"a""b",""\r\n"c","d"\r\n');
    console.log('p13: query row cap + csv');

    // Index designer + view editor (P14).
    context.webcSqlite.createIndex('many_a', 'many', ['a'], false);
    assert.ok(context.webcSqlite.schema().tree
        .find((t) => t.name === 'many').indexes.includes('many_a'));
    assert.throws(() => context.webcSqlite
        .createIndex('x1', 'many', ['nope'], false), /no such column/);
    assert.throws(() => context.webcSqlite
        .createIndex('x1', 'ghost', ['a'], false), /table not found/);
    context.webcSqlite.createView('adult_v', 'SELECT * FROM people');
    assert.ok(/^CREATE VIEW/.test(context.webcSqlite.viewSql('adult_v')));
    // Body compile-check runs before replace: bad body keeps the old view.
    assert.throws(() => context.webcSqlite
        .createView('adult_v', 'SELECT * FROM missing_tbl'), /no such table/);
    assert.ok(context.webcSqlite.viewSql('adult_v'));
    assert.throws(() => context.webcSqlite
        .createView('adult_v', 'DELETE FROM people'), /SELECT/);
    assert.throws(() => context.webcSqlite.createView('adult_v',
        'WITH x AS (SELECT 1) DELETE FROM people'), /./);
    context.webcSqlite.createView('adult_v', 'SELECT id, nick FROM people');
    context.webcSqlite.execute('DROP INDEX many_a', []);
    assert.ok(!context.webcSqlite.schema().tree
        .find((t) => t.name === 'many').indexes.includes('many_a'));
    console.log('p14: index create/validate + view save/compile-guard + drop index');

    // Compile-check + trigger editor (P15).
    context.webcSqlite.checkSql('SELECT 1');
    assert.throws(() => context.webcSqlite
        .checkSql('SELECT FROM WHERE'), /./);
    context.webcSqlite.createTrigger(
        'CREATE TRIGGER many_log AFTER INSERT ON many BEGIN '
        + 'UPDATE many SET a = a WHERE 1; END');
    assert.ok(context.webcSqlite.schema().tree
        .some((t) => t.kind === 'trigger' && t.name === 'many_log'));
    assert.ok(/^CREATE TRIGGER/.test(
        context.webcSqlite.objectSql('many_log')));
    assert.throws(() => context.webcSqlite
        .createTrigger('DELETE FROM people'), /CREATE/);
    // Compile-check runs before replace: bad body keeps the old trigger.
    assert.throws(() => context.webcSqlite.createTrigger(
        'CREATE TRIGGER many_log AFTER INSERT ON missing_t BEGIN SELECT 1; END'),
        /no such table/);
    assert.ok(context.webcSqlite.schema().tree
        .some((t) => t.kind === 'trigger' && t.name === 'many_log'));
    // Replace with a fixed body, then drop (tree ✕ path).
    context.webcSqlite.createTrigger(
        'CREATE TRIGGER many_log AFTER DELETE ON many BEGIN SELECT 1; END');
    context.webcSqlite.execute('DROP TRIGGER many_log', []);
    assert.ok(!context.webcSqlite.schema().tree
        .some((t) => t.kind === 'trigger'));
    console.log('p15: compile-check + trigger create/replace/drop');

    // Transactions (P16).
    assert.equal(context.webcSqlite.inTransaction(), false);
    context.webcSqlite.begin();
    assert.equal(context.webcSqlite.inTransaction(), true);
    exec('INSERT INTO many(a) VALUES (?)', [99]);
    assert.throws(() => context.webcSqlite.begin(), /within a transaction/);
    context.webcSqlite.rollback();
    assert.equal(context.webcSqlite.inTransaction(), false);
    assert.equal(context.webcSqlite
        .query('SELECT count(*) FROM many WHERE a = ?', [99]).rows[0][0], '0');
    context.webcSqlite.begin();
    exec('INSERT INTO many(a) VALUES (?)', [77]);
    context.webcSqlite.commit();
    assert.equal(context.webcSqlite.inTransaction(), false);
    assert.equal(context.webcSqlite
        .query('SELECT count(*) FROM many WHERE a = ?', [77]).rows[0][0], '1');
    console.log('p16: begin/commit/rollback + nested rejection + autocommit state');

    // Bulk delete of selected rows (P17).
    assert.throws(() => context.webcSqlite
        .deleteRows('many', []), /no rows selected/);
    assert.throws(() => context.webcSqlite
        .deleteRows('many', ['1; DROP TABLE many']), /bad rowid/);
    exec('INSERT INTO many(a) VALUES (?)', [1]);
    exec('INSERT INTO many(a) VALUES (?)', [2]);
    exec('INSERT INTO many(a) VALUES (?)', [3]);
    const manyIds = context.webcSqlite.tableRows('many', 0, 50).rows
        .map((r) => r[0]);
    assert.ok(manyIds.length >= 4);  // p13 rows + 77 + the three above
    context.webcSqlite.deleteRows('many', manyIds);
    assert.equal(context.webcSqlite.tableRows('many', 0, 10).total, 0);
    console.log('p17: bulk delete validation + rowid IN (...)');

    // Database info panel + FK toggle (P18).
    const info = context.webcSqlite.dbInfo();
    assert.equal(info['sqlite version'], context.webcSqlite.version());
    assert.ok(Number(info.page_size) > 0);
    assert.ok(Number(info.page_count) > 0);
    assert.equal(info['tables/views/triggers'].split('/').length, 3);
    assert.equal(info.foreign_keys, '0');
    context.webcSqlite.execute('PRAGMA foreign_keys = ON', []);
    assert.equal(context.webcSqlite.dbInfo().foreign_keys, '1');
    context.webcSqlite.execute('PRAGMA foreign_keys = OFF', []);
    assert.equal(context.webcSqlite.dbInfo().foreign_keys, '0');
    console.log('p18: db info + foreign_keys toggle');

    // WAL-format file (regression: file-format byte 19 = 2 made the first
    // prepare fail with SQLITE_NOTADB, boot died, every button was dead).
    const walPath = path.join(tmp, 'wal.db');
    const wdb = new DatabaseSync(walPath);
    wdb.exec("PRAGMA journal_mode=WAL");
    wdb.exec("CREATE TABLE w(v TEXT); INSERT INTO w(v) VALUES ('wal');");
    wdb.exec("PRAGMA wal_checkpoint(TRUNCATE)");
    wdb.close();
    const walBytes = fs.readFileSync(walPath);
    assert.equal(walBytes[19], 2);   // fixture really is WAL format
    assert.equal(fs.existsSync(walPath + '-wal'), false);  // cleanly closed
    const wloaded = context.webcSqlite.load(new Uint8Array(walBytes));
    assert.equal(j(wloaded.tables), j(['w']));
    assert.equal(context.webcSqlite.tableRows('w', 0, 10).rows[0][1], 'wal');
    // dump() restores the device's journal-mode bytes (engine copy runs as
    // rollback-journal) so save/export round-trips the format.
    const wd = context.webcSqlite.dump();
    assert.equal(j([wd[18], wd[19]]), j([2, 2]));
    exec("INSERT INTO w(v) VALUES ('edited')", []);
    assert.equal(context.webcSqlite.isDirty(), true);
    const wd2 = context.webcSqlite.dump();
    assert.equal(j([wd2[18], wd2[19]]), j([2, 2]));
    context.webcSqlite.load(new Uint8Array(wd2));  // round-trip with edits
    assert.equal(context.webcSqlite
        .query("SELECT count(*) FROM w WHERE v='edited'").rows[0][0], '1');
    // A plain rollback-journal file must stay untouched (no format rewrite).
    const dBytes = fs.readFileSync(dbPath);
    context.webcSqlite.load(new Uint8Array(dBytes));
    assert.equal(Buffer.compare(Buffer.from(context.webcSqlite.dump()),
        dBytes), 0);
    console.log('p19: WAL-format load + format-preserving dump + byte-exact delete-mode dump');
} finally {
    fs.rmSync(tmp, { recursive: true, force: true });
}
