# Testing

How the app runs against a database nobody cares about.

## The one rule

> **No test can touch a real database.**

`test/run.sh` exports a throwaway `HOME` before anything starts, so the
default `~/.sqlite3/webc/db` is never opened. If a test ever reaches your real
data, the harness is broken — treat that as a bug in the harness first.

## `test/run.sh`

```bash
./test/run.sh              # build, then run everything
SKIP_BUILD=1 ./test/run.sh # reuse the current build/bin/webc
```

### What it does, in order

1. **Build**, unless `SKIP_BUILD` is set. Failure prints the last 30 lines of
   `/tmp/webc_test_build.log` and exits 1 — you never get a red test run
   caused by a stale binary without being told.
2. **Isolate.** `TMP=$(mktemp -d /tmp/webc_test.XXXXXX)`, then:
   ```bash
   export HOME="$TMP"
   export WEBC_DB="$TMP/nested/deep/db"   # recursive mkdir is part of the test
   ```
   `HOME` is redirected because that is where the default DB path comes from.
   `WEBC_DB` deliberately points at a path whose parents do not exist, so
   directory creation is exercised on every run.
3. **Pick a free port** by binding a socket to port 0 from Python — no fixed
   port, so runs cannot collide with each other or with a running server.
4. **Start the server**: `./build/bin/webc serve "$PORT" > "$TMP/server.log"`.
5. **Run the suites** (see below).
6. **`trap cleanup EXIT`** — always tears down, even on failure:
   - kill the server
   - remove every upload file recorded by the suite in `$TMP/uploads.txt`
   - `rmdir resource/image/upload`
   - `rm -rf "$TMP"`

### Suite orchestration

```bash
python3 test/mysql_test.py "$TMP"  > "$TMP/mysql_test.log"  2>&1 &   # background
python3 test/https_test.py "$TMP"  > "$TMP/https_test.log"  2>&1 &   # background
python3 test/http_test.py "$HOST" "$PORT" "$TMP"                     # foreground
wait "$MYSQL_PID"; wait "$HTTPS_PID"
cat "$TMP/mysql_test.log" "$TMP/https_test.log"
```

The SQLite suite runs against the shared server on the foreground; the dialect
and TLS suites run **in parallel**, each with its **own** server, `mariadbd` or
cert. All state lives under `$TMP`, so the single cleanup trap covers all
three.

Exit code is sticky — any suite failing sets `rc=1` and the other suites still
finish. You get every failure in one run rather than one at a time.

## The suites

| Suite | Server | Backend | Skips when |
|---|---|---|---|
| `http_test.py` | shared (`$HOST:$PORT`) | SQLite (`$WEBC_DB`) | never |
| `mysql_test.py` | own | throwaway `mariadbd` | MariaDB tooling missing |
| `https_test.py` | own, TLS | own throwaway SQLite DB | `openssl` CLI missing |

**Skip = exit 0 with a printed reason.** A missing optional tool is not a
failure, so the suite stays green on a machine without MariaDB or OpenSSL —
but it also means **a green run does not prove those suites ran**. Check the
logs if coverage matters.

### `http_test.py` — main suite

The bulk of coverage (~19 KB). Runs against the shared server on the chosen
free port, and records every upload file it creates into `$TMP/uploads.txt` so
the cleanup trap can delete them.

### `mysql_test.py` — dialect proof

Boots a private `mariadbd` with:

- a throwaway datadir under `$TMP`
- a unix socket (no networking)
- `--skip-networking`

then starts a webc pointed at it with
`WEBC_DB='mysql:user=root;password=;database=webc;socket=...'`, runs the HTTP
checks, and **verifies the resulting rows through the `mariadb` CLI** — so it
confirms what actually landed in the server, not what the app thinks it wrote.
Both are torn down at exit.

### `https_test.py` — TLS proof

Generates a throwaway self-signed certificate, boots its own server with
`WEBC_TLS_CERT` + `WEBC_TLS_KEY` and its own SQLite DB, then checks:

- pages load over TLS
- a form POST lands
- **plaintext on the TLS port is not served as HTTP** — the property that
  `tls_server_init()` aborting on misconfiguration is supposed to guarantee,
  verified end to end

## `test/golden/`

Golden files pin the migration SQL **byte for byte**, most importantly the
`sqlite3.sql` files.

Why this matters: an already-applied migration's exact text is stored in the
`Migrations.query` column and re-compared on every open. Editing a shipped
migration file is therefore a `SCHEMA_HISTORY_MISMATCH` for every existing
database. The golden test turns "someone tidied up a migration" into a red
test instead of a data-loss incident for users. See
[database.md](database.md).

## Layout

```
test/
  run.sh          harness: build, isolate, spawn, run, teardown
  testlib.py      Checker — assertion helper shared by the suites
  http_test.py    SQLite suite (main)
  mysql_test.py   MariaDB suite (own server + own mariadbd)
  https_test.py   TLS suite (own cert + own server)
  golden/         pinned migration files
  __pycache__/    disposable
```

## Adding a test

1. `from testlib import Checker` and follow the existing suites' shape.
2. Take `$TMP` from `sys.argv` for anything you create — never a path outside
   it, or the cleanup trap will not remove it.
3. Record created upload files in `$TMP/uploads.txt`.
4. If the test needs tooling, **skip with exit 0** and say why; do not fail.
5. If it introduces a migration, add the file *and* the `migrations[]` entry,
   then update `test/golden/`.

## What is not covered

Honest gaps, kept in [dev-log.md](dev-log.md) as well:

- **No Postgres suite** — the driver is a stub, so there is nothing to run.
- **No concurrency test.** The pool's round-robin-without-checkout behaviour
  (see [database.md](database.md)) is untested; no suite opens more than one
  request at a time.
- **`src/crud/` is untested** because it is untested *and* unreferenced — no
  caller exists to drive it.
- **No keep-alive or chunked-encoding test** — neither is implemented.
