#!/bin/sh

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

image=${WRAPPER_SMOKE_IMAGE:-wrapper-smoke:local}
platform=${WRAPPER_SMOKE_PLATFORM:-linux/amd64}
build_image=${WRAPPER_SMOKE_BUILD:-1}
container_name=${WRAPPER_SMOKE_CONTAINER:-wrapper-smoke-$$}
data_dir=${WRAPPER_SMOKE_DATA_DIR:-$root/rootfs/data}
decrypt_port=${WRAPPER_SMOKE_DECRYPT_PORT:-11020}
m3u8_port=${WRAPPER_SMOKE_M3U8_PORT:-12020}
account_port=${WRAPPER_SMOKE_ACCOUNT_PORT:-13020}
start_timeout=${WRAPPER_SMOKE_START_TIMEOUT:-60}
poll_seconds=2
container_started=0
response_file=

music_files="$data_dir/data/com.apple.android.music/files"
token_db="$music_files/mpl_db/kvs.sqlitedb"
storefront_id="$music_files/STOREFRONT_ID"
music_token="$music_files/MUSIC_TOKEN"

log() {
  printf '[smoke] %s\n' "$1"
}

fail() {
  printf '[smoke] fail: %s\n' "$1" >&2
  exit 1
}

cleanup() {
  if [ "$container_started" = "1" ]; then
    docker rm -f "$container_name" >/dev/null 2>&1 || true
  fi
  if [ -n "${response_file:-}" ]; then
    rm -f "$response_file"
  fi
}

trap cleanup EXIT INT TERM

have_command() {
  command -v "$1" >/dev/null 2>&1
}

is_container_running() {
  [ "$(docker inspect -f '{{.State.Running}}' "$container_name" 2>/dev/null || true)" = "true" ]
}

fetch_account_response() {
  url="http://127.0.0.1:$account_port/"
  if have_command curl; then
    curl -fsS --max-time 2 -o "$response_file" "$url" >/dev/null 2>&1
    return $?
  fi

  if have_command python3; then
    python3 - "$url" "$response_file" <<'PY'
import sys
import urllib.request

url = sys.argv[1]
path = sys.argv[2]
try:
    with urllib.request.urlopen(url, timeout=2) as response:
        body = response.read()
    with open(path, "wb") as output:
        output.write(body)
except Exception:
    sys.exit(1)
PY
    return $?
  fi

  fail "curl or python3 is required to probe the account endpoint"
}

validate_account_response() {
  if ! have_command python3; then
    fail "python3 is required to validate account endpoint JSON without printing tokens"
  fi

  python3 - "$response_file" <<'PY'
import json
import sys

path = sys.argv[1]
try:
    with open(path, "r", encoding="utf-8") as input_file:
        data = json.load(input_file)
except Exception:
    print("[smoke] fail: account endpoint did not return valid JSON", file=sys.stderr)
    sys.exit(1)

required = ("storefront_id", "dev_token", "music_token")
missing = [
    key for key in required
    if not isinstance(data.get(key), str) or data.get(key) == ""
]
if missing:
    print("[smoke] fail: account endpoint missing non-empty fields: " + ", ".join(missing), file=sys.stderr)
    sys.exit(1)

print("[smoke] account endpoint returned expected JSON fields")
PY
}

if ! have_command docker; then
  fail "Docker CLI is not available"
fi

if ! docker info >/dev/null 2>&1; then
  fail "Docker daemon is not reachable"
fi

if [ "$build_image" = "1" ]; then
  log "building $image for $platform"
  docker buildx build --platform "$platform" --load --tag "$image" .
else
  log "using existing image $image"
fi

log "running container doctor"
docker run --platform "$platform" --privileged --rm \
  -v "$data_dir:/app/rootfs/data" \
  "$image" --doctor

if [ ! -s "$token_db" ] || [ ! -s "$storefront_id" ] || [ ! -s "$music_token" ]; then
  log "account state is incomplete; skipping service/account endpoint smoke"
  exit 0
fi

if ! have_command curl && ! have_command python3; then
  fail "curl or python3 is required to probe the account endpoint"
fi

if ! have_command python3; then
  fail "python3 is required to validate account endpoint JSON without printing tokens"
fi

response_file=$(mktemp "${TMPDIR:-/tmp}/wrapper-smoke-account.XXXXXX")
chmod 600 "$response_file"

docker rm -f "$container_name" >/dev/null 2>&1 || true

log "starting service container $container_name"
docker run --platform "$platform" --privileged -d \
  --name "$container_name" \
  -v "$data_dir:/app/rootfs/data" \
  -p "127.0.0.1:$decrypt_port:10020" \
  -p "127.0.0.1:$m3u8_port:20020" \
  -p "127.0.0.1:$account_port:30020" \
  "$image" >/dev/null
container_started=1

elapsed=0
while [ "$elapsed" -lt "$start_timeout" ]; do
  if fetch_account_response; then
    validate_account_response
    log "service smoke passed"
    exit 0
  fi

  if ! is_container_running; then
    docker logs --tail 80 "$container_name" >&2 || true
    fail "service container exited before account endpoint responded"
  fi

  sleep "$poll_seconds"
  elapsed=$((elapsed + poll_seconds))
done

docker logs --tail 80 "$container_name" >&2 || true
fail "account endpoint did not respond within ${start_timeout}s"
