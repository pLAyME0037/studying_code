#!/usr/bin/env bash
# Build SQLite for browser execution with WASI SDK (Preview 1, reactor ABI).
set -euo pipefail
cd "$(dirname "$0")"

WASI_SDK_PATH="${WASI_SDK_PATH:-/opt/wasi-sdk}"
CC="$WASI_SDK_PATH/bin/clang"
if [ ! -x "$CC" ]; then
    echo "WASI SDK not found: $CC (set WASI_SDK_PATH)" >&2
    exit 1
fi

"$CC" -O2 -mexec-model=reactor \
    -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION \
    -DSQLITE_OMIT_DEPRECATED -DSQLITE_DEFAULT_MEMSTATUS=0 \
    -Wl,-z,stack-size=1048576 -Wl,--max-memory=1073741824 \
    -Wl,--export=malloc -Wl,--export=free \
    -Wl,--export=sqlite3_malloc -Wl,--export=sqlite3_free \
    -Wl,--export=sqlite3_open -Wl,--export=sqlite3_close \
    -Wl,--export=sqlite3_deserialize \
    -Wl,--export=sqlite3_prepare_v2 -Wl,--export=sqlite3_step \
    -Wl,--export=sqlite3_column_text -Wl,--export=sqlite3_finalize \
    -Wl,--export=sqlite3_column_count -Wl,--export=sqlite3_column_name \
    -Wl,--export=sqlite3_bind_text -Wl,--export=sqlite3_bind_int64 \
    -Wl,--export=sqlite3_bind_double -Wl,--export=sqlite3_bind_null \
    -Wl,--export=sqlite3_changes \
    -Wl,--export=sqlite3_serialize -Wl,--export=sqlite3_exec \
    -Wl,--export=sqlite3_stmt_readonly \
    -Wl,--export=sqlite3_get_autocommit \
    -Wl,--export=sqlite3_errmsg \
    -o build/sqlite.wasm module/sqlite-amalgamation-3460100/sqlite3.c

echo "Built build/sqlite.wasm"
