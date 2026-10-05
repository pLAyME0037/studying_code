#!/usr/bin/env bash
# webc test runner: build -> throwaway HOME -> spawn serve on free port ->
# run python suites -> teardown (test DB + test upload files removed).
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
    # remove upload files created by this run (recorded by http_test.py)
    if [ -f "$TMP/uploads.txt" ]; then
        while IFS= read -r u; do
            [ -n "$u" ] && rm -f ".$u"
        done < "$TMP/uploads.txt"
        rmdir resource/image/upload 2>/dev/null
    fi
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
# Phase 2: composite-column pages share this server + DB (needs http_test's
# rows to be in place first, and cleans up its own rows as it goes).
python3 test/pos_test.py "$HOST" "$PORT" "$TMP" || rc=$?
# Phase 11: storefront + checkout + auth suite runs LAST - the guest users
# and web orders it creates must not disturb earlier suites' counts.
python3 test/shop_test.py "$HOST" "$PORT" "$TMP" || rc=$?

if ! wait "$MYSQL_PID"; then
    rc=1
fi
if ! wait "$HTTPS_PID"; then
    rc=1
fi
cat "$MYSQL_LOG"
cat "$HTTPS_LOG"

if [ "$rc" -ne 0 ]; then
    echo "--- server.log (tail) ---"
    tail -40 "$TMP/server.log"
fi
exit "$rc"
