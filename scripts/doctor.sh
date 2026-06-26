#!/bin/sh

set -u

failures=0
warnings=0

ok() {
  printf '[ok] %s\n' "$1"
}

warn() {
  warnings=$((warnings + 1))
  printf '[warn] %s\n' "$1"
}

fail() {
  failures=$((failures + 1))
  printf '[fail] %s\n' "$1"
}

have_command() {
  command -v "$1" >/dev/null 2>&1
}

is_docker_container() {
  [ -f /.dockerenv ] || grep -qaE '/docker/|/containerd/' /proc/1/cgroup 2>/dev/null
}

resolve_root() {
  script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" 2>/dev/null && pwd)
  if [ -n "${script_dir:-}" ] && [ -d "$script_dir/.." ]; then
    CDPATH= cd -- "$script_dir/.." 2>/dev/null && pwd
    return
  fi
  pwd
}

check_path() {
  path=$1
  label=$2
  if [ -e "$path" ]; then
    ok "$label exists: $path"
  else
    fail "$label missing: $path"
  fi
}

check_optional_file() {
  path=$1
  label=$2
  if [ -s "$path" ]; then
    ok "$label exists"
  else
    warn "$label missing or empty: $path"
  fi
}

check_port() {
  host=$1
  port=$2
  if have_command nc; then
    if nc -z "$host" "$port" >/dev/null 2>&1; then
      warn "port $host:$port is already in use"
    else
      ok "port $host:$port is available"
    fi
    return
  fi

  if have_command lsof; then
    if lsof -nP -iTCP:"$port" -sTCP:LISTEN >/dev/null 2>&1; then
      warn "port $host:$port is already in use"
    else
      ok "port $host:$port is available"
    fi
    return
  fi

  warn "cannot check port $host:$port because neither nc nor lsof is available"
}

check_cap_sys_admin() {
  if [ ! -r /proc/self/status ]; then
    warn "cannot check Linux capabilities outside /proc"
    return
  fi

  cap_eff=$(awk '/^CapEff:/ {print $2}' /proc/self/status 2>/dev/null)
  if [ -z "${cap_eff:-}" ]; then
    warn "cannot read CapEff from /proc/self/status"
    return
  fi

  cap_dec=$(printf '%d' "0x$cap_eff" 2>/dev/null || printf '0')
  if [ $((cap_dec & 2097152)) -ne 0 ]; then
    ok "CAP_SYS_ADMIN is available"
  else
    fail "CAP_SYS_ADMIN is missing; Docker runs need --privileged for the default wrapper"
  fi
}

check_docker() {
  image=${WRAPPER_DOCKER_IMAGE:-wrapper:local}

  if ! have_command docker; then
    if [ "$(uname -s 2>/dev/null)" = "Darwin" ]; then
      fail "Docker CLI is not installed or not on PATH"
    else
      warn "Docker CLI is not installed or not on PATH"
    fi
    return
  fi
  ok "Docker CLI is available"

  if docker info >/dev/null 2>&1; then
    ok "Docker daemon is reachable"
  else
    fail "Docker daemon is not reachable; start Docker Desktop or the Docker service"
    return
  fi

  if docker image inspect "$image" >/dev/null 2>&1; then
    arch_os=$(docker image inspect "$image" --format '{{.Architecture}}/{{.Os}}' 2>/dev/null || true)
    case "$arch_os" in
      amd64/linux) ok "Docker image $image is linux/amd64" ;;
      */*) fail "Docker image $image has unexpected platform: $arch_os" ;;
      *) warn "Docker image $image exists, but platform could not be inspected" ;;
    esac
  else
    warn "Docker image $image is not built yet; build it with docker buildx build --platform linux/amd64 --load --tag $image ."
  fi
}

root=$(resolve_root)
data_dir="$root/rootfs/data"
music_files="$data_dir/data/com.apple.android.music/files"
token_db="$music_files/mpl_db/kvs.sqlitedb"
storefront_id="$music_files/STOREFRONT_ID"
music_token="$music_files/MUSIC_TOKEN"
engine="$root/rootfs/system/bin/main"
tzdata="$root/rootfs/system/usr/share/zoneinfo/tzdata"

printf 'wrapper doctor\n'
printf 'project root: %s\n\n' "$root"

check_path "$root/rootfs" "rootfs"
check_path "$data_dir" "rootfs/data"
check_path "$music_files" "Apple Music data directory"

if [ -s "$token_db" ]; then
  ok "account database exists"
else
  if [ -n "${USERNAME:-}" ] && [ -n "${PASSWORD:-}" ]; then
    ok "account database is missing, but USERNAME and PASSWORD are set for login"
  else
    warn "account database missing: $token_db"
    warn "set USERNAME and PASSWORD for first login, or mount the populated rootfs/data directory"
  fi
fi

check_optional_file "$storefront_id" "storefront cache"
check_optional_file "$music_token" "music token cache"

if [ -s "$tzdata" ]; then
  tzdata_magic=$(dd if="$tzdata" bs=6 count=1 2>/dev/null)
  if [ "$tzdata_magic" = "tzdata" ]; then
    ok "Android timezone database exists: $tzdata"
  else
    fail "Android timezone database has unexpected format: $tzdata"
  fi
else
  if [ ! -e "$engine" ] && [ -f "$root/Dockerfile" ]; then
    warn "Android timezone database is not present in source tree before Docker build"
  else
    fail "Android timezone database missing or empty: $tzdata"
  fi
fi

if [ -e "$engine" ]; then
  if [ -x "$engine" ]; then
    ok "engine binary is executable"
  else
    fail "engine binary exists but is not executable: $engine"
  fi
else
  if [ -f "$root/Dockerfile" ]; then
    warn "engine binary is not present in source tree; Docker builds generate it in the image"
  else
    fail "engine binary missing: $engine"
  fi
fi

for lib in \
  libandroidappmusic.so \
  libstoreservicescore.so \
  libmediaplatform.so \
  libc++_shared.so \
  liblog.so
do
  check_path "$root/rootfs/system/lib64/$lib" "$lib"
done

if is_docker_container; then
  check_cap_sys_admin
else
  check_docker
  check_port "${WRAPPER_HOST:-127.0.0.1}" "${WRAPPER_DECRYPT_PORT:-10020}"
  check_port "${WRAPPER_HOST:-127.0.0.1}" "${WRAPPER_M3U8_PORT:-20020}"
  check_port "${WRAPPER_HOST:-127.0.0.1}" "${WRAPPER_ACCOUNT_PORT:-30020}"
fi

printf '\nsummary: %d failure(s), %d warning(s)\n' "$failures" "$warnings"

if [ "$failures" -ne 0 ]; then
  exit 1
fi

exit 0
