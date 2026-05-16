# FlyWithLua-Mac: Build & Deployment Guide

This guide provides instructions for building FlyWithLua-Mac as a Universal Binary (arm64 + x86_64) and deploying it to X-Plane 12.

## Prerequisites

- **macOS 12.0+** (Monterey or later recommended for Swift Concurrency)
- **Xcode 13.0+**
- **Homebrew** (for managing tools)
- **XcodeGen**: Install via homebrew:
  ```bash
  brew install xcodegen
  ```

## Step 1: Prepare Dependencies

Run the automation scripts to fetch and build the necessary libraries.

1. **LuaJIT (Universal Binary)**:
   ```bash
   chmod +x scripts/build_luajit.sh
   ./scripts/build_luajit.sh
   ```
   *This clones LuaJIT v2.1 and creates a combined library in `lib/LuaJIT/lib/libluajit.a`.*

2. **X-Plane SDK 4.0**:
   ```bash
   chmod +x scripts/setup_sdk.sh
   ./scripts/setup_sdk.sh
   ```
   *This downloads the latest SDK and organizes headers in `include/`.*

## Step 2: Generate Xcode Project

Use XcodeGen to generate the `.xcodeproj` file from the `project.yml` configuration.
```bash
xcodegen generate
```

## Step 3: Build & Deploy

You can build directly from Xcode or use the provided deployment script.

### Using the Deployment Script
```bash
chmod +x scripts/deploy.sh
./scripts/deploy.sh
```
*This script builds the Universal Binary and packages it into the `dist/` directory.*

### Manual Build in Xcode
1. Open `FlyWithLua-Mac.xcodeproj`.
2. Select the **FlyWithLua-Mac** target and **Any Mac (Apple Silicon, Intel)** as the destination.
3. Build (**Cmd + B**).
4. The resulting `FlyWithLua.xpl` bundle will be in the build output directory.

## Deployment to X-Plane

Copy the `FlyWithLua.xpl` bundle to your X-Plane plugin directory:
`X-Plane 12/Resources/plugins/FlyWithLua/mac.xpl`

*(Note: On Mac, the plugin can be a bundle folder named `FlyWithLua.xpl` or a single file renamed to `mac.xpl` inside a folder. Our project generates a bundle.)*

## Troubleshooting

- **Architecture Mismatch**: Run `lipo -info` on the resulting binary to ensure it contains both `arm64` and `x86_64`.
- **Code Signing**: If X-Plane fails to load the plugin, ensure it is code-signed. The `project.yml` is configured for ad-hoc signing by default.
