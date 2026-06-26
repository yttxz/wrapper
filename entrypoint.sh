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
  login_arg="$USERNAME"
  if [ "${WRAPPER_PASSWORD_FROM_ENV:-0}" = "1" ]; then
    if [ -z "${PASSWORD:-}" ]; then
      echo "Error: PASSWORD must be set when WRAPPER_PASSWORD_FROM_ENV=1." >&2
      exit 1
    fi
    login_arg="${USERNAME}:${PASSWORD}"
  elif [ -n "${PASSWORD:-}" ]; then
    echo "Warning: ignoring PASSWORD unless WRAPPER_PASSWORD_FROM_ENV=1; the wrapper will prompt for the password." >&2
  fi

  if [ "${WRAPPER_2FA_FROM_FILE:-0}" = "1" ]; then
    exec ./wrapper \
      -L "$login_arg" \
      -F \
      -H 0.0.0.0 \
      "$@"
  fi

  exec ./wrapper \
    -L "$login_arg" \
    -H 0.0.0.0 \
    "$@"
}

DATA_UID="$(stat -c %u "/app/rootfs/data")"
DATA_GID="$(stat -c %g "/app/rootfs/data")"
if [ "$DATA_UID" != "0" ] || [ "$DATA_GID" != "0" ]; then
  if [ "${WRAPPER_CHOWN_DATA:-0}" = "1" ]; then
    echo "Repairing rootfs/data ownership because WRAPPER_CHOWN_DATA=1."
    chown -R root:root "/app/rootfs/data"
  else
    echo "Warning: rootfs/data is not owned by root; set WRAPPER_CHOWN_DATA=1 to repair ownership if startup fails." >&2
  fi
fi

if [ ! -f "$TOKEN_DB_PATH" ]; then
  echo "Login required: account database not found."
  if [ -z "${USERNAME:-}" ]; then
    echo "Error: USERNAME environment variable must be set when account database is missing." >&2
    exit 1
  fi
  run_login "$@"
elif { [ ! -s "$STOREFRONT_ID_PATH" ] || [ ! -s "$MUSIC_TOKEN_PATH" ]; } && \
     [ -n "${USERNAME:-}" ]; then
  echo "Refreshing login: token cache is missing."
  run_login "$@"
else
  if [ ! -s "$STOREFRONT_ID_PATH" ] || [ ! -s "$MUSIC_TOKEN_PATH" ]; then
    echo "Warning: token cache is missing; set USERNAME to refresh login if startup fails." >&2
  fi
  exec ./wrapper \
    -H 0.0.0.0 \
    "$@"
fi
