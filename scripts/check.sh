#!/bin/sh

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

run() {
  printf '\n[check] %s\n' "$*"
  "$@"
}

if command -v sh >/dev/null 2>&1; then
  run sh -n entrypoint.sh
  run sh -n scripts/doctor.sh
  run sh -n scripts/check.sh
fi

run scripts/doctor.sh

if command -v clang >/dev/null 2>&1; then
  run clang -fsyntax-only -Wall -Wextra -Werror \
    -DWRAPPER_SOURCE_DIR="\"$root\"" macos-native.c cmdline.c

  run clang -DMyRelease -fsyntax-only -Wall -Wextra -Werror \
    -Itests/stubs -include tests/stubs/linux_compat.h main.c
else
  printf '[warn] clang not found; skipping syntax-only C checks\n'
fi

if command -v cmake >/dev/null 2>&1 && [ "$(uname -s 2>/dev/null)" = "Darwin" ]; then
  run cmake -S . -B build-macos
  run cmake --build build-macos
  run ./build-macos/wrapper --version
  run ./build-macos/wrapper --help
  run ./build-macos/wrapper --doctor
else
  printf '[warn] cmake or macOS host not available; skipping macOS launcher build check\n'
fi

if [ "${RUN_DOCKER_CHECKS:-0}" = "1" ]; then
  run docker buildx build --platform linux/amd64 --load --tag wrapper-check:local .
  run docker run --platform linux/amd64 --rm --entrypoint /bin/sh \
    wrapper-check:local -c \
    'test -d /app/rootfs/data/data/com.apple.android.music/files && ! find /app/rootfs/data -type f | grep -q .'
  run docker run --platform linux/amd64 --privileged --rm \
    -v "$root/rootfs/data:/app/rootfs/data" \
    wrapper-check:local --doctor
else
  printf '[warn] RUN_DOCKER_CHECKS=1 not set; skipping Docker build/container checks\n'
fi

printf '\n[check] done\n'
