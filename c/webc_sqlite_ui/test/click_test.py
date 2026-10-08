#!/usr/bin/env python3
"""DB Admin suite - P19: real-browser interaction test.

Boots /db in headless Chromium over CDP (test/click.mjs) and clicks the UI:
tab/pane sync, tree->grid, SQL run, dirty indicator, info-panel refresh,
reload. Catches boot crashes and dead handlers that presence-based HTTP
checks cannot see.

Skips (exit 0 with a notice) when chromium or a WebSocket-capable node is
unavailable, so the suite still runs on minimal hosts.
"""
import os
import shutil
import socket
import sqlite3
import subprocess
import sys
import time
import urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from testlib import Client, Checker  # noqa: E402

HOST = sys.argv[1]
PORT = int(sys.argv[2])
TMP = sys.argv[3]

CLICK_DB = os.path.join(TMP, "click.db")
FORM = {"Content-Type": "application/x-www-form-urlencoded"}

c = Client(HOST, PORT)
t = Checker()


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def node_has_websocket():
    if not shutil.which("node"):
        return False
    probe = subprocess.run(
        ["node", "-e", "process.exit(typeof WebSocket === 'undefined' ? 1 : 0)"],
        capture_output=True,
    )
    return probe.returncode == 0


def main():
    chromium = shutil.which("chromium") or shutil.which("chromium-browser") \
        or shutil.which("google-chrome")
    if not chromium:
        print("SKIP click test: no chromium")
        return
    if not node_has_websocket():
        print("SKIP click test: node without global WebSocket")
        return

    # ---- fixture: 2 rows so the grid assertion (header + rows >= 3) holds -
    # WAL format on purpose: file-format byte 19 = 2 used to kill boot with
    # SQLITE_NOTADB before any handler bound (all buttons dead). The whole
    # browser battery below now runs against exactly that header.
    conn = sqlite3.connect(CLICK_DB)
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("CREATE TABLE people(id INTEGER PRIMARY KEY, name TEXT)")
    conn.execute("INSERT INTO people(name) VALUES ('alice'),('grace')")
    conn.commit()
    conn.close()
    with open(CLICK_DB, "rb") as f:
        click_hdr = f.read(20)
    t.chk("click fixture is WAL format", click_hdr[19], 2)
    t.chk("click fixture has no sidecars",
          any(os.path.exists(CLICK_DB + s) for s in ("-wal", "-shm")), False)

    st, _, resp, _ = c.req_full(
        "POST", "/db/open", urllib.parse.urlencode(
            {"path": CLICK_DB, "action": "open"}).encode(), FORM)
    t.chk("open click fixture", st in (200, 302), True)

    dbg = free_port()
    user_dir = os.path.join(TMP, "chromium-profile")
    chrome = subprocess.Popen(
        [chromium, "--headless=new", "--disable-gpu", "--no-sandbox",
         f"--user-data-dir={user_dir}",
         f"--remote-debugging-port={dbg}", "--remote-allow-origins=*",
         "about:blank"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    try:
        # wait for the DevTools endpoint
        for _ in range(50):
            try:
                with socket.create_connection(("127.0.0.1", dbg), timeout=0.3):
                    break
            except OSError:
                time.sleep(0.2)
        proc = subprocess.run(
            ["node", os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                  "click.mjs"),
             f"http://{HOST}:{PORT}", str(dbg)],
            capture_output=True, text=True, timeout=120,
        )
        out = (proc.stdout + proc.stderr).strip()
        print(out)
        t.chk("click suite exit 0", proc.returncode, 0)
        t.chk("click suite summary", "CLICK TEST PASSED" in out, True)
    finally:
        chrome.terminate()
        try:
            chrome.wait(timeout=10)
        except subprocess.TimeoutExpired:
            chrome.kill()

    ok = t.summary()
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
