#!/bin/bash

# Build a local Apple Silicon release for an M5 Max host.
# This intentionally produces arm64 only; the universal distribution build
# remains available through scripts/deploy.sh.

set -Eeuo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROJECT_NAME="FlyWithLua-Mac"
TARGET_NAME="FlyWithLua-Mac"
CONFIGURATION="Release"
DERIVED_DATA_DIR="${FLYWITHLUA_M5MAX_DERIVED_DATA:-$REPO_ROOT/build/DerivedData-M5Max}"

if [ "$(uname -m)" != "arm64" ]; then
    echo "Error: build_m5max.sh must run natively on an arm64 macOS host." >&2
    exit 1
fi

if ! command -v xcodegen >/dev/null 2>&1; then
    echo "Error: xcodegen is required." >&2
    exit 1
fi

cd "$REPO_ROOT"
mkdir -p "$DERIVED_DATA_DIR"

# project.yml is the source of truth for this generated project.
xcodegen generate

BUILD_LOG="$DERIVED_DATA_DIR/build.log"
run_build() {
    xcodebuild \
        -project "$PROJECT_NAME.xcodeproj" \
        -scheme "$TARGET_NAME" \
        -configuration "$CONFIGURATION" \
        -sdk macosx \
        -destination "platform=macOS,arch=arm64" \
        -derivedDataPath "$DERIVED_DATA_DIR" \
        ARCHS=arm64 \
        ONLY_ACTIVE_ARCH=YES \
        GCC_OPTIMIZATION_LEVEL=3 \
        DEAD_CODE_STRIPPING=YES \
        SWIFT_OPTIMIZATION_LEVEL=-O \
        SWIFT_COMPILATION_MODE=wholemodule \
        CODE_SIGN_IDENTITY="" \
        CODE_SIGNING_ALLOWED=NO \
        CODE_SIGNING_REQUIRED=NO \
        -quiet \
        build >"$BUILD_LOG" 2>&1
}

set +e
BUILD_STATUS=143
attempt=1
while [ "$attempt" -le 3 ]; do
    run_build
    BUILD_STATUS=$?
    if [ "$BUILD_STATUS" -eq 0 ]; then
        break
    fi
    if [ "$BUILD_STATUS" -ne 143 ] || [ "$attempt" -eq 3 ]; then
        break
    fi
    echo "xcodebuild was terminated by the host; retrying ($((attempt + 1))/3)..." >&2
    sleep 2
    attempt=$((attempt + 1))
done
set -e
if [ "$BUILD_STATUS" -ne 0 ]; then
    echo "Error: arm64 build failed; last log lines:" >&2
    tail -80 "$BUILD_LOG" >&2
    exit "$BUILD_STATUS"
fi

BUILD_PATH="$(find "$DERIVED_DATA_DIR/Build/Products" -type d -path "*/$CONFIGURATION/FlyWithLua.xpl" -print -quit)"
if [ -z "$BUILD_PATH" ]; then
    echo "Error: built FlyWithLua.xpl was not found under $DERIVED_DATA_DIR." >&2
    exit 1
fi

BINARY="$BUILD_PATH/Contents/MacOS/FlyWithLua"
METALLIB="$BUILD_PATH/Contents/Resources/default.metallib"
if [ ! -f "$BINARY" ]; then
    echo "Error: built bundle has no plugin binary: $BINARY" >&2
    exit 1
fi
if [ ! -s "$METALLIB" ]; then
    echo "Error: built bundle has no non-empty Metal library: $METALLIB" >&2
    exit 1
fi

xcrun lipo "$BINARY" -verify_arch arm64
echo "M5 Max arm64 build ready: $BUILD_PATH"
echo "Metal library verified: $METALLIB"
