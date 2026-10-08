#!/usr/bin/env bash
# webc test runner: build -> throwaway HOME -> spawn serve on free port ->
# run python suites -> teardown (throwaway test DB removed).
# Never touches the real ~/.sqlite3/webc/db.
set -uo pipefail
cd "$(dirname "$0")/.."

if [ -z "${SKIP_BUILD:-}" ]; then
    if ! ./build/bin/nob > /tmp/webc_test_build.log 2>&1; then
        echo "BUILD FAILED:"
        tail -30 /tmp/webc_test_build.log
        exit 1
    fi
fi

TMP="$(mktemp -d /tmp/webc_test.XXXXXX)"
export HOME="$TMP"
export WEBC_DB="$TMP/nested/deep/db"   # explicit db location; exercises recursive mkdir
HOST=127.0.0.1
PORT="$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()')"
SERVER_PID=""

cleanup() {
    [ -n "$SERVER_PID" ] && kill "$SERVER_PID" 2>/dev/null
    rm -rf "$TMP"
}
trap cleanup EXIT

./build/bin/webc serve "$PORT" > "$TMP/server.log" 2>&1 &
SERVER_PID=$!

rc=0
MYSQL_LOG="$TMP/mysql_test.log"
HTTPS_LOG="$TMP/https_test.log"
# Dialect + TLS suites run alongside the sqlite suite (own mariadbd / cert /
# server each); their state lives under $TMP so the cleanup trap removes it.
python3 test/mysql_test.py "$TMP" > "$MYSQL_LOG" 2>&1 &
MYSQL_PID=$!
python3 test/https_test.py "$TMP" > "$HTTPS_LOG" 2>&1 &
HTTPS_PID=$!

python3 test/http_test.py "$HOST" "$PORT" "$TMP" || rc=$?

# DB Admin feature suites (sequential: they share the server and mutate its
# active-database state).
DBADMIN_LOG="$TMP/dbadmin_test.log"
python3 test/dbadmin_test.py "$HOST" "$PORT" "$TMP" > "$DBADMIN_LOG" 2>&1 || rc=$?

# Real-browser interaction suite (headless Chromium + CDP); skips itself
# when chromium/node-WebSocket are unavailable.
CLICK_LOG="$TMP/click_test.log"
python3 test/click_test.py "$HOST" "$PORT" "$TMP" > "$CLICK_LOG" 2>&1 || rc=$?

# WASI loader smoke: instantiate sqlite.wasm and run an in-memory query.
if [ -f build/sqlite.wasm ]; then
    node test/wasm_smoke.mjs || rc=$?
fi

if ! wait "$MYSQL_PID"; then
    rc=1
fi
if ! wait "$HTTPS_PID"; then
    rc=1
fi
cat "$MYSQL_LOG"
cat "$HTTPS_LOG"
cat "$DBADMIN_LOG"
cat "$CLICK_LOG"

if [ "$rc" -ne 0 ]; then
    echo "--- server.log (tail) ---"
    tail -40 "$TMP/server.log"
fi
exit "$rc"
