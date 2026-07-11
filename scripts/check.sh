#!/bin/sh

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
data_dir=${WRAPPER_CHECK_DATA_DIR:-$root/rootfs/data}

if [ -n "${WRAPPER_CHECK_DATA_DIR:-}" ]; then
  mkdir -p "$data_dir/data/com.apple.android.music/files"
fi

run() {
  printf '\n[check] %s\n' "$*"
  "$@"
}

if command -v sh >/dev/null 2>&1; then
  run sh -n entrypoint.sh
  run sh -n scripts/doctor.sh
  run sh -n scripts/check.sh
  run sh -n scripts/package-release.sh
  run sh -n scripts/smoke.sh
fi

run env WRAPPER_DATA_DIR="$data_dir" scripts/doctor.sh

if command -v clang >/dev/null 2>&1; then
  run clang -fsyntax-only -Wall -Wextra -Werror cmdline.c

  run clang -DMyRelease -fsyntax-only -Wall -Wextra -Werror \
    -Itests/stubs -include tests/stubs/linux_compat.h main.c
else
  printf '[warn] clang not found; skipping syntax-only C checks\n'
fi

if command -v cc >/dev/null 2>&1; then
  runtime_test_dir=$(mktemp -d "${TMPDIR:-/tmp}/wrapper-runtime-test.XXXXXX")
  trap 'rm -rf "$runtime_test_dir"' EXIT HUP INT TERM
  run cc -std=c99 -Wall -Wextra -Werror -I. \
    tests/runtime_dirs_test.c -o "$runtime_test_dir/runtime-dirs-test"
  run "$runtime_test_dir/runtime-dirs-test" "$runtime_test_dir/root"
else
  printf '[warn] host C compiler not available; skipping runtime directory test\n'
fi

if [ "${RUN_DOCKER_CHECKS:-0}" = "1" ]; then
  run docker buildx build --platform linux/amd64 --load --tag wrapper-check:local .
  run docker run --platform linux/amd64 --rm --entrypoint /bin/sh \
    wrapper-check:local -c \
    'test -d /app/rootfs/data/data/com.apple.android.music/files && ! find /app/rootfs/data -type f | grep -q .'
  run docker run --platform linux/amd64 --privileged --rm \
    -v "$data_dir:/app/rootfs/data" \
    wrapper-check:local --doctor
  run env WRAPPER_SMOKE_BUILD=0 WRAPPER_SMOKE_IMAGE=wrapper-check:local \
    WRAPPER_SMOKE_DATA_DIR="$data_dir" \
    scripts/smoke.sh
else
  printf '[warn] RUN_DOCKER_CHECKS=1 not set; skipping Docker build/container checks\n'
fi

printf '\n[check] done\n'
