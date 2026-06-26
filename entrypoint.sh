#!/bin/sh
set -e

TOKEN_DB_PATH="/app/rootfs/data/data/com.apple.android.music/files/mpl_db/kvs.sqlitedb"
STOREFRONT_ID_PATH="/app/rootfs/data/data/com.apple.android.music/files/STOREFRONT_ID"
MUSIC_TOKEN_PATH="/app/rootfs/data/data/com.apple.android.music/files/MUSIC_TOKEN"

if [ "${1:-}" = "--doctor" ] || [ "${1:-}" = "doctor" ]; then
  exec /app/scripts/doctor.sh
fi

if [ ! -d "/app/rootfs/data/data/com.apple.android.music/files" ]; then
  mkdir -p "/app/rootfs/data/data/com.apple.android.music/files"
fi

run_login() {
  if [ -t 0 ] && [ "${WRAPPER_2FA_FROM_FILE:-0}" != "1" ]; then
    exec ./wrapper \
      -L "${USERNAME}:${PASSWORD}" \
      -H 0.0.0.0 \
      "$@"
  fi

  exec ./wrapper \
    -L "${USERNAME}:${PASSWORD}" \
    -F \
    -H 0.0.0.0 \
    "$@"
}

if [ "$(stat -c %U "/app/rootfs/data")" != "root" ] || [ "$(stat -c %G "/app/rootfs/data")" != "root" ]; then
  chown -R root:root "/app/rootfs/data"
fi

if [ ! -f "$TOKEN_DB_PATH" ]; then
  echo "Login required: account database not found."
  if [ -z "${USERNAME:-}" ] || [ -z "${PASSWORD:-}" ]; then
    echo "Error: USERNAME and PASSWORD environment variables must be set when account database is missing." >&2
    exit 1
  fi
  run_login "$@"
elif { [ ! -s "$STOREFRONT_ID_PATH" ] || [ ! -s "$MUSIC_TOKEN_PATH" ]; } && \
     [ -n "${USERNAME:-}" ] && [ -n "${PASSWORD:-}" ]; then
  echo "Refreshing login: token cache is missing."
  run_login "$@"
else
  if [ ! -s "$STOREFRONT_ID_PATH" ] || [ ! -s "$MUSIC_TOKEN_PATH" ]; then
    echo "Warning: token cache is missing; set USERNAME and PASSWORD to refresh login if startup fails." >&2
  fi
  exec ./wrapper \
    -H 0.0.0.0 \
    "$@"
fi
