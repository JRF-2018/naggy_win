#!/bin/bash
# Builds everything and assembles installer/stage (run on WSL).
#   tools/stage.sh            build Go binaries + IME DLLs, stage files
# Requirements: Go (cross compiling), Visual Studio 2022 C++ on Windows.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
GO=${GO:-go}
WINTMP=$(wslpath "$(cmd.exe /c 'echo %TEMP%' 2>/dev/null | tr -d '\r')")
WBUILD="$WINTMP/naggy_win_build"
STAGE="$ROOT/installer/stage"

rm -rf "$STAGE"
mkdir -p "$STAGE"/{amd64,386,x64,x86,dic}
cd "$ROOT"
for arch in amd64 386; do
  GOOS=windows GOARCH=$arch $GO build -trimpath -o "$STAGE/$arch/naggy-backend.exe" ./cmd/naggy-backend
  GOOS=windows GOARCH=$arch $GO build -trimpath -ldflags "-H windowsgui" -o "$STAGE/$arch/naggy-server.exe" ./cmd/naggy-server
  GOOS=windows GOARCH=$arch $GO build -trimpath -o "$STAGE/$arch/naggy-client.exe" ./cmd/naggy-client
  GOOS=windows GOARCH=$arch $GO build -trimpath -o "$STAGE/$arch/naggy-dictool.exe" ./cmd/naggy-dictool
done

# IME DLLs (MSVC needs a Windows path)
rm -rf "$WBUILD/ime"
mkdir -p "$WBUILD/ime"
cp -r ime/build.bat ime/src ime/res ime/config "$WBUILD/ime/"
(cd "$WBUILD/ime" && cmd.exe /c "$(wslpath -w "$WBUILD/ime/build.bat") all" | tr -d '\r' | grep -E "error|Built" || true)
cp "$WBUILD/ime/out/x64/naggy_ime.dll" "$STAGE/x64/"
cp "$WBUILD/ime/out/x86/naggy_ime.dll" "$STAGE/x86/"
cp "$WBUILD/ime/out/x64/naggy-config.exe" "$STAGE/amd64/"
cp "$WBUILD/ime/out/x86/naggy-config.exe" "$STAGE/386/"

cp -r data/nginit data/trl "$STAGE/"
cp data/bushu-skk-dic.txt "$STAGE/dic/"
cp installer/naggy-server.ini.sample ime/res/naggy.ico "$STAGE/"
cp README.md "$STAGE/README.md"
cp LICENSE "$STAGE/LICENSE.txt"
echo "staged into $STAGE"
