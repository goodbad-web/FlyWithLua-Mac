#!/bin/bash

# Build, package, and safely deploy FlyWithLua-Mac.
# The active runtime is merged without deleting user files. Existing files are
# copied to a timestamped backup before they can be overwritten.

set -e

PROJECT_NAME="FlyWithLua-Mac"
TARGET_NAME="FlyWithLua-Mac"
CONFIGURATION="Release"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DERIVED_DATA_DIR="$FLYWITHLUA_BUILD_DIR"
if [ -z "$DERIVED_DATA_DIR" ]; then
    DERIVED_DATA_DIR="/tmp/FlyWithLua-Mac-Release"
fi
DIST_DIR="$REPO_ROOT/dist"
DIST_PACKAGE="$DIST_DIR/FlyWithLua"
RUNTIME_SOURCE="$REPO_ROOT/FlyWithLua"

if [ -z "$DEPLOY_TO_XPLANE" ]; then
    DEPLOY_TO_XPLANE=1
fi
if [ -z "$XPLANE_ROOT" ]; then
    XPLANE_ROOT="/Users/hiroshi/X-Plane"
fi

cd "$REPO_ROOT"

echo "Building $PROJECT_NAME ($CONFIGURATION)..."

# project.yml is the source of truth; regenerate the generated project every time.
echo "Generating Xcode project with XcodeGen..."
xcodegen generate

# Build both architectures. The product is checked before any deployment
# target is touched, so a failed or thin build cannot replace the installed one.
# FLYWITHLUA_BUILD_PRODUCT is an explicit escape hatch for a previously
# verified product when Xcode services are temporarily unavailable.
if [ -z "$FLYWITHLUA_BUILD_PRODUCT" ]; then
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

TMP_BASE="$TMPDIR"
if [ -z "$TMP_BASE" ]; then
    TMP_BASE="/tmp"
fi
STAGE_ROOT="$(mktemp -d "$TMP_BASE/flywithlua-deploy.XXXXXX")"
trap 'rm -rf "$STAGE_ROOT"' EXIT

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

STAMP="$(date +%Y%m%d-%H%M%S)"
mkdir -p "$DIST_DIR"
DIST_PREVIOUS="$DIST_DIR/FlyWithLua.previous-$STAMP-$$"
if [ -e "$DIST_PACKAGE" ] || [ -L "$DIST_PACKAGE" ]; then
    mv "$DIST_PACKAGE" "$DIST_PREVIOUS"
fi
mv "$PACKAGE_ROOT" "$DIST_PACKAGE"

echo "Distribution package ready: $DIST_PACKAGE"

if [ "$DEPLOY_TO_XPLANE" = "0" ]; then
    echo "X-Plane deployment skipped (DEPLOY_TO_XPLANE=0)."
    exit 0
fi

RUNTIME_PARENT="$XPLANE_ROOT/Resources/plugins"
RUNTIME_DIR="$RUNTIME_PARENT/FlyWithLua"
if [ ! -d "$RUNTIME_PARENT" ]; then
    echo "Error: X-Plane plugins directory does not exist: $RUNTIME_PARENT"
    exit 1
fi

BACKUP_ROOT="$RUNTIME_PARENT/FlyWithLua.backup-$STAMP-$$"
mkdir -p "$BACKUP_ROOT"

backup_path() {
    local relative_path="$1"
    local source_path="$RUNTIME_DIR/$relative_path"
    local backup_path="$BACKUP_ROOT/$relative_path"
    if [ -d "$source_path" ]; then
        mkdir -p "$backup_path"
        cp -R "$source_path/." "$backup_path/"
    elif [ -e "$source_path" ]; then
        mkdir -p "$(dirname "$backup_path")"
        cp -p "$source_path" "$backup_path"
    fi
}

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

for runtime_path in mac_x64 Internals Modules Scripts "Scripts (Quarantine)" fwl_prefs.ini user.ini user.exit README.txt; do
    backup_path "$runtime_path"
done

mkdir -p "$RUNTIME_DIR"
mkdir -p "$RUNTIME_DIR/mac_x64"
RUNTIME_PLUGIN_BINARY="$RUNTIME_DIR/mac_x64/FlyWithLua.xpl"
if [ -d "$RUNTIME_PLUGIN_BINARY" ]; then
    mv "$RUNTIME_PLUGIN_BINARY" "$BACKUP_ROOT/mac_x64/FlyWithLua.xpl.bundle"
fi
cp -p "$DIST_PACKAGE/mac_x64/FlyWithLua.xpl" "$RUNTIME_PLUGIN_BINARY"
merge_tree "$DIST_PACKAGE/Internals" "$RUNTIME_DIR/Internals"
merge_tree "$DIST_PACKAGE/Modules" "$RUNTIME_DIR/Modules"
merge_tree "$DIST_PACKAGE/Scripts" "$RUNTIME_DIR/Scripts"
merge_tree "$DIST_PACKAGE/Scripts (Quarantine)" "$RUNTIME_DIR/Scripts (Quarantine)"
for runtime_file in fwl_prefs.ini user.ini user.exit README.txt; do
    merge_file "$DIST_PACKAGE/$runtime_file" "$RUNTIME_DIR/$runtime_file"
done

# A repository checkout named multi-layer-binding-main is not a FlyWithLua
# script root. Move it into the backup (recoverable, not deleted), then copy
# its runnable entry point and helper directory to the active Scripts root.
LAYER_MAIN="$RUNTIME_DIR/Scripts/multi-layer-binding-main"
if [ -d "$LAYER_MAIN" ]; then
    LAYER_BACKUP="$BACKUP_ROOT/multi-layer-binding-main.normalized-source"
    mv "$LAYER_MAIN" "$LAYER_BACKUP"
    if [ -f "$LAYER_BACKUP/multi-layer-binding.lua" ]; then
        merge_file "$LAYER_BACKUP/multi-layer-binding.lua" "$RUNTIME_DIR/Scripts/multi-layer-binding.lua"
    fi
    if [ -d "$LAYER_BACKUP/multi-layer-binding" ]; then
        merge_tree "$LAYER_BACKUP/multi-layer-binding" "$RUNTIME_DIR/Scripts/multi-layer-binding"
    fi
fi

echo "Installed universal plugin and runtime files to: $RUNTIME_DIR"
echo "Existing files backed up to: $BACKUP_ROOT"
