#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT="$(cd -P -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
cd "$PROJECT_ROOT"

export WINEPREFIX="${WINEPREFIX:-$PROJECT_ROOT/.wine}"
export WINEDEBUG="${WINEDEBUG:-fixme-all,warn+debugstr,warn+seh}"

SRC_FILE="$PROJECT_ROOT/code/handmade.cpp"
OUT_DIR="$PROJECT_ROOT/build"
OUT_EXE="$OUT_DIR/handmade.exe"
LIBRARY=(-luser32 -lgdi32)
GDB_PORT="${WINE_GDB_PORT:-31337}"

ensure_wineprefix() {
    if [ ! -d "$WINEPREFIX/drive_c" ]; then
        echo "Initializing Wine prefix at $WINEPREFIX..."
        wineboot -u
    fi
}

compile_binary() {
    local extra_ld_flags=("$@")
    mkdir -p "$OUT_DIR"
    rm -f "$OUT_DIR/handmade.exe.so"

    x86_64-w64-mingw32-g++ \
        -ggdb \
        -gdwarf-4 \
        -O0 \
        -mwindows \
        -static \
        ${extra_ld_flags[@]+"${extra_ld_flags[@]}"} \
        -o "$OUT_EXE" \
        "$SRC_FILE" \
        "${LIBRARY[@]}"
}

mode="${1:-}"

case "$mode" in
d)
    ensure_wineprefix
    compile_binary "-Wl,--disable-dynamicbase"
    echo "Built target: $OUT_EXE"
    ;;
"")
    ensure_wineprefix
    compile_binary "-Wl,--disable-dynamicbase"
    exec wine "$OUT_EXE"
    ;;
*)
    echo "Usage: $0 [build-debug|debug|warm]" >&2
    exit 1
    ;;
esac
