#!/usr/bin/env python3
"""MySQL/MariaDB dialect suite for webc (spawned by test/run.sh in parallel
with http_test.py).

Boots a throwaway mariadbd (temp datadir, unix socket, --skip-networking)
plus a webc server pointed at it via WEBC_DB='mysql:...', runs HTTP checks
against it and verifies the rows through the mariadb CLI, then tears both
down. The datadir lives under the TMP directory passed on the command line
(run.sh removes TMP afterwards).

Skips (exit 0) when the mariadb tooling is not installed.
"""
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from testlib import Client, Checker  # noqa: E402

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEBC_BIN = os.path.join(PROJECT, "build", "bin", "webc")


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


class MysqlEnv:
    """Throwaway MariaDB server + a webc server running on top of it."""

    def __init__(self, tmp):
        self.tmp = tmp
        self.datadir = os.path.join(tmp, "mysql")
        self.sock = os.path.join(self.datadir, "s.sock")
        self.err_log = os.path.join(self.datadir, "err.log")
        self.webc_log = os.path.join(tmp, "mysql_webc.log")
        self.daemon = None
        self.webc = None

    def q(self, sql):
        """Raw scalar query against the `webc` database (-N -B: no headers,
        tab-separated; SQL NULL arrives as the literal string 'NULL')."""
        r = subprocess.run(
            ["mariadb", f"--socket={self.sock}", "-uroot", "-B", "-N",
             "webc", "-e", sql],
            capture_output=True, text=True, timeout=30,
        )
        if r.returncode:
            raise RuntimeError(f"mariadb query failed: {r.stderr.strip()}")
        return r.stdout.rstrip("\n")

    def start_daemon(self):
        os.makedirs(self.datadir)
        subprocess.run(
            ["mariadb-install-db", f"--datadir={self.datadir}",
             "--auth-root-authentication-method=normal", "--skip-test-db"],
            check=True, capture_output=True, timeout=60,
        )
        self.daemon = subprocess.Popen(
            ["mariadbd", f"--datadir={self.datadir}", f"--socket={self.sock}",
             "--skip-networking", f"--log-error={self.err_log}",
             f"--pid-file={os.path.join(self.datadir, 'pid')}"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        deadline = time.time() + 15
        ready = False
        while time.time() < deadline:
            if os.path.exists(self.sock):
                r = subprocess.run(
                    ["mariadb", f"--socket={self.sock}", "-uroot",
                     "-e", "SELECT 1"],
                    capture_output=True,
                )
                if r.returncode == 0:
                    ready = True
                    break
            time.sleep(0.05)
        if not ready:
            raise RuntimeError("mariadbd did not become ready")
        subprocess.run(
            ["mariadb", f"--socket={self.sock}", "-uroot", "-e",
             "CREATE DATABASE webc"],
            check=True, capture_output=True, timeout=30,
        )

    def start_webc(self, port):
        env = dict(os.environ)
        env["HOME"] = self.tmp
        env["WEBC_DB"] = (
            "mysql:user=root;password=;database=webc;socket=" + self.sock
        )
        log = open(self.webc_log, "w")
        try:
            self.webc = subprocess.Popen(
                [WEBC_BIN, "serve", str(port)],
                cwd=PROJECT, env=env,
                stdout=log, stderr=subprocess.STDOUT,
            )
        finally:
            log.close()

    def stop(self):
        for proc in (self.webc, self.daemon):
            if proc is not None and proc.poll() is None:
                proc.terminate()
        for proc in (self.webc, self.daemon):
            if proc is not None and proc.poll() is None:
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()

    def dump(self, path, label, lines=25):
        if not os.path.exists(path):
            return
        try:
            with open(path) as f:
                tail = f.read().splitlines()[-lines:]
        except OSError:
            return
        if tail:
            print(f"--- {label} (tail) ---")
            print("\n".join(tail))


def run_checks(t, c, env):
    # ---- pages + fresh-install migration path ---------------------------
    t.chk("home", c.get("/")[0], 200)
    t.chk("users page", c.get("/users")[0], 200)
    t.chk("notes page", c.get("/notes")[0], 200)
    t.chk("fresh install: history rows",
          env.q("SELECT COUNT(*) FROM Migrations"), "5")
    t.chk("notes table exists", env.q("SHOW TABLES LIKE 'notes'"), "notes")
    t.chk("users table exists", env.q("SHOW TABLES LIKE 'users'"), "users")

    # ---- users: create (multipart, no file -> '') -----------------------
    st, _, _ = c.post_multipart(
        "/users/create",
        {"name": "MySql User", "username": "mysql_t", "email": "m@t.com",
         "profile_pic": ""},
    )
    t.chk("create no file", st, 302)
    t.chk("no file -> empty pic",
          env.q("SELECT LENGTH(profile_pic) "
                "FROM users WHERE username='mysql_t'"),
          "0")
    uid = env.q("SELECT id FROM users WHERE username='mysql_t'")
    t.chk("uuid id length", len(uid), 36)

    st, body, _ = c.get("/users")
    t.chk("list shows username", body.decode().count("mysql_t"), 1)

    st, body, _ = c.get(f"/users/{uid}/edit")
    t.chk("edit page", st, 200)
    t.chk("edit shows id", body.decode().count(uid) >= 1, True)

    st, _, _ = c.post_multipart(
        f"/users/{uid}/update",
        {"name": "Renamed MySql", "username": "mysql_t", "email": "m2@t.com",
         "profile_pic": ""},
    )
    t.chk("update", st, 302)
    t.chk("update renamed",
          env.q("SELECT name FROM users WHERE id='%s'" % uid),
          "Renamed MySql")

    # ---- browser-style empty file part (filename="") -> NULL -----------
    st, _, _ = c.post_multipart(
        "/users/create",
        {"name": "Raw Empty", "username": "mysql_nf", "email": "n@t.com"},
        files={"profile_pic": ("", b"", "application/octet-stream")},
    )
    t.chk("create empty file part", st, 302)
    t.chk("empty file part -> NULL pic",
          env.q("SELECT LENGTH(profile_pic) "
                "FROM users WHERE username='mysql_nf'"),
          "NULL")

    # ---- delete --------------------------------------------------------
    st, _, _ = c.req("POST", f"/users/{uid}/delete")
    t.chk("user delete", st, 302)
    t.chk("user row gone",
          env.q("SELECT COUNT(*) FROM users WHERE id='%s'" % uid), "0")

    # ---- notes: urlencoded form CRUD ------------------------------------
    st, _, _ = c.post_urlencoded("/notes/create", {"title": "nt", "body": "nb"})
    t.chk("notes create", st, 302)
    nid = env.q("SELECT id FROM notes WHERE title='nt'")
    t.chk("note uuid length", len(nid), 36)

    st, body, _ = c.get(f"/notes/{nid}/edit")
    t.chk("note edit page", st, 200)

    st, _, _ = c.post_urlencoded(f"/notes/{nid}/update",
                                 {"title": "nt2", "body": "nb2"})
    t.chk("note update", st, 302)
    t.chk("note updated",
          env.q("SELECT title FROM notes WHERE id='%s'" % nid), "nt2")

    # ---- JSON API -------------------------------------------------------
    st, data, _ = c.get("/api/notes")
    t.chk("api GET", st, 200)
    notes = json.loads(data)
    t.chk("api ids are strings",
          all(isinstance(n["id"], str) for n in notes), True)

    st, _, _ = c.post_json("/api/notes", {"title": "api_t", "body": "api_b"})
    t.chk("api POST", st, 200)
    api_id = env.q("SELECT id FROM notes WHERE title='api_t'")
    t.chk("api POST row", len(api_id), 36)

    st, _, _ = c.req(
        "PUT", "/api/notes",
        json.dumps({"id": api_id, "title": "api_t3", "body": "api_b3"}).encode(),
        {"Content-Type": "application/json"},
    )
    t.chk("api PUT", st, 200)
    t.chk("api PUT updated",
          env.q("SELECT title FROM notes WHERE id='%s'" % api_id), "api_t3")

    st, _, _ = c.req(
        "DELETE", "/api/notes",
        json.dumps({"id": api_id}).encode(),
        {"Content-Type": "application/json"},
    )
    t.chk("api DELETE", st, 200)
    t.chk("api DELETE gone",
          env.q("SELECT COUNT(*) FROM notes WHERE id='%s'" % api_id), "0")

    st, _, _ = c.req(
        "PATCH", "/api/notes",
        json.dumps({"id": "x"}).encode(),
        {"Content-Type": "application/json"},
    )
    t.chk("api PATCH -> 405", st, 405)

    # ---- delete leftover note row --------------------------------------
    st, _, _ = c.req("POST", f"/notes/{nid}/delete")
    t.chk("note delete route", st, 302)
    t.chk("note row gone",
          env.q("SELECT COUNT(*) FROM notes WHERE id='%s'" % nid), "0")

    # ---- /people master-detail on mysql -------------------------------
    st, body, _ = c.get("/people")
    t.chk("people page", st, 200)
    t.chk("people lists mysql user", "mysql_nf" in body.decode(), "True")

    uid2 = env.q("SELECT id FROM users WHERE username='mysql_nf'")
    st, hdr, _, _ = c.req_full(
        "POST", "/notes/create?user_id=%s&redirect=/people" % uid2,
        "title=mysql_md_note&body=via+people".encode(),
        {"Content-Type": "application/x-www-form-urlencoded"},
    )
    t.chk("md note create", f"{st} {hdr.get('Location')}", "302 /people")
    t.chk("md note linked to user",
          env.q("SELECT user_id FROM notes WHERE title='mysql_md_note'"),
          uid2)
    st, body, _ = c.get("/people")
    t.chk("md child row shown", "mysql_md_note" in body.decode(), "True")
    st, _, _ = c.req("POST",
                     "/notes/create?user_id=%s&redirect=/people" % uid2)
    t.chk("md note create empty body -> 400", st, 400)


def main():
    if not (shutil.which("mariadbd") and shutil.which("mariadb-install-db")
            and shutil.which("mariadb")):
        print("SKIP mysql_test: mariadb tooling not installed")
        return 0

    own_tmp = len(sys.argv) <= 1
    tmp = sys.argv[1] if not own_tmp else tempfile.mkdtemp(prefix="webcmysql.")
    env = MysqlEnv(tmp)
    t = Checker()
    exit_code = 1
    try:
        env.start_daemon()
        port = free_port()
        env.start_webc(port)
        c = Client("127.0.0.1", port)
        c.wait_ready(tries=80, delay=0.1)
        run_checks(t, c, env)
        exit_code = 0 if t.summary() else 1
    except Exception as exc:  # noqa: BLE001 - report + dump logs, never crash
        print(f"ERROR: {exc}")
        t.summary()
    finally:
        env.stop()

    if exit_code != 0:
        env.dump(env.err_log, "mariadbd err.log")
        env.dump(env.webc_log, "webc server log")
    if own_tmp:
        shutil.rmtree(tmp, ignore_errors=True)
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
