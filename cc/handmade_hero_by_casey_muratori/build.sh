#!/usr/bin/env bash

set -euo pipefail

export WINEPREFIX="${WINEPREFIX:-$PWD/.wine}"

mkdir -p build
rm -f build/handmade.exe.so

mode="${1:-}"

case "$mode" in
debug|--debug|-d)
    x86_64-w64-mingw32-g++ \
        -Wall \
        -Wextra \
        -ggdb \
        -gdwarf-4 \
        -O0 \
        -mwindows \
        -static \
        -o build/handmade.exe \
        handmade.cpp

    echo "Starting winedbg GDB server on 127.0.0.1:31337..."
    winedbg --gdb --no-start --port 31337 build/handmade.exe
    ;;
"")
    x86_64-w64-mingw32-g++ \
        -Wall \
        -Wextra \
        -ggdb \
        -O0 \
        -mwindows \
        -static \
        -o build/handmade.exe \
        handmade.cpp

    wine build/handmade.exe
    ;;
*)
    echo "Usage: $0 [debug]" >&2
    exit 1
    ;;
esac
