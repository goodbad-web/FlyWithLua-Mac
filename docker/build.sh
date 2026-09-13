#!/bin/bash
set -Eeuo pipefail

function build() {
  local src_dir="$1"
  local platform="$2"
  echo "----------------- Building for $platform -----------------"

  local build_dir="$src_dir/build-$platform"

  local flags=()
  case "$platform" in
    lin)
      ;;
#    win)
#      flags+=('-DCMAKE_TOOLCHAIN_FILE=../Toolchain-mingw-w64-x86-64.cmake')
#      ;;
#    mac)
#      flags+=('-DCMAKE_TOOLCHAIN_FILE=../Toolchain-ubuntu-osxcross-10.11.cmake')
#      flags+=('-DCMAKE_FIND_ROOT_PATH=/usr/osxcross/SDK/MacOSX10.11.sdk/')
#      ;;
    *)
      echo "Platform $platform is not supported, skipping..."
      return
  esac

  (
#    export PATH="$PATH:/usr/osxcross/bin"
    mkdir -p "$build_dir" && cd "$build_dir"
    cmake -G Ninja "${flags[@]}" ..
    ninja -v
  )
}

if [ "$#" -eq 0 ]; then
  echo "Usage: $0 <platform> [<platform> ...]" >&2
  exit 2
fi

src_dir="$(cd "$(dirname "$0")/.." && pwd)"
for platform in "$@"; do
  build "$src_dir" "$platform"
done
