#!/usr/bin/env python3
"""GeoCascade browser suite - drives the real picker in headless chromium.

Why this exists: GeoCascade.js is the only thing that fills the province /
district / commune selects (the server ships them disabled with just their
placeholder). Every other suite only scrapes HTML, so a JS bug there is
invisible to them - which is exactly how the create form shipped with all
three ancestors permanently disabled and no way to ever pick a village.

This suite executes the script in a real browser and walks the whole flow:
province -> district -> commune -> (lazy) village -> submit -> row in the DB.

SKIPs loudly (exit 0) when chromium or the `websocket` module is missing,
so the suite stays runnable on a box without a browser.
"""
import json
import os
import shutil
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from testlib import Client, Checker  # noqa: E402

HOST = sys.argv[1]
PORT = int(sys.argv[2])
TMP = sys.argv[3]
DB = os.path.join(TMP, "nested", "deep", "db")

MARKER = "GEOC-"
STAFF = "staff"

t = Checker()


# ------------------------------------------------------- required FKs


def db_value_taken(table, col, value):
    """Is this value already held by ANY row, live or soft-deleted?

    staff.user_id / staff.org_unit_id... are checked against UNIQUE, and a
    UNIQUE constraint does NOT care about deleted_at: a soft-deleted staff
    row still occupies its user_id. So "free" here must mean no row holds
    it at all - filtering on deleted_at (like crud_contract's helper does)
    hands back a value the engine answers 409 for.
    """
    if not table or not col or value == "":
        return False
    conn = sqlite3.connect(DB, timeout=10)
    try:
        conn.execute("PRAGMA busy_timeout=5000")
        try:
            return conn.execute(
                f"SELECT count(*) FROM {table} WHERE {col} = ?",
                (value,)).fetchone()[0] > 0
        except sqlite3.Error:
            return False
    finally:
        conn.close()


def pick_fk_values(choices):
    """name -> first option value no live staff row holds yet."""
    preset = {}
    for name, options in choices.items():
        for value in options:
            if value and not db_value_taken(STAFF, name, value):
                preset[name] = value
                break
        else:
            if options:
                preset[name] = options[0]
    return preset


# ---------------------------------------------------------------- browser


def find_chromium():
    for name in ("chromium", "chromium-browser", "google-chrome",
                 "google-chrome-stable", "chrome"):
        path = shutil.which(name)
        if path:
            return path
    return None


def websocket_mod():
    try:
        import websocket  # noqa: F401
        return websocket
    except ImportError:
        return None


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


