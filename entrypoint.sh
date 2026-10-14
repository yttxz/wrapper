#!/bin/sh
set -eu
umask 077
base_dir=/data/data/com.apple.android.music/files
files=/app/rootfs$base_dir
if [ "${1:-}" = "--doctor" ] || [ "${1:-}" = "doctor" ]; then
  exec /app/scripts/doctor.sh
fi
# Help/version should work without account state, mounts, or credentials.
if [ "${1:-}" = "--help" ] || [ "${1:-}" = "--version" ]; then
  exec /app/wrapper-lite "$@"
fi
mkdir -p "$files"
/app/scripts/runtime-dns.sh
if [ "${WRAPPER_CHOWN_DATA:-0}" = "1" ]; then
  chown -R root:root /app/rootfs/data
fi
# Explicit --login always wins over environment-driven automatic login.
explicit_login=0
for arg in "$@"; do
  if [ "$arg" = "--login" ]; then explicit_login=1; fi
done
if [ "$explicit_login" = "0" ] && [ -n "${USERNAME:-}" ] &&    { [ ! -s "$files/mpl_db/kvs.sqlitedb" ] || [ ! -s "$files/MUSIC_TOKEN" ] || [ ! -s "$files/STOREFRONT_ID" ]; }; then
  login_arg=$USERNAME
  if [ "${WRAPPER_PASSWORD_FROM_ENV:-0}" = "1" ]; then
    [ -n "${PASSWORD:-}" ] || { echo 'PASSWORD is required when WRAPPER_PASSWORD_FROM_ENV=1.' >&2; exit 1; }
    login_arg="$USERNAME:$PASSWORD"
  elif [ -n "${PASSWORD:-}" ]; then
    echo 'Ignoring PASSWORD; the password will be prompted. Use WRAPPER_PASSWORD_FROM_ENV=1 for legacy login.' >&2
  fi
  if [ "${WRAPPER_2FA_FROM_FILE:-0}" = "1" ]; then
    ./wrapper-lite --base-dir "$base_dir" --login "$login_arg" --code-from-file "$@"
  else
    ./wrapper-lite --base-dir "$base_dir" --login "$login_arg" "$@"
  fi
fi
exec ./wrapper-lite --base-dir "$base_dir" --host 0.0.0.0 "$@"
