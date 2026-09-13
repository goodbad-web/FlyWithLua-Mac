#!/bin/bash

# Build, package, and safely deploy FlyWithLua-Mac.
# The active runtime is merged without deleting user files. Existing files are
# copied to a timestamped backup before they can be overwritten.

set -Eeuo pipefail

PROJECT_NAME="FlyWithLua-Mac"
TARGET_NAME="FlyWithLua-Mac"
CONFIGURATION="Release"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DERIVED_DATA_DIR="${FLYWITHLUA_BUILD_DIR:-}"
if [ -z "$DERIVED_DATA_DIR" ]; then
    DERIVED_DATA_DIR="/tmp/FlyWithLua-Mac-Release"
fi
DIST_DIR="$REPO_ROOT/dist"
DIST_PACKAGE="$DIST_DIR/FlyWithLua"
RUNTIME_SOURCE="$REPO_ROOT/FlyWithLua"

DEPLOY_TO_XPLANE="${DEPLOY_TO_XPLANE:-1}"
if [ -z "${XPLANE_ROOT:-}" ]; then
    if [ -z "${HOME:-}" ]; then
        echo "Error: XPLANE_ROOT is required when HOME is unavailable." >&2
        exit 1
    fi
    XPLANE_ROOT="$HOME/X-Plane"
fi

case "$DEPLOY_TO_XPLANE" in
    0|1) ;;
    *)
        echo "Error: DEPLOY_TO_XPLANE must be 0 or 1." >&2
        exit 1
        ;;
esac

cd "$REPO_ROOT"

RUNTIME_PARENT="$XPLANE_ROOT/Resources/plugins"
RUNTIME_DIR="$RUNTIME_PARENT/FlyWithLua"
if [ "$DEPLOY_TO_XPLANE" = "1" ] && [ ! -d "$RUNTIME_PARENT" ]; then
    echo "Error: X-Plane plugins directory does not exist: $RUNTIME_PARENT" >&2
    exit 1
fi

echo "Building $PROJECT_NAME ($CONFIGURATION)..."

# Build both architectures. The product is checked before any deployment
# target is touched, so a failed or thin build cannot replace the installed one.
# FLYWITHLUA_BUILD_PRODUCT is an explicit escape hatch for a previously
# verified product when Xcode services are temporarily unavailable.
if [ -z "${FLYWITHLUA_BUILD_PRODUCT:-}" ]; then
    # project.yml is the source of truth; regenerate the generated project for
    # builds produced by this script.
    echo "Generating Xcode project with XcodeGen..."
    xcodegen generate
    xcodebuild clean build \
        -project "$PROJECT_NAME.xcodeproj" \
        -scheme "$TARGET_NAME" \
        -configuration "$CONFIGURATION" \
        -sdk macosx \
        -derivedDataPath "$DERIVED_DATA_DIR" \
        ONLY_ACTIVE_ARCH=NO \
        CODE_SIGN_IDENTITY="" \
        CODE_SIGNING_REQUIRED=NO
    BUILD_PATH="$(find "$DERIVED_DATA_DIR" -type d -path "*/Build/Products/$CONFIGURATION/FlyWithLua.xpl" -print -quit)"
else
    BUILD_PATH="$FLYWITHLUA_BUILD_PRODUCT"
    echo "Using explicitly supplied build product: $BUILD_PATH"
fi

if [ -z "$BUILD_PATH" ]; then
    echo "Error: Could not find the built FlyWithLua.xpl product."
    exit 1
fi

BUILD_BINARY="$BUILD_PATH/Contents/MacOS/FlyWithLua"
if [ ! -f "$BUILD_BINARY" ]; then
    echo "Error: Built product has no FlyWithLua binary."
    exit 1
fi

ARCHES="$(lipo -archs "$BUILD_BINARY")"
case " $ARCHES " in
    *" arm64 "*) ;;
    *)
        echo "Error: Release product is missing arm64: $ARCHES"
        exit 1
        ;;
esac
case " $ARCHES " in
    *" x86_64 "*) ;;
    *)
        echo "Error: Release product is missing x86_64: $ARCHES"
        exit 1
        ;;
esac

TMP_BASE="${TMPDIR:-}"
if [ -z "$TMP_BASE" ]; then
    TMP_BASE="/tmp"
