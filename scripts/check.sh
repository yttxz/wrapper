#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
data_dir=${WRAPPER_CHECK_DATA_DIR:-$root/rootfs/data}
mkdir -p "$data_dir/data/com.apple.android.music/files"
for script in entrypoint.sh scripts/*.sh; do sh -n "$script"; done
env WRAPPER_DATA_DIR="$data_dir" ./scripts/doctor.sh
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/wrapper-lite-tests.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
cc -std=c99 -Wall -Wextra -Werror -I. tests/runtime_dirs_test.c -o "$test_dir/runtime-dirs"
"$test_dir/runtime-dirs" "$test_dir/root"
c++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined -I. \
  tests/login_input_test.cpp -o "$test_dir/login-input"
"$test_dir/login-input" "$test_dir/auth"
c++ -std=c++11 -Wall -Wextra -Werror -I. tests/terminal_probe.cpp -o "$test_dir/terminal-probe"
python3 tests/terminal_input_test.py "$test_dir/terminal-probe"
if [ "${RUN_DOCKER_CHECKS:-0}" = 1 ]; then
  image=${WRAPPER_CHECK_IMAGE:-wrapper-lite-check:local}
  docker buildx build --platform linux/amd64 --load --tag "$image" .
  docker run --platform linux/amd64 --rm --entrypoint /bin/sh "$image" -c \
    'test -d /app/rootfs/data/data/com.apple.android.music/files && ! find /app/rootfs/data -type f | grep -q .'
  env WRAPPER_SMOKE_BUILD=0 WRAPPER_SMOKE_IMAGE="$image" WRAPPER_SMOKE_DATA_DIR="$data_dir" ./scripts/smoke.sh
else
  echo '[check] Docker checks require RUN_DOCKER_CHECKS=1'
fi
echo '[check] passed'
