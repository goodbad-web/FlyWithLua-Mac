#!/bin/bash

# Deployment script for FlyWithLua-Mac
# Builds the project and packages it for distribution.

set -e

PROJECT_NAME="FlyWithLua-Mac"
TARGET_NAME="FlyWithLua-Mac"
CONFIGURATION="Release"
DIST_DIR="dist"

echo "Building $PROJECT_NAME ($CONFIGURATION)..."

# project.yml is the source of truth; regenerate the generated project every time.
echo "Generating Xcode project with XcodeGen..."
xcodegen generate

# Clean and build using xcodebuild
xcodebuild clean build \
    -project "$PROJECT_NAME.xcodeproj" \
    -scheme "$TARGET_NAME" \
    -configuration "$CONFIGURATION" \
    -derivedDataPath "build/DerivedData" \
    ONLY_ACTIVE_ARCH=NO \
    CODE_SIGN_IDENTITY="" \
    CODE_SIGNING_REQUIRED=NO

# Locate the built product
BUILD_PATH=$(find build/DerivedData -name "FlyWithLua.xpl" -type d | head -n 1)

if [ -z "$BUILD_PATH" ]; then
    echo "Error: Could not find the built product."
    exit 1
fi

echo "Packaging to $DIST_DIR..."
mkdir -p "$DIST_DIR"
cp -R "$BUILD_PATH" "$DIST_DIR/"

echo "Verifying architectures..."
BINARY_PATH="$DIST_DIR/FlyWithLua.xpl/Contents/MacOS/FlyWithLua"
lipo -info "$BINARY_PATH"

echo "Deployment package ready in $DIST_DIR/FlyWithLua.xpl"
