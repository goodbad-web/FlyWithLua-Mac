#!/bin/bash

set -Eeuo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LUA_BIN="$(command -v luajit || true)"
if [ -z "$LUA_BIN" ]; then
    echo "Error: luajit is required for Lua regression tests." >&2
    exit 1
fi

TEST_TMP="$(mktemp -d "${TMPDIR:-/tmp}/flywithlua-tests.XXXXXX")"
cleanup() {
    rm -rf "$TEST_TMP"
}
trap cleanup EXIT

FWL_TEST_ROOT="$REPO_ROOT" FWL_TEST_TMP="$TEST_TMP" "$LUA_BIN" "$REPO_ROOT/tests/lua/test_regressions.lua"
