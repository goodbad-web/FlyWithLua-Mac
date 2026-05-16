#!/bin/bash

# Fetch X-Plane SDK 4.0 and organize it into the include directory

set -e

SDK_URL="https://developer.x-plane.com/wp-content/plugins/code-sample-generation/sdk_zip_files/XPSDK400.zip"
SDK_ZIP="build/SDK400.zip"
SDK_EXTRACT_DIR="build/SDK400"
INCLUDE_DIR="$(pwd)/include"

mkdir -p build
mkdir -p "$INCLUDE_DIR"

if [ ! -f "$SDK_ZIP" ]; then
    echo "Downloading X-Plane SDK 4.0..."
    curl -Lf "$SDK_URL" -o "$SDK_ZIP"
fi

echo "Extracting SDK..."
unzip -q -o "$SDK_ZIP" -d "$SDK_EXTRACT_DIR"

echo "Organizing headers..."
# The SDK zip usually contains a 'CHeaders' directory
CP_DIR=$(find "$SDK_EXTRACT_DIR" -name "CHeaders" -type d | head -n 1)

if [ -d "$CP_DIR" ]; then
    cp -R "$CP_DIR/"* "$INCLUDE_DIR/"
    echo "SDK Headers installed to $INCLUDE_DIR"
else
    echo "Error: Could not find CHeaders in SDK zip."
    exit 1
fi

echo "SDK setup complete."
ls -la "$INCLUDE_DIR"
