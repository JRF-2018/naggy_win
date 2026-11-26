# Building naggy_win

## Requirements

* Go 1.22 or later (no external modules are used).
* Visual Studio 2022 with "Desktop development with C++" (MSVC v143 and a
  Windows 10 SDK) for the IME DLLs.
* Inno Setup 6.3 or later for the installer.
* WSL (the helper scripts assume building from WSL; everything can also be
  run from a Windows command prompt).

## Go programs

```sh
go test ./...
go build ./cmd/naggy-backend          # for the current platform
GOOS=windows GOARCH=amd64 go build -o naggy-backend.exe ./cmd/naggy-backend
GOOS=windows GOARCH=amd64 go build -ldflags "-H windowsgui" -o naggy-server.exe ./cmd/naggy-server
```

## IME DLLs

```bat
ime\build.bat all      rem -> ime\out\x64\naggy_ime.dll, ime\out\x86\naggy_ime.dll
```

`build.bat` must be run from a Windows path (not \\wsl$\...).
`tools/stage.sh` copies the sources to %TEMP% and builds there.

Register manually for development (as administrator):

```bat
regsvr32 ime\out\x64\naggy_ime.dll
regsvr32 ime\out\x86\naggy_ime.dll   (from %SystemRoot%\SysWOW64\regsvr32)
```

The DLL looks for `naggy-server.exe` in its own directory or its parent
directory (or the `ServerPath` registry value).

## Installer

```sh
tools/stage.sh                       # builds everything into installer/stage
cmd.exe /c installer\build_installer.bat
```

## Tests

* `go test ./...` - unit tests (escape, init-file language, translit).
* `tools/compare_backends.py DIR "perl naggy-backend.pl" "./naggy-backend"`
  compares the Perl and Go backends response by response (thousands of
  generated conversions).  DIR needs site-init.nginit and the dictionaries
  (with SDBM indexes for the Perl version).  The Perl version needs
  Unicode::Japanese (the pure-Perl part works: `PERL5LIB=.../lib`).
* `ime/test/engine_test.cpp` - runs the IME state machine on Linux against
  naggy-backend:

  ```sh
  g++ -std=c++17 -o engine_test ime/test/engine_test.cpp ime/src/Engine.cpp ime/src/Protocol.cpp
  ./engine_test ./naggy-backend DIR "aka:kou f" "nihongo<MUHENKAN>"
  ```

## Regenerating tables

* `internal/jenc/jtables.bin`: `python3 tools/gen_jtables.py internal/jenc/jtables.bin`
* `internal/ujconv/tables.go`: run `tools/dump_unicode_japanese.pl` with
  Unicode::Japanese, then `python3 tools/gen_ujtables.py tools/unicode_japanese_tables.txt internal/ujconv/tables.go`.
* `ime/res/naggy.ico`: `python3 tools/make_icon.py ime/res/naggy.ico`