class Browser:
    """A headless chromium page driven over the DevTools protocol."""

    def __init__(self, exe):
        self.exe = exe
        self.proc = None
        self.ws = None
        self._id = 0
        self.port = free_port()
        self.profile = tempfile.mkdtemp(prefix="webc-geocascade-")

    def __enter__(self):
        import websocket
        args = [
            self.exe, "--headless", "--disable-gpu",
            "--disable-dev-shm-usage",
            f"--remote-debugging-port={self.port}",
            f"--user-data-dir={self.profile}",
            "--no-first-run", "--no-default-browser-check",
            "--disable-extensions", "--hide-scrollbars",
            "--window-size=1400,1000",
            "about:blank",
        ]
        if os.geteuid() == 0:
            args.insert(1, "--no-sandbox")
        self.proc = subprocess.Popen(args, stdout=subprocess.DEVNULL,
                                     stderr=subprocess.DEVNULL)
        target = None
        for _ in range(150):
            try:
                import urllib.request
                with urllib.request.urlopen(
                        f"http://127.0.0.1:{self.port}/json/list",
                        timeout=1) as r:
                    tabs = json.loads(r.read().decode())
                pages = [x for x in tabs if x.get("type") == "page"]
                if pages:
                    target = pages[0]
                    break
            except Exception:
                pass
            time.sleep(0.2)
        if not target:
            self.__exit__(None, None, None)
            raise RuntimeError("chromium never exposed a CDP target")
        self.ws = websocket.create_connection(target["webSocketDebuggerUrl"],
                                              timeout=60)
        self.cmd("Page.enable")
        self.cmd("Runtime.enable")
        self.cmd("Network.enable")
        return self

    def __exit__(self, *exc):
        if self.ws:
            try:
                self.ws.close()
            except Exception:
                pass
        if self.proc:
            self.proc.kill()
            try:
                self.proc.wait(timeout=10)
            except Exception:
                self.proc.kill()
        shutil.rmtree(self.profile, ignore_errors=True)

    def cmd(self, method, params=None):
        self._id += 1
        mine = self._id
        self.ws.send(json.dumps({"id": mine, "method": method,
                                 "params": params or {}}))
        while True:
            msg = json.loads(self.ws.recv())
            if msg.get("id") == mine:
                if "error" in msg:
                    raise RuntimeError(f"{method}: {msg['error']}")
                return msg.get("result", {})

    def set_cookies(self, cookies, base):
        for name, value in cookies.items():
            self.cmd("Network.setCookie", {"name": name, "value": value,
                                           "url": base})

    def goto(self, url):
        self.cmd("Page.navigate", {"url": url})
        for _ in range(150):
            st = self.cmd("Runtime.evaluate",
                          {"expression": "document.readyState",
                           "returnByValue": True})
            if st.get("result", {}).get("value") == "complete":
                break
            time.sleep(0.2)
        # let DOMContentLoaded handlers (GeoCascade initAll) finish
        time.sleep(0.8)

    def eval_js(self, expression):
        """Evaluate an expression; resolves promises; raises on exception."""
        res = self.cmd("Runtime.evaluate", {
            "expression": expression,
            "returnByValue": True,
            "awaitPromise": True,
        })
        details = res.get("exceptionDetails")
        if details:
            raise RuntimeError(
                "JS exception: "
                + json.dumps(details.get("exception", details),
                             ensure_ascii=False)[:800])
        out = res.get("result", {})
        if "value" not in out:
            raise RuntimeError("no value returned: "
                               + json.dumps(out)[:400])
        return out["value"]


# ------------------------------------------------------------- probe JS

# Snapshot one cascade box's four levels.
SNAP_JS = """
(() => {
  const box = document.querySelector(%s);
  if (!box) return null;
  const out = {};
  for (const l of ['province','district','commune','village']) {
    const s = box.querySelector('[data-geo="' + l + '"]');
    out[l] = s ? { disabled: !!s.disabled, options: s.options.length,
                   name: s.getAttribute('name'),
                   value: s.value } : 'MISSING';
  }
  return out;
})()
"""

# Required selects with no value yet, so Python can choose one that no
# live row holds (staff.user_id / org_unit_id are UNIQUE).
FK_CHOICES_JS = """
(() => {
  const form = document.querySelector('form[data-md-op="create"]');
  if (!form) return null;
  const out = {};
  for (const s of form.querySelectorAll('select[name]')) {
    if (!s.hasAttribute('required') || s.value) continue;
    out[s.name] = Array.from(s.options).map(o => o.value).filter(v => v);
  }
  return out;
})()
"""

