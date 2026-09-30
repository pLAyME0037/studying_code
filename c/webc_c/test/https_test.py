#!/usr/bin/env python3
"""HTTPS suite for webc (spawned by test/run.sh in parallel with the rest).

Generates a throwaway self-signed cert, boots webc with WEBC_TLS_CERT +
WEBC_TLS_KEY against its own throwaway sqlite DB, and verifies TLS-only
serving: pages load, a form POST lands, and plaintext on the TLS port is
not served as HTTP. Skips (exit 0) when the openssl CLI is missing.
"""
import http.client
import os
import shutil
import socket
import sqlite3
import ssl
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from testlib import Checker  # noqa: E402

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEBC_BIN = os.path.join(PROJECT, "build", "bin", "webc")


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


class TlsClient:
    """Minimal HTTPS client (unverified: the test cert is self-signed)."""

    def __init__(self, host, port):
        ctx = ssl._create_unverified_context()
        self.conn = http.client.HTTPSConnection(
            host, port, context=ctx, timeout=30
        )

    def req(self, method, path, body=None, headers=None):
        self.conn.request(method, path, body=body, headers=headers or {})
        resp = self.conn.getresponse()
        data = resp.read()
        return resp.status, data, resp.getheader("Content-Type", "")

    def get(self, path):
        return self.req("GET", path)

    def post_urlencoded(self, path, fields):
        import urllib.parse
        body = urllib.parse.urlencode(fields).encode()
        return self.req(
            "POST", path, body,
            {"Content-Type": "application/x-www-form-urlencoded"},
        )

    def wait_ready(self, tries=60, delay=0.1):
        for _ in range(tries):
            try:
                if self.get("/")[0] == 200:
                    return
            except Exception:  # noqa: BLE001 - any failure = server not ready
                # A failed attempt can leave http.client in Request-sent
                # state, which poisons the next request(): start fresh.
                self.conn.close()
                ctx = ssl._create_unverified_context()
                self.conn = http.client.HTTPSConnection(
                    self.conn.host, self.conn.port, context=ctx, timeout=30
                )
            time.sleep(delay)
        raise RuntimeError("TLS server not ready")


def plain_http_probe(port):
    """Send plaintext HTTP to the TLS port; returns whatever comes back."""
    try:
        s = socket.create_connection(("127.0.0.1", port), timeout=2)
        try:
            s.sendall(b"GET / HTTP/1.1\r\nHost: t\r\n\r\n")
            return s.recv(64)
        finally:
            s.close()
    except OSError:
        return b""


def main():
    if not shutil.which("openssl"):
        print("SKIP https_test: openssl CLI not installed")
        return 0

    own_tmp = len(sys.argv) <= 1
    tmp = sys.argv[1] if not own_tmp else tempfile.mkdtemp(prefix="webctls.")
    cert = os.path.join(tmp, "cert.pem")
    key = os.path.join(tmp, "key.pem")
    db = os.path.join(tmp, "https", "db")
    log_path = os.path.join(tmp, "https_webc.log")

    t = Checker()
    server = None
    exit_code = 1
    try:
        subprocess.run(
            ["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
             "-days", "2", "-subj", "/CN=localhost",
             "-keyout", key, "-out", cert],
            check=True, capture_output=True, timeout=60,
        )

        port = free_port()
        env = dict(os.environ)
        env["HOME"] = tmp
        env["WEBC_DB"] = db
        env["WEBC_TLS_CERT"] = cert
        env["WEBC_TLS_KEY"] = key
        log = open(log_path, "w")
        try:
            server = subprocess.Popen(
                [WEBC_BIN, "serve", str(port)],
                cwd=PROJECT, env=env,
                stdout=log, stderr=subprocess.STDOUT,
            )
        finally:
            log.close()

        c = TlsClient("127.0.0.1", port)
        c.wait_ready()

        # ---- pages over TLS --------------------------------------------
        t.chk("https home", c.get("/")[0], 200)
        t.chk("https users page", c.get("/users")[0], 200)
        t.chk("https notes page", c.get("/notes")[0], 200)

        # ---- form POST over TLS writes to the DB -----------------------
        st, _, _ = c.post_urlencoded(
            "/notes/create", {"title": "tls_note", "body": "over tls"})
        t.chk("https form create", st, 302)
        conn = sqlite3.connect(db)
        row = conn.execute(
            "SELECT title FROM notes WHERE title='tls_note'").fetchone()
        conn.close()
        t.chk("https post row", row[0] if row else None, "tls_note")

        # ---- plaintext must not speak HTTP on the TLS port -------------
        first = plain_http_probe(port)
        t.chk("plaintext not served as HTTP",
              first.startswith(b"HTTP/1."), False)

        exit_code = 0 if t.summary() else 1
    except Exception as exc:  # noqa: BLE001 - report + dump log, never crash
        print(f"ERROR: {exc}")
        t.summary()
    finally:
        if server is not None:
            if server.poll() is None:
                server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()

    if exit_code != 0 and os.path.exists(log_path):
        with open(log_path) as f:
            tail = f.read().splitlines()[-25:]
        if tail:
            print("--- webc server log (tail) ---")
            print("\n".join(tail))
    if own_tmp:
        shutil.rmtree(tmp, ignore_errors=True)
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
