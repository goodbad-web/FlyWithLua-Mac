#!/bin/bash

# Build LuaJIT as a Universal Binary (arm64 + x86_64) for macOS

set -Eeuo pipefail

LUAJIT_REPO="https://github.com/LuaJIT/LuaJIT.git"
LUAJIT_COMMIT="c6ffc141a8762b41703f9287d63d93622a13dd8f"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LUAJIT_DIR="$REPO_ROOT/build/LuaJIT"
INSTALL_DIR="$REPO_ROOT/lib/LuaJIT"

mkdir -p "$REPO_ROOT/build"
if [ ! -d "$LUAJIT_DIR/.git" ]; then
	git clone "$LUAJIT_REPO" "$LUAJIT_DIR"
fi

if ! git -C "$LUAJIT_DIR" cat-file -e "$LUAJIT_COMMIT^{commit}" 2>/dev/null; then
	git -C "$LUAJIT_DIR" fetch --depth 1 origin "$LUAJIT_COMMIT"
fi
git -C "$LUAJIT_DIR" checkout --detach "$LUAJIT_COMMIT"

cd "$LUAJIT_DIR"

export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-11.0}"

# Ensure a clean state before starting
make clean

echo "Building LuaJIT for arm64..."
# We only need the static library (libluajit.a)
make -C src -j"$(sysctl -n hw.ncpu)" CFLAGS="-DLUAJIT_ENABLE_GC64" CC="clang -arch arm64" HOST_CC="clang" libluajit.a
mv src/libluajit.a src/libluajit_arm64.a

make -C src clean

echo "Building LuaJIT for x86_64..."
make -C src -j"$(sysctl -n hw.ncpu)" CFLAGS="-DLUAJIT_ENABLE_GC64" CC="clang -arch x86_64" HOST_CC="clang" libluajit.a
mv src/libluajit.a src/libluajit_x86_64.a

echo "Creating Universal Binary..."
lipo -create src/libluajit_arm64.a src/libluajit_x86_64.a -output libluajit_universal.a

echo "Installing to $INSTALL_DIR..."
mkdir -p "$INSTALL_DIR/include"
mkdir -p "$INSTALL_DIR/lib"

cp src/lua.h src/lualib.h src/lauxlib.h src/luaconf.h src/lua.hpp src/luajit.h "$INSTALL_DIR/include/"
cp libluajit_universal.a "$INSTALL_DIR/lib/libluajit.a"

echo "LuaJIT build complete."
ARCHES="$(lipo -archs "$INSTALL_DIR/lib/libluajit.a")"
case " $ARCHES " in
  *" arm64 "*) ;;
  *)
    echo "Error: LuaJIT universal library is missing arm64: $ARCHES" >&2
    exit 1
    ;;
esac
case " $ARCHES " in
  *" x86_64 "*) ;;
  *)
    echo "Error: LuaJIT universal library is missing x86_64: $ARCHES" >&2
    exit 1
    ;;
esac
lipo -info "$INSTALL_DIR/lib/libluajit.a"
