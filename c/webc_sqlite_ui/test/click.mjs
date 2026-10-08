// CDP interaction test: boots /db in headless Chromium and really clicks it.
// Covers what presence-based HTTP checks cannot: handler wiring, tab/pane
// sync, tree->grid rendering, SQL run, dirty indicator, info-panel refresh.
// Driven by test/click_test.py. usage: node click.mjs <base-url> <dbg-port>
const base = process.argv[2];
const dbgPort = process.argv[3];

const resp = await fetch(`http://127.0.0.1:${dbgPort}/json/list`);
const page = (await resp.json()).find((t) => t.type === 'page');
if (!page) { console.log('FAIL no debuggable page'); process.exit(1); }

const ws = new WebSocket(page.webSocketDebuggerUrl);
let id = 0;
const pending = new Map();
const events = [];
ws.onmessage = (ev) => {
    const m = JSON.parse(ev.data);
    if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
    else if (m.method) events.push(m);
};
const send = (method, params) => new Promise((res) => {
    const i = ++id;
    pending.set(i, res);
    ws.send(JSON.stringify({ id: i, method, params }));
});
const sleep = (ms) => new Promise((res) => setTimeout(res, ms));
async function evaljs(expr) {
    const rr = await send('Runtime.evaluate', {
        expression: expr, returnByValue: true, awaitPromise: true,
    });
    if (rr.result?.exceptionDetails) return { exc: String(rr.result.exceptionDetails.text) };
    return rr.result?.result?.value;
}
let fails = 0;
function chk(name, got, want) {
    const ok = JSON.stringify(got) === JSON.stringify(want);
    if (!ok) fails++;
    console.log(`${ok ? 'ok' : 'FAIL'} click ${name}: got=${JSON.stringify(got)} want=${JSON.stringify(want)}`);
}
const activeTab = `document.querySelector('#db-tabs .tab.active')?.dataset.tab`;
const activePane = `document.querySelector('#db-panes .pane.active')?.dataset.pane`;

await new Promise((res) => { ws.onopen = res; });
await send('Page.enable');
await send('Runtime.enable');
await send('Page.navigate', { url: base + '/db' });
await sleep(4000);  // wasm boot (fetch + deserialize + first render)

// 1. boot completes; tabs and panes switch in lockstep
chk('boot ready', await evaljs(
    `document.getElementById('sqlite-wasm-status').textContent.includes('ready')`), true);
await evaljs(`document.querySelectorAll('#db-tabs .tab')[2].click()`); // Structure
chk('tab->structure', await evaljs(activeTab), 'structure');
chk('pane->structure', await evaljs(activePane), 'structure');
await evaljs(`document.querySelectorAll('#db-tabs .tab')[0].click()`); // Browse
chk('tab->browse', await evaljs(activeTab), 'browse');
chk('pane->browse', await evaljs(activePane), 'browse');

// 2. tree summary click -> opens details -> grid renders (header + rows)
await evaljs(`document.querySelector('#sqlite-db-tree .tree-node summary').click()`);
await sleep(600);
chk('tree->pane browse', await evaljs(activePane), 'browse');
chk('grid rows >= 3', (await evaljs(
    `document.querySelectorAll('#sqlite-db-grid tr').length`)) >= 3, true);

// 3. SQL tab: run a SELECT, read the result
await evaljs(`document.querySelectorAll('#db-tabs .tab')[1].click()`);
chk('pane->sql', await evaljs(activePane), 'sql');
await evaljs(`document.getElementById('sqlite-sql-input').value =
    "SELECT count(*) AS n FROM people"`);
await evaljs(`document.getElementById('sqlite-sql-run').click()`);
await sleep(600);
chk('select result has 2', await evaljs(
    `document.getElementById('sqlite-sql-result').textContent.includes('2')`), true);

// 4. handlers are wired (the P19 boot crash left every button dead)
chk('run handler bound', await evaljs(
    `typeof document.getElementById('sqlite-sql-run').onclick === 'function'`), true);
chk('save handler bound', await evaljs(
    `document.getElementById('sqlite-save-db').onclick !== null`), true);
chk('tab handler bound', await evaljs(
    `typeof document.querySelectorAll('#db-tabs .tab')[3].onclick === 'function'`), true);

// 5. INSERT -> dirty indicator; info panel re-render must not recurse
await evaljs(`document.getElementById('sqlite-sql-input').value =
    "INSERT INTO people(name) VALUES ('linus')"`);
await evaljs(`document.getElementById('sqlite-sql-run').click()`);
await sleep(600);
chk('dirty after insert', await evaljs(
    `!document.getElementById('sqlite-dirty').classList.contains('hidden')`), true);
chk('info rows >= 6', await evaljs(
    `document.querySelectorAll('#sqlite-db-info .info-k').length >= 6`), true);
chk('status not crashed', await evaljs(
    `!document.getElementById('sqlite-wasm-status').textContent.includes('unavailable')`), true);

// 6. Connection tab: PRAGMA toggle exercises updateDirty->infoHook path
await evaljs(`document.querySelectorAll('#db-tabs .tab')[3].click()`);
chk('pane->connection', await evaljs(activePane), 'connection');
await evaljs(`document.getElementById('sqlite-fk-toggle').click()`);
await sleep(500);
chk('no crash after fk toggle', await evaljs(
    `!document.getElementById('sqlite-wasm-status').textContent.includes('unavailable')`), true);

// 7. Info tab re-renders after mutations
await evaljs(`document.querySelectorAll('#db-tabs .tab')[4].click()`);
chk('pane->info', await evaljs(activePane), 'info');
chk('info rows >= 6 final', await evaljs(
    `document.querySelectorAll('#sqlite-db-info .info-k').length >= 6`), true);

// 8. Reload re-reads file bytes cleanly (discards the in-memory insert)
await evaljs(`document.getElementById('sqlite-reload-db').click()`);
await sleep(1200);
chk('reload keeps ready', await evaljs(
    `document.getElementById('sqlite-wasm-status').textContent.includes('ready')`), true);

const errs = events.filter((e) => e.method === 'Runtime.exceptionThrown'
    || (e.method === 'Runtime.consoleAPICalled' && e.params.type === 'error'));
chk('no console exceptions', errs.length, 0);

console.log(fails === 0 ? 'CLICK TEST PASSED' : `CLICK TEST FAILED: ${fails}`);
ws.close();
process.exit(fails === 0 ? 0 : 1);
