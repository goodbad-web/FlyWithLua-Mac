#!/bin/bash

# Fetch X-Plane SDK 4.0 and organize it into the include directory

set -Eeuo pipefail

SDK_URL="https://developer.x-plane.com/wp-content/plugins/code-sample-generation/sdk_zip_files/XPSDK400.zip"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK_ZIP="$REPO_ROOT/build/SDK400.zip"
SDK_EXTRACT_DIR="$REPO_ROOT/build/SDK400"
INCLUDE_DIR="$REPO_ROOT/include"

mkdir -p "$REPO_ROOT/build"
mkdir -p "$INCLUDE_DIR"

if [ ! -f "$SDK_ZIP" ]; then
    echo "Downloading X-Plane SDK 4.0..."
    curl -fL --retry 3 --retry-all-errors --silent --show-error "$SDK_URL" -o "$SDK_ZIP"
fi

echo "Extracting SDK..."
rm -rf "$SDK_EXTRACT_DIR"
mkdir -p "$SDK_EXTRACT_DIR"
unzip -q -o "$SDK_ZIP" -d "$SDK_EXTRACT_DIR"

echo "Organizing headers..."
# The SDK zip usually contains a 'CHeaders' directory
CP_DIR="$(find "$SDK_EXTRACT_DIR" -name "CHeaders" -type d -print -quit)"

if [ -d "$CP_DIR" ]; then
    cp -R "$CP_DIR/"* "$INCLUDE_DIR/"
    echo "SDK Headers installed to $INCLUDE_DIR"
else
    echo "Error: Could not find CHeaders in SDK zip."
    exit 1
fi

echo "SDK setup complete."
ls -la "$INCLUDE_DIR"