# Walk province -> district -> commune -> village on the CREATE form and
# submit it. Fills the required plain inputs first, the way a user would.
CREATE_WALK_JS = r"""
(async (preset) => {
  const sleep = ms => new Promise(r => setTimeout(r, ms));
  const form = document.querySelector('form[data-md-op="create"]');
  if (!form) return { fatal: 'no create form' };
  const box = form.querySelector('[data-geo-cascade]');
  if (!box) return { fatal: 'no cascade box in create form' };
  const el = l => box.querySelector('[data-geo="' + l + '"]');
  const out = { step: {} };

  const marker = 'GEOC-' + Date.now().toString(36).toUpperCase();
  for (const inp of form.querySelectorAll('input[name], textarea[name]')) {
    if (!inp.name || inp.disabled) continue;
    if (['hidden','checkbox','radio','file','submit','button','reset']
        .includes(inp.type)) continue;
    if (inp.type === 'date') { inp.value = '2026-02-03'; continue; }
    if (inp.hasAttribute('required') ||
        ['staff_code','first_name','last_name'].includes(inp.name)) {
      inp.value = inp.name === 'staff_code' ? marker : 'geoc_' + inp.name;
    }
  }
  // required selects: the caller picks values no live row holds
  for (const s of form.querySelectorAll('select[name]')) {
    if (!s.hasAttribute('required')) continue;
    const want = preset && preset[s.name];
    if (want) { s.value = want; continue; }
    if (s.value) continue;
    const real = Array.from(s.options).find(o => o.value);
    if (real) s.value = real.value;
  }

  // province has no parent: it must already be live before any interaction
  const p = el('province');
  out.step.province_before = { disabled: p.disabled, options: p.options.length };
  if (p.disabled || p.options.length < 2) {
    out.fatal = 'province not live';
    return out;
  }

  p.value = Array.from(p.options).find(o => o.value).value;
  p.dispatchEvent(new Event('change', { bubbles: true }));
  out.step.district = { disabled: el('district').disabled,
                        options: el('district').options.length };
  if (el('district').disabled || el('district').options.length < 2) {
    out.fatal = 'district did not open';
    return out;
  }

  el('district').value =
      Array.from(el('district').options).find(o => o.value).value;
  el('district').dispatchEvent(new Event('change', { bubbles: true }));
  out.step.commune = { disabled: el('commune').disabled,
                       options: el('commune').options.length };
  if (el('commune').disabled || el('commune').options.length < 2) {
    out.fatal = 'commune did not open';
    return out;
  }

  el('commune').value =
      Array.from(el('commune').options).find(o => o.value).value;
  el('commune').dispatchEvent(new Event('change', { bubbles: true }));
  for (let i = 0; i < 50 && el('village').options.length < 2; ++i) {
    await sleep(100);
  }
  out.step.village = { options: el('village').options.length };
  if (el('village').options.length < 2) {
    out.fatal = 'villages never loaded';
    return out;
  }

  const real = Array.from(el('village').options).find(o => o.value);
  el('village').value = real.value;
  out.village = { value: real.value, label: real.textContent };
  out.marker = marker;

  const fd = new FormData(form);
  out.posted_geo_fields = Array.from(fd.keys())
      .filter(k => ['village_id','province_id','district_id','commune_id']
                   .includes(k));
  out.posted_village = fd.get('village_id');
  // redirect:'follow' (the default) so we see the PRG landing page;
  // `redirected` is what proves the 302 happened.
  const r = await fetch(form.getAttribute('action'),
                        { method: 'POST', body: fd,
                          credentials: 'same-origin' });
  out.submit = { status: r.status, redirected: r.redirected, url: r.url };
  return out;
})(%s)
"""

# The UPDATE form must still restore from its stored village: restore()'s
# other branch walks village -> commune -> district -> province. The create
# fix only touched the !stored path, but this pins both.
EDIT_RESTORE_JS = """
(() => {
  const btn = document.querySelector('.master-edit-btn');
  if (!btn) return { fatal: 'no edit button on page' };
  btn.click();
  const forms = Array.from(document.querySelectorAll('form[data-md-op="update"]'))
      .filter(f => f.offsetParent !== null);
  const out = { visible_update_forms: forms.length, boxes: [] };
  for (const f of forms) {
    const b = f.querySelector('[data-geo-cascade]');
    if (!b) { out.boxes.push({ fatal: 'no cascade box in update form' }); continue; }
    const p = b.querySelector('[data-geo="province"]');
    const d = b.querySelector('[data-geo="district"]');
    const c = b.querySelector('[data-geo="commune"]');
    const v = b.querySelector('[data-geo="village"]');
    out.boxes.push({
      province_disabled: !!p && p.disabled,
      province_options: p ? p.options.length : -1,
      district_options: d ? d.options.length : -1,
      commune_options: c ? c.options.length : -1,
      village_value: v ? v.value : null,
      village_options: v ? v.options.length : -1,
    });
  }
  return out;
})()
"""


