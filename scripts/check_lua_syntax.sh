#!/bin/bash

set -Eeuo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

CHECKER=""
CHECKER_MODE=""
if command -v luajit >/dev/null 2>&1; then
    CHECKER="$(command -v luajit)"
    CHECKER_MODE="luajit"
elif command -v luac5.1 >/dev/null 2>&1; then
    CHECKER="$(command -v luac5.1)"
    CHECKER_MODE="luac"
elif command -v luac >/dev/null 2>&1; then
    CHECKER="$(command -v luac)"
    CHECKER_MODE="luac"
else
    echo "Error: luajit, luac5.1, or luac is required for Lua syntax checks." >&2
    exit 1
fi

check_file() {
    local file="$1"
    if [ "$CHECKER_MODE" = "luajit" ]; then
        LUA_SYNTAX_FILE="$file" "$CHECKER" -e 'assert(loadfile(os.getenv("LUA_SYNTAX_FILE")))'
    else
        "$CHECKER" -p "$file"
    fi
}

file_count=0
while IFS= read -r -d '' file; do
    file_count=$((file_count + 1))
    echo "Checking $file"
    check_file "$file"
done < <(find FlyWithLua/Internals FlyWithLua/Modules FlyWithLua/Scripts -type f \( -name '*.lua' -o -name '*.fwl' \) -print0)

for file in FlyWithLua/Internals/FlyWithLua.ini FlyWithLua/user.ini FlyWithLua/user.exit; do
    if [ -f "$file" ]; then
        file_count=$((file_count + 1))
        echo "Checking $file"
        check_file "$file"
    fi
done

if [ "$file_count" -eq 0 ]; then
    echo "Error: No Lua source files found." >&2
    exit 1
fi

echo "Lua syntax check passed for $file_count files."
