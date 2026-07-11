#!/bin/sh

set -eu

TZDATA_SHA256=479e83ca4d289b2ae3d08eb222a5167a3c8bff185f2ccac932458e96bd6489ee

usage() {
  echo "Usage: $0 OUTPUT.zip" >&2
  exit 2
}

if [ "$#" -ne 1 ]; then
  usage
fi

invocation_dir=$(pwd)
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
case $1 in
  /*) output=$1 ;;
  *) output=$invocation_dir/$1 ;;
esac

output_dir=$(dirname -- "$output")
if [ ! -d "$output_dir" ]; then
  echo "Error: output directory does not exist: $output_dir" >&2
  exit 1
fi

for command_name in mktemp tar zip; do
  if ! command -v "$command_name" >/dev/null 2>&1; then
    echo "Error: required command not found: $command_name" >&2
    exit 1
  fi
done

required_paths='rootfs
rootfs/system/bin/main
rootfs/system/bin/linker64
rootfs/system/usr/share/zoneinfo/tzdata
wrapper
wrapper-rootless
Dockerfile
compose.yaml
entrypoint.sh
scripts/doctor.sh
.env.example
README.md'

cd "$root"
printf '%s\n' "$required_paths" | while IFS= read -r path; do
  if [ ! -e "$path" ]; then
    echo "Error: required release input is missing: $path" >&2
    exit 1
  fi
done

if [ ! -x wrapper ] || [ ! -x wrapper-rootless ] || [ ! -x rootfs/system/bin/main ]; then
  echo "Error: generated release binaries must be executable." >&2
  exit 1
fi

if command -v sha256sum >/dev/null 2>&1; then
  actual_tzdata_sha=$(sha256sum rootfs/system/usr/share/zoneinfo/tzdata | awk '{print $1}')
elif command -v shasum >/dev/null 2>&1; then
  actual_tzdata_sha=$(shasum -a 256 rootfs/system/usr/share/zoneinfo/tzdata | awk '{print $1}')
else
  echo "Error: sha256sum or shasum is required." >&2
  exit 1
fi

if [ "$actual_tzdata_sha" != "$TZDATA_SHA256" ]; then
  echo "Error: Android tzdata checksum does not match the pinned release input." >&2
  exit 1
fi

tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/wrapper-release.XXXXXX")
trap 'rm -rf "$tmp_dir"' EXIT HUP INT TERM
payload=$tmp_dir/payload
mkdir -p "$payload/rootfs" "$payload/scripts"

tar -C rootfs --exclude='./data' -cf - . | tar -C "$payload/rootfs" -xf -
cp wrapper wrapper-rootless Dockerfile compose.yaml entrypoint.sh .env.example README.md "$payload/"
cp scripts/doctor.sh "$payload/scripts/doctor.sh"

if [ -e "$payload/rootfs/data" ]; then
  echo "Error: rootfs/data must not be included in release artifacts." >&2
  exit 1
fi
if find "$payload" -type f \( -name '*.c' -o -name '*.h' -o -name '*.cpp' \) \
    -print -quit | grep -q .; then
  echo "Error: source files must not be included in release artifacts." >&2
  exit 1
fi

(cd "$payload" && zip -qr "$tmp_dir/release.zip" .)
mv "$tmp_dir/release.zip" "$output"
echo "Created $output"