fi
STAGE_ROOT="$(mktemp -d "$TMP_BASE/flywithlua-deploy.XXXXXX")"
DIST_PREVIOUS=""
BACKUP_ROOT=""
DIST_OLD_MOVED=0
DIST_NEW_MOVED=0
RUNTIME_INSTALL_STARTED=0
OLD_RUNTIME_MOVED=0
NEW_RUNTIME_MOVED=0

rollback() {
    local status="$?"
    if [ "$status" -ne 0 ]; then
        if [ "$RUNTIME_INSTALL_STARTED" = "1" ] && [ "$NEW_RUNTIME_MOVED" = "0" ] && [ "$OLD_RUNTIME_MOVED" = "1" ] && [ -e "$RUNTIME_DIR" ]; then
            rm -rf "$RUNTIME_DIR"
        fi
        if [ "$NEW_RUNTIME_MOVED" = "1" ] && [ -e "$RUNTIME_DIR" ]; then
            rm -rf "$RUNTIME_DIR"
        fi
        if [ "$OLD_RUNTIME_MOVED" = "1" ] && [ -e "$BACKUP_ROOT/FlyWithLua.active" ]; then
            mv "$BACKUP_ROOT/FlyWithLua.active" "$RUNTIME_DIR"
        fi
        if [ "$DIST_NEW_MOVED" = "1" ] && [ -e "$DIST_PACKAGE" ]; then
            rm -rf "$DIST_PACKAGE"
        fi
        if [ "$DIST_OLD_MOVED" = "1" ] && [ -e "$DIST_PREVIOUS" ]; then
            mv "$DIST_PREVIOUS" "$DIST_PACKAGE"
        fi
    fi
    rm -rf "$STAGE_ROOT"
    exit "$status"
}
trap rollback EXIT

PACKAGE_ROOT="$STAGE_ROOT/FlyWithLua"
mkdir -p "$PACKAGE_ROOT/mac_x64"
cp -p "$BUILD_BINARY" "$PACKAGE_ROOT/mac_x64/FlyWithLua.xpl"

copy_tree() {
    local source_path="$1"
    local destination_path="$2"
    mkdir -p "$destination_path"
    cp -R "$source_path/." "$destination_path/"
}

copy_file() {
    local source_path="$1"
    local destination_path="$2"
    mkdir -p "$(dirname "$destination_path")"
    cp -p "$source_path" "$destination_path"
}

for required_directory in Internals Modules Scripts "Scripts (Quarantine)"; do
    if [ ! -d "$RUNTIME_SOURCE/$required_directory" ]; then
        echo "Error: Missing runtime directory: $RUNTIME_SOURCE/$required_directory"
        exit 1
    fi
    copy_tree "$RUNTIME_SOURCE/$required_directory" "$PACKAGE_ROOT/$required_directory"
done

for required_file in fwl_prefs.ini user.ini user.exit README.txt; do
    if [ ! -f "$RUNTIME_SOURCE/$required_file" ]; then
        echo "Error: Missing runtime file: $RUNTIME_SOURCE/$required_file"
        exit 1
    fi
    copy_file "$RUNTIME_SOURCE/$required_file" "$PACKAGE_ROOT/$required_file"
done

PACKAGE_BINARY="$PACKAGE_ROOT/mac_x64/FlyWithLua.xpl"
echo "Verifying architectures..."
lipo -info "$PACKAGE_BINARY"

merge_tree() {
    local source_path="$1"
    local destination_path="$2"
    mkdir -p "$destination_path"
    cp -R "$source_path/." "$destination_path/"
}

merge_file() {
    local source_path="$1"
    local destination_path="$2"
    mkdir -p "$(dirname "$destination_path")"
    cp -p "$source_path" "$destination_path"
}

STAMP="$(date +%Y%m%d-%H%M%S)"

