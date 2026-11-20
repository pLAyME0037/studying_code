## Deverlopment enviroment in Arch Linux:

1. install pkg: `mingw-w64-gcc wine`
2. Initialize an isolated prefix and check Windows version:
  * WINEPREFIX="$PWD/.wine" wineboot -u
  * WINEPREFIX="$PWD/.wine" winecfg
    (verify Windows version is Windows 10)
3. Compile with -mwindows 
   (GUI subsystem for WinMain) and -static (embeds MinGW runtime).
4. Debug session is to be research.
