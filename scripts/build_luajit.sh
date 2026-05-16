#!/bin/bash

# Build LuaJIT as a Universal Binary (arm64 + x86_64) for macOS

set -e

LUAJIT_REPO="https://github.com/LuaJIT/LuaJIT.git"
LUAJIT_DIR="build/LuaJIT"
INSTALL_DIR="$(pwd)/lib/LuaJIT"

mkdir -p build
if [ ! -d "$LUAJIT_DIR" ]; then
    git clone --branch v2.1 "$LUAJIT_REPO" "$LUAJIT_DIR"
fi

cd "$LUAJIT_DIR"

export MACOSX_DEPLOYMENT_TARGET=11.0

# Ensure a clean state before starting
make clean

echo "Building LuaJIT for arm64..."
# We only need the static library (libluajit.a)
make -C src -j$(sysctl -n hw.ncpu) CFLAGS="-DLUAJIT_ENABLE_GC64" CC="clang -arch arm64" HOST_CC="clang" libluajit.a
mv src/libluajit.a src/libluajit_arm64.a

make -C src clean

echo "Building LuaJIT for x86_64..."
make -C src -j$(sysctl -n hw.ncpu) CFLAGS="-DLUAJIT_ENABLE_GC64" CC="clang -arch x86_64" HOST_CC="clang" libluajit.a
mv src/libluajit.a src/libluajit_x86_64.a

echo "Creating Universal Binary..."
lipo -create src/libluajit_arm64.a src/libluajit_x86_64.a -output libluajit_universal.a

echo "Installing to $INSTALL_DIR..."
mkdir -p "$INSTALL_DIR/include"
mkdir -p "$INSTALL_DIR/lib"

cp src/lua.h src/lualib.h src/lauxlib.h src/luaconf.h src/lua.hpp src/luajit.h "$INSTALL_DIR/include/"
cp libluajit_universal.a "$INSTALL_DIR/lib/libluajit.a"

echo "LuaJIT build complete."
lipo -info "$INSTALL_DIR/lib/libluajit.a"
