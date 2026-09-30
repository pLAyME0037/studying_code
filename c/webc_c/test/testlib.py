"""Shared helpers for webc HTTP tests (python3 stdlib only)."""
import http.client
import os
import sqlite3
import time
import urllib.parse


class Client:
    def __init__(self, host, port, timeout=60):
        self.host = host
        self.port = port
        self.timeout = timeout

    def req(self, method, path, body=None, headers=None, timeout=None):
        """Returns (status:int, data:bytes, content_type:str)."""
        conn = http.client.HTTPConnection(
            self.host, self.port, timeout=timeout or self.timeout
        )
        try:
            conn.request(method, path, body=body, headers=headers or {})
            resp = conn.getresponse()
            data = resp.read()
            return resp.status, data, resp.getheader("Content-Type", "")
        finally:
            conn.close()

    def get(self, path):
        return self.req("GET", path)

    def post_urlencoded(self, path, fields):
        body = urllib.parse.urlencode(fields).encode()
        return self.req(
            "POST",
            path,
            body,
            {"Content-Type": "application/x-www-form-urlencoded"},
        )

    def post_json(self, path, obj):
        import json

        body = json.dumps(obj).encode()
        return self.req(
            "POST", path, body, {"Content-Type": "application/json"}
        )

    def post_multipart(self, path, fields, files=None, extra_headers=None):
        body, hdrs = multipart_body(fields, files)
        if extra_headers:
            hdrs.update(extra_headers)
        return self.req("POST", path, body, hdrs)

    def wait_ready(self, tries=50, delay=0.1):
        last = None
        for _ in range(tries):
            try:
                status, _, _ = self.get("/")
                if status == 200:
                    return True
                last = f"HTTP {status}"
            except OSError as e:
                last = str(e)
            time.sleep(delay)
        raise RuntimeError(f"server not ready: {last}")


def multipart_body(fields, files=None):
    """Builds multipart/form-data. files: {name: (filename, bytes, content_type)}.

    filename="" produces the browser-style empty file part.
    Returns (body:bytes, headers:dict).
    """
    boundary = "----webctest" + os.urandom(8).hex()
    out = b""
    for key, value in fields.items():
        out += (
            f'--{boundary}\r\nContent-Disposition: form-data; name="{key}"\r\n'
            f"\r\n{value}\r\n"
        ).encode()
    for key, (filename, data, ctype) in (files or {}).items():
        out += (
            f'--{boundary}\r\nContent-Disposition: form-data; name="{key}"; '
            f'filename="{filename}"\r\nContent-Type: {ctype}\r\n\r\n'
        ).encode()
        out += data + b"\r\n"
    out += f"--{boundary}--\r\n".encode()
    return out, {"Content-Type": f"multipart/form-data; boundary={boundary}"}


def db_query(db_path, sql, args=()):
    """One-shot scalar query against the test DB (WAL-safe, busy retry)."""
    conn = sqlite3.connect(db_path, timeout=10)
    try:
        conn.execute("PRAGMA busy_timeout=5000")
        row = conn.execute(sql, args).fetchone()
        conn.commit()
        return row[0] if row else None
    finally:
        conn.close()


def db_exec(db_path, sql, args=()):
    conn = sqlite3.connect(db_path, timeout=10)
    try:
        conn.execute("PRAGMA busy_timeout=5000")
        conn.execute(sql, args)
        conn.commit()
    finally:
        conn.close()


class Checker:
    def __init__(self):
        self.passed = 0
        self.failed = 0

    def chk(self, name, got, want):
        if str(got) == str(want):
            self.passed += 1
            print(f"PASS: {name} ({got})")
        else:
            self.failed += 1
            print(f"FAIL: {name} got=[{got}] want=[{want}]")

    def summary(self):
        print(f"=== {self.passed} passed, {self.failed} failed ===")
        return self.failed == 0
