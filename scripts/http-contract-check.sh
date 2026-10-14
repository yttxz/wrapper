#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
cjson_source=${1:-build/_deps/cjson-src}
[ -f "$cjson_source/cJSON.c" ] || { echo 'Provide the pinned cJSON source directory.' >&2; exit 1; }
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/wrapper-http-tests.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
cc -std=c99 -O2 -c "$cjson_source/cJSON.c" -o "$test_dir/cjson.o"
c++ -std=c++11 -O1 -Wall -Werror -pthread -I. -Ilite -I"$cjson_source" \
  lite/lite_main.cpp tests/lite_backend_stub.cpp "$test_dir/cjson.o" -o "$test_dir/http-fixture"
python3 tests/http_contract_test.py "$test_dir/http-fixture"
