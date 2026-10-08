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
    t.chk("version page", c.get("/version")[0], 200)
    t.chk("fresh install: empty migration history",
          env.q("SELECT COUNT(*) FROM Migrations"), "0")
    t.chk("bootstrap Migrations table exists",
          env.q("SHOW TABLES LIKE 'Migrations'"), "Migrations")
    t.chk("no demo tables", env.q("SHOW TABLES LIKE 'notes'"), "")
    t.chk("no demo tables (users)", env.q("SHOW TABLES LIKE 'users'"), "")

    # ---- demo routes are gone on the mysql dialect too -------------------
    for route in ("/notes", "/users", "/people", "/api/notes"):
        t.chk(f"removed route {route}", c.get(route)[0], 404)


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