# Build a complete staged runtime from the existing installation. This keeps
# user-added files while allowing the final installation to be a directory
# swap instead of a partially merged live tree.
if [ "$DEPLOY_TO_XPLANE" = "1" ]; then
    RUNTIME_STAGE_DIR="$STAGE_ROOT/runtime/FlyWithLua"
    if [ -e "$RUNTIME_DIR" ] || [ -L "$RUNTIME_DIR" ]; then
        if [ ! -d "$RUNTIME_DIR" ]; then
            echo "Error: Existing FlyWithLua runtime is not a directory: $RUNTIME_DIR" >&2
            exit 1
        fi
        copy_tree "$RUNTIME_DIR" "$RUNTIME_STAGE_DIR"
    else
        mkdir -p "$RUNTIME_STAGE_DIR"
    fi

    mkdir -p "$RUNTIME_STAGE_DIR/mac_x64"
    RUNTIME_PLUGIN_BINARY="$RUNTIME_STAGE_DIR/mac_x64/FlyWithLua.xpl"
    if [ -e "$RUNTIME_PLUGIN_BINARY" ] || [ -L "$RUNTIME_PLUGIN_BINARY" ]; then
        rm -rf "$RUNTIME_PLUGIN_BINARY"
    fi
    cp -p "$PACKAGE_ROOT/mac_x64/FlyWithLua.xpl" "$RUNTIME_PLUGIN_BINARY"
    merge_tree "$PACKAGE_ROOT/Internals" "$RUNTIME_STAGE_DIR/Internals"
    merge_tree "$PACKAGE_ROOT/Modules" "$RUNTIME_STAGE_DIR/Modules"
    merge_tree "$PACKAGE_ROOT/Scripts" "$RUNTIME_STAGE_DIR/Scripts"
    merge_tree "$PACKAGE_ROOT/Scripts (Quarantine)" "$RUNTIME_STAGE_DIR/Scripts (Quarantine)"

    for runtime_file in fwl_prefs.ini user.ini user.exit; do
        if [ ! -e "$RUNTIME_STAGE_DIR/$runtime_file" ]; then
            merge_file "$PACKAGE_ROOT/$runtime_file" "$RUNTIME_STAGE_DIR/$runtime_file"
        fi
    done
    merge_file "$PACKAGE_ROOT/README.txt" "$RUNTIME_STAGE_DIR/README.txt"

    # A repository checkout named multi-layer-binding-main is not a FlyWithLua
    # script root. Keep its complete copy in the staged backup, then expose
    # only its runnable entry point and helper directory to FlyWithLua.
    LAYER_MAIN="$RUNTIME_STAGE_DIR/Scripts/multi-layer-binding-main"
    if [ -d "$LAYER_MAIN" ]; then
        if [ -f "$LAYER_MAIN/multi-layer-binding.lua" ]; then
            merge_file "$LAYER_MAIN/multi-layer-binding.lua" "$RUNTIME_STAGE_DIR/Scripts/multi-layer-binding.lua"
        fi
        if [ -d "$LAYER_MAIN/multi-layer-binding" ]; then
            merge_tree "$LAYER_MAIN/multi-layer-binding" "$RUNTIME_STAGE_DIR/Scripts/multi-layer-binding"
        fi
        rm -rf "$LAYER_MAIN"
    fi

    if [ ! -f "$RUNTIME_PLUGIN_BINARY" ]; then
        echo "Error: Staged runtime has no FlyWithLua plugin binary." >&2
        exit 1
    fi
    lipo -info "$RUNTIME_PLUGIN_BINARY"
fi

mkdir -p "$DIST_DIR"
DIST_PREVIOUS="$DIST_DIR/FlyWithLua.previous-$STAMP-$$"
if [ -e "$DIST_PACKAGE" ] || [ -L "$DIST_PACKAGE" ]; then
    mv "$DIST_PACKAGE" "$DIST_PREVIOUS"
    DIST_OLD_MOVED=1
fi
mv "$PACKAGE_ROOT" "$DIST_PACKAGE"
DIST_NEW_MOVED=1
echo "Distribution package ready: $DIST_PACKAGE"

if [ "$DEPLOY_TO_XPLANE" = "0" ]; then
    echo "X-Plane deployment skipped (DEPLOY_TO_XPLANE=0)."
    exit 0
fi

BACKUP_ROOT="$RUNTIME_PARENT/FlyWithLua.backup-$STAMP-$$"
mkdir -p "$BACKUP_ROOT"
if [ -e "$RUNTIME_DIR" ] || [ -L "$RUNTIME_DIR" ]; then
    mv "$RUNTIME_DIR" "$BACKUP_ROOT/FlyWithLua.active"
    OLD_RUNTIME_MOVED=1
fi

RUNTIME_INSTALL_STARTED=1
mv "$RUNTIME_STAGE_DIR" "$RUNTIME_DIR"
NEW_RUNTIME_MOVED=1

echo "Installed universal plugin and runtime files to: $RUNTIME_DIR"
echo "Existing files backed up to: $BACKUP_ROOT"
