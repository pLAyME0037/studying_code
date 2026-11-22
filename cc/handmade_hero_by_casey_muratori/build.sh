#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT="$(cd -P -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
cd "$PROJECT_ROOT"

export WINEPREFIX="${WINEPREFIX:-$PROJECT_ROOT/.wine}"
export WINEDEBUG="${WINEDEBUG:-fixme-all,warn+debugstr,warn+seh}"

SRC_FILE="$PROJECT_ROOT/handmade.cpp"
OUT_DIR="$PROJECT_ROOT/build"
OUT_EXE="$OUT_DIR/handmade.exe"
LIBRARY="-user32.lib -Gdi32.lib"
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
        -Wall \
        -Wextra \
        -ggdb \
        -gdwarf-4 \
        -O0 \
        -mwindows \
        -static \
        "${extra_ld_flags[@]}" \
        -o "$OUT_EXE" \
        "$SRC_FILE" \
        "$LIBRARY"
}

check_port_free() {
    local port="$1"
    if command -v ss >/dev/null 2>&1; then
        if ss -ltn "( sport = :$port )" | grep -q ":$port"; then
            echo "Error: TCP port $port is already in use. Set WINE_GDB_PORT to another port." >&2
            exit 1
        fi
    fi
}

mode="${1:-}"

case "$mode" in
build-debug)
    ensure_wineprefix
    compile_binary "-Wl,--disable-dynamicbase"
    echo "Built debug target: $OUT_EXE"
    ;;
debug|--debug|-d)
    ensure_wineprefix
    compile_binary "-Wl,--disable-dynamicbase"
    check_port_free "$GDB_PORT"

    if [ "${WINE_USE_GDBSERVER:-0}" = "1" ]; then
        if ! command -v gdbserver.exe >/dev/null 2>&1; then
            echo "Error: WINE_USE_GDBSERVER=1 set, but gdbserver.exe was not found in PATH." >&2
            exit 1
        fi
        echo "Starting MinGW gdbserver.exe on 127.0.0.1:$GDB_PORT..."
        exec wine gdbserver.exe "127.0.0.1:$GDB_PORT" "$OUT_EXE"
    fi

    echo "Starting winedbg GDB server on 127.0.0.1:$GDB_PORT..."
    exec winedbg --gdb --no-start --port "$GDB_PORT" "$OUT_EXE"
    ;;
warm)
    ensure_wineprefix
    echo "Starting persistent wineserver (600s keep-alive)..."
    wineserver -p 600
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