# ------------------------------------------------------------------ main

def main():
    exe = find_chromium()
    ws_mod = websocket_mod()
    if not exe or not ws_mod:
        why = []
        if not exe:
            why.append("no chromium/chrome on PATH")
        if not ws_mod:
            why.append("python websocket module not installed")
        print("SKIP: geo_cascade_test needs a real browser (%s)"
              % "; ".join(why))
        print("      GeoCascade.js is otherwise untested - install chromium "
              "and `websocket` to cover it.")
        return

    base = f"http://{HOST}:{PORT}"
    c = Client(HOST, PORT)
    st, _ = c.login_as("sd.staff1", "posadmin1")
    t.chk("login", st in (200, 302, 303), True)

    # What the browser is about to execute must be the file on disk. NOTE:
    # /js/* is served from build/bundle.h, which is COMPILED INTO the
    # binary (serve_resource checks find_resource() before touching disk),
    # so a stale binary silently serves old JS - and a JS bug is invisible
    # to every other suite, so a mismatch would go unnoticed.
    import hashlib
    import urllib.request
    served = urllib.request.urlopen(f"{base}/js/GeoCascade.js",
                                    timeout=15).read()
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "..", "js", "GeoCascade.js"), "rb") as fh:
        on_disk = fh.read()
    served_md5 = hashlib.md5(served).hexdigest()
    disk_md5 = hashlib.md5(on_disk).hexdigest()
    if served_md5 != disk_md5:
        print("  NOTE: the running binary serves stale GeoCascade.js "
              f"(bundle={served_md5[:8]}, disk={disk_md5[:8]}). "
              "Rebuild with ./build/bin/nob before trusting these checks.")
    t.chk("browser will execute the GeoCascade.js on disk",
          served_md5, disk_md5)

    with Browser(exe) as b:
        b.set_cookies(c.cookies, base)
        b.goto(f"{base}/pos/staff")

        # ---- the dataset the whole picker runs off --------------------
        data = b.eval_js("""
        (() => {
          const el = document.querySelector('script[data-geo-data]');
          if (!el) return null;
          try { const d = JSON.parse(el.textContent);
                return { p: (d.p||[]).length, d: (d.d||[]).length,
                         c: (d.c||[]).length,
                         s: Object.keys(d.s||{}).length }; }
          catch (e) { return { error: String(e) }; }
        })()""")
        t.chk("geo dataset embedded", data is not None, True)
        if data:
            t.chk("dataset holds all 44 provinces", data.get("p"), 44)
            t.chk("dataset holds 227 districts", data.get("d"), 227)
            t.chk("dataset holds 1712 communes", data.get("c"), 1712)

        # ---- THE BUG: province has no parent, so it must start live ---
        # initBox() disables all three ancestors; restore() used to early
        # return on an empty stored village (the create form) after
        # clearBelow(box, 0) had disabled province too - so nothing ever
        # re-enabled it and the picker was dead on create.
        snap = b.eval_js(SNAP_JS
                         % "'form[data-md-op=\"create\"] [data-geo-cascade]'")
        t.chk("create form has a cascade box", snap is not None, True)
        if snap:
            t.chk("create: province ENABLED (no parent to wait for)",
                  snap["province"]["disabled"], False)
            t.chk("create: province offers every province",
                  snap["province"]["options"] > 40, True)
            t.chk("create: district waits for its parent",
                  snap["district"]["disabled"], True)
            t.chk("create: commune waits for its parent",
                  snap["commune"]["disabled"], True)
            # ancestors are UI-only: no name -> never posted
            for lvl in ("province", "district", "commune"):
                t.chk(f"create: {lvl} select carries no name (not posted)",
                      snap[lvl]["name"], None)
            t.chk("create: village select posts village_id",
                  snap["village"]["name"], "village_id")

        # ---- walk the whole cascade and submit ------------------------
        choices = b.eval_js(FK_CHOICES_JS) or {}
        preset = pick_fk_values(choices)
        walked = b.eval_js(CREATE_WALK_JS % json.dumps(preset))
        t.chk("cascade walk completed", walked.get("fatal"), None)
        if walked.get("fatal"):
            print("  walk state:", json.dumps(walked, ensure_ascii=False))
        else:
            t.chk("province was live before any click",
                  walked["step"]["province_before"]["disabled"], False)
            t.chk("province offered 44 rows",
                  walked["step"]["province_before"]["options"], 45)
            t.chk("choosing a province opened district",
                  walked["step"]["district"]["options"] > 1, True)
            t.chk("choosing a district opened commune",
                  walked["step"]["commune"]["options"] > 1, True)
            t.chk("choosing a commune lazy-loaded villages",
                  walked["step"]["village"]["options"] > 1, True)
            t.chk("exactly one geo field posted",
                  walked.get("posted_geo_fields"), ["village_id"])
            t.chk("posted village matches the pick",
                  walked.get("posted_village"),
                  walked.get("village", {}).get("value"))
            t.chk("create PRG-redirected after the pick",
                  walked.get("submit", {}).get("redirected"), True)
            if not walked.get("submit", {}).get("redirected"):
                # surface the engine's answer: 409 = a UNIQUE FK collided
                # with a row an earlier suite left, 400 = a required field
                # posted empty - both are form/data issues, not cascade
                # issues, and the numbers say which.
                print("  submit state:",
                      json.dumps(walked.get("submit"), ensure_ascii=False),
                      "preset:", json.dumps(preset, ensure_ascii=False))

            # the row really landed, pointing at the village that was
            # chosen through the four-level walk
            marker = walked.get("marker")
            if marker:
                conn = sqlite3.connect(DB, timeout=10)
                try:
                    conn.execute("PRAGMA busy_timeout=5000")
                    row = conn.execute(
                        "SELECT s.village_id, v.name_kh FROM staff s "
                        "JOIN villages v ON v.id = s.village_id "
                        "WHERE s.staff_code = ?", (marker,)).fetchone()
                finally:
                    conn.close()
                t.chk("created row persisted", row is not None, True)
                if row:
                    t.chk("row's village is the one picked in the UI",
                          row[0], walked.get("village", {}).get("value"))
                    t.chk("that village has a name",
                          bool(row[1] and row[1].strip()), True)

        # ---- the edit side must still work (regression guard) ---------
        edit = b.eval_js(EDIT_RESTORE_JS)
        t.chk("page exposes an edit form", "fatal" not in edit, True)
        if "fatal" not in edit:
            t.chk("update form has a cascade box",
                  any("fatal" not in x for x in edit["boxes"]), True)
            good = [x for x in edit["boxes"] if "fatal" not in x]
            t.chk("edit form restores province from stored village",
                  any(x["province_disabled"] is False
                      and x["province_options"] > 40 for x in good), True)
            t.chk("edit form walks down to its own commune/district",
                  any(x["district_options"] > 1 and x["commune_options"] > 1
                      for x in good), True)
            t.chk("edit form keeps its stored village selected",
                  any(x["village_value"] for x in good), True)

    # ---- cleanup: the row this suite created --------------------------
    conn = sqlite3.connect(DB, timeout=10)
    try:
        conn.execute("PRAGMA busy_timeout=5000")
        conn.execute("DELETE FROM staff WHERE staff_code LIKE ?",
                     (MARKER + "%",))
        conn.commit()
    finally:
        conn.close()


if __name__ == "__main__":
    main()
    ok = t.summary()
    sys.exit(0 if ok else 1)
