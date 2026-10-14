#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
image=${WRAPPER_SMOKE_IMAGE:-wrapper-lite:local}
platform=${WRAPPER_SMOKE_PLATFORM:-linux/amd64}
data_dir=${WRAPPER_SMOKE_DATA_DIR:-$root/rootfs/data}
port=${WRAPPER_SMOKE_PORT:-12440}
container_name=wrapper-lite-smoke-$$
trap 'docker rm -f "$container_name" >/dev/null 2>&1 || true' EXIT HUP INT TERM
if [ "${WRAPPER_SMOKE_BUILD:-1}" = 1 ]; then
  docker buildx build --platform "$platform" --load --tag "$image" .
fi
mkdir -p "$data_dir/data/com.apple.android.music/files"
docker run --platform "$platform" --privileged --rm -v "$data_dir:/app/rootfs/data" "$image" --doctor
# Works with an empty account: verifies startup, honest status, and input errors.
docker run --platform "$platform" --privileged -d --name "$container_name" \
  -v "$data_dir:/app/rootfs/data" -p "127.0.0.1:$port:12340" "$image" >/dev/null
if ! python3 - "$port" <<'PY'
import json, sys, time, urllib.request
base = 'http://127.0.0.1:' + sys.argv[1]
for _ in range(90):
    try:
        with urllib.request.urlopen(base + '/status', timeout=2) as response:
            status = json.load(response)
        assert status['code'] == 0 and isinstance(status['data']['regions'], list)
        break
    except OSError:
        time.sleep(1)
else:
    sys.exit('Lite /status did not respond')
for path in ('/key', '/m3u8', '/lyrics', '/webplayback'):
    with urllib.request.urlopen(base + path, timeout=3) as response:
        assert json.load(response)['code'] == 400
print('[smoke] Lite started; status and input-error envelopes passed')
PY
then
  docker logs --tail 60 "$container_name" >&2 || true
  exit 1
fi
