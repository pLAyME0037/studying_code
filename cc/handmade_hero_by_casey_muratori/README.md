## Development Environment in Arch Linux

1. Install packages:
   ```bash
   sudo pacman -S mingw-w64-gcc wine gdb
   ```
   Optional (faster native Windows gdbserver under Wine, removes `winedbg --gdb`
   proxy overhead):
   install `mingw-w64-gdb` from AUR and set `WINE_USE_GDBSERVER=1`.

2. Initialize the isolated Wine prefix and check Windows version:
   ```bash
   WINEPREFIX="$PWD/.wine" wineboot -u
   WINEPREFIX="$PWD/.wine" winecfg
   ```
   Verify Windows version is set to Windows 10.

3. Build & run:
   - `./build.sh` compiles `build/handmade.exe` 
     (`-gdwarf-4 -O0 -mwindows -static -Wl,--disable-dynamicbase`) and runs it
     under Wine.
   - `./build.sh warm` keeps `wineserver` alive for 600s to remove cold-boot
     latency between debug runs.
   - `./build.sh debug` starts `winedbg --gdb --no-start --port 31337`
     (or `gdbserver.exe` if `WINE_USE_GDBSERVER=1`)
     with `WINEDEBUG=fixme-all,warn+debugstr,warn+seh`.

4. Neovim (`nvim-dap`) Debug Workflow:
   - Press `<F5>` and pick:
     - `Wine: Build & Debug (winedbg + live output)`: compiles automatically,
       opens a `Wine Debug Output` split capturing `OutputDebugString`, `stdout`,
       `stderr`, and Wine SEH channels, and attaches `gdb --interpreter=dap`.
     - `Wine: Attach to running winedbg (127.0.0.1:31337)`: attaches to an
       external `./build.sh debug` session.
     - `Linux: Launch Executable (gdb)` or `(codelldb)`: debugs native Linux ELF
       binaries with the exact same tools.
   - Keymaps:
     - `<F9>` toggle breakpoint, `<F10>` step over, `<F11>` step into, `<F8>` step out.
     - `<leader>du` toggle DAP View (`scopes`, `watches`, `threads`,
       `breakpoints`, `console`, `repl`, `disassembly`).
     - `<leader>do` toggle the `Wine Debug Output` split.
     - `<leader>dw` / `Q` add expression to Watches or peek value.
     - `<leader>dp` print `&var` / `obj.field` / `ptr->member` address, target
       pointer, and value (`m` inspect `&expr`, `p` inspect target, `s`/`S`
       search virtual memory).
     - `<leader>dm` inspect memory (`f`/`<Tab>` cycles `dec -> hex -> bin`, `x`
       toggles hex/dec dump bytes, `a` toggles hex/dec addresses, `s` searches
       virtual memory for current address).
     - `<leader>dR` virtual memory tools (full/fast virtual-memory search for
       pointer, integer, UTF-8, UTF-16LE, or hex pattern; `vmmap`; region lookup).
     - `<leader>ds` interleaved Source + ASM popup (`f`/`<Tab>` cycles
       `dec -> hex -> bin` machine-code bytes,
       `a` toggles hex/dec addresses,
       `r` refreshes at current PC).
     - `<leader>da` toggle instruction-granularity stepping,
       `<leader>dh` toggle global Hex/Decimal display.

5. Verification test:
   ```bash
   ./tests/verify_debug_session.sh
   ```
   Requires an active display (`DISPLAY` or Wine headless virtual desktop) since
   `handmade.cpp` invokes `MessageBoxA` after line 30.
