# FlyWithLua-Mac: Native Optimization Project

This repository is a modernized, macOS-optimized version of FlyWithLua.

## Project Structure

- `src/`: C++ and Swift source files.
- `lib/`: Compiled libraries (Universal Binaries).
- `include/`: Header files for dependencies.
- `scripts/`: Automation scripts for building dependencies.
- `build/`: Temporary build artifacts.

## Prerequisites

- Xcode 13+
- Git
- Internet connection (to fetch dependencies)

## Setup Instructions

### 1. Build LuaJIT
Run the provided script to build LuaJIT as a Universal Binary (arm64 + x86_64).
```bash
chmod +x scripts/build_luajit.sh
./scripts/build_luajit.sh
```

### 2. X-Plane SDK
Download the X-Plane SDK 4.0 from the [developer portal](https://developer.x-plane.com/sdk/downloading-the-sdk/) and place it in the `include/XPLM` directory.

### 3. FMOD SDK
Integrate the FMOD Core API (Mac) into the project as a framework.
