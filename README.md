# wrapper

`wrapper` runs the Apple Music decryption engine from the bundled Linux/Android
runtime. An active Apple Music subscription is required.

The supported release path is Docker. macOS users should run the Linux container
through Docker Desktop; this project does not ship a macOS-native executable.

## Platform Support

| Runtime | Status |
| --- | --- |
| Docker on Linux or macOS | Supported and recommended |
| Native Linux x86_64 | Supported for advanced users |
| Native Linux arm64 | Upstream-supported |
| Native macOS | Not supported |

`rootfs/` contains Linux/Android ELF binaries, so macOS support is Docker-only.

## What This Fork Adds

- Docker entrypoint support for first-login and normal service runs.
- Doctor, check, and smoke scripts for local and Docker-backed validation.
- `rootfs/data` mounted at runtime instead of baked into Docker images.
- Docker builds include Android timezone data required by the bundled bionic
  runtime.
- Shared common setup code for the privileged Linux wrapper and rootless wrapper.
- Safer C paths around input parsing, path formatting, token/cache handling,
  account JSON formatting, sockets, offline channel probing, and cleanup.

## Data And Login

`rootfs/data` stores local account state, token caches, and app data. It is
mounted into Docker containers and intentionally excluded from Docker images.
Do not commit it.

The Docker entrypoint does not recursively change ownership of mounted
`rootfs/data` by default. If a moved or restored data directory has ownership
that prevents startup, set `WRAPPER_CHOWN_DATA=1` for one repair run.

Important account files:

```text
rootfs/data/data/com.apple.android.music/files/mpl_db/kvs.sqlitedb
rootfs/data/data/com.apple.android.music/files/STOREFRONT_ID
rootfs/data/data/com.apple.android.music/files/MUSIC_TOKEN
```

Normal service runs reuse these files and should not need credentials. Provide
`USERNAME` only for first login, when the token cache is missing, or when the
saved session has expired. The password is prompted interactively and hidden.

During login, password and 2FA prompts are read from the terminal with input
hidden. File-based 2FA is only used when explicitly requested with
`--code-from-file` or `WRAPPER_2FA_FROM_FILE=1`.

## Docker Workflow

Install Docker Desktop on macOS. The default container needs `--privileged`
because the Linux wrapper prepares `/dev/urandom`, `rootfs`, `/proc`, and the
bundled Linux runtime.

Build a local image:

```sh
cd path/to/wrapper
docker buildx build --platform linux/amd64 --load --tag wrapper:local .
```

Docker builds verify downloaded build inputs before unpacking or installing
them. The pinned inputs are:

| Input | Pin |
| --- | --- |
| Android NDK | `android-ndk-r23b-linux.zip` with SHA-256 `c6e97f9c8cfe5b7be0a9e6c15af8e7a179475b7ded23e2d1c1fa0945d6fb4382` |
| Android tzdata | `platform/system/timezone` commit `0470df3d38d8e08932ebbe08b3d8ec9bbdcd403f` with SHA-256 `479e83ca4d289b2ae3d08eb222a5167a3c8bff185f2ccac932458e96bd6489ee` |

Login when no account database exists, or refresh login when the token cache is
missing or expired:

```sh
cd path/to/wrapper
read "APPLE_ID?Apple ID: "

docker run --platform linux/amd64 --privileged --name wrapper --rm -it \
  -v "$PWD/rootfs/data:/app/rootfs/data" \
  -e USERNAME="$APPLE_ID" \
  wrapper:local
```

When the password is requested, enter it at the container prompt. The password
is read like a password and is not echoed to the terminal.

When 2FA is requested, enter the code at the container prompt. The code is read
like a password and is not echoed to the terminal.

For legacy non-interactive login, set both `WRAPPER_PASSWORD_FROM_ENV=1` and
`PASSWORD`. This exposes the password through Docker environment and wrapper
process arguments, so prefer the interactive prompt.

For detached 2FA handoff, add `-e WRAPPER_2FA_FROM_FILE=1` to the Docker command
and write the six-digit code from another terminal:

```sh
cd path/to/wrapper
umask 077
printf '%s' 123456 > rootfs/data/data/com.apple.android.music/files/2fa.txt
```

The 2FA file must be a regular, non-symlink file readable only by its owner.
Empty or malformed files are deleted and the wrapper keeps waiting; unsafe files
are refused. The default wait is 60 seconds. Set `WRAPPER_2FA_TIMEOUT_SECONDS`
to `1` through `600` to adjust it.

Quit after login completes. Later service runs reuse the mounted account state
and should not include credentials.

Run the service:

```sh
cd path/to/wrapper
docker run --platform linux/amd64 --privileged --name wrapper --rm -it \
  -v "$PWD/rootfs/data:/app/rootfs/data" \
  -p 127.0.0.1:10020:10020 \
  -p 127.0.0.1:20020:20020 \
  -p 127.0.0.1:30020:30020 \
  wrapper:local
```

The published host ports are bound to `127.0.0.1`. Inside the container, the
entrypoint binds the Linux service to `0.0.0.0` so Docker can forward those
localhost-only host ports into the container.

The M3U8 service uses the offline/download channel when available. To force the
streaming channel, add:

```sh
-e WRAPPER_DISABLE_OFFLINE=1
```

Run diagnostics inside Docker:

```sh
cd path/to/wrapper
docker run --platform linux/amd64 --privileged --rm \
  -v "$PWD/rootfs/data:/app/rootfs/data" \
  wrapper:local --doctor
```

## Linux Workflow

Native Linux builds are useful for development, but Docker is the release path.

Install build dependencies:

```sh
sudo apt install build-essential cmake curl unzip git
```

Install LLVM:

```sh
sudo bash -c "$(wget -O - https://apt.llvm.org/llvm.sh)"
```

Download Android NDK r23b:

```sh
cd path/to/wrapper
curl -fLO https://dl.google.com/android/repository/android-ndk-r23b-linux.zip
echo "c6e97f9c8cfe5b7be0a9e6c15af8e7a179475b7ded23e2d1c1fa0945d6fb4382  android-ndk-r23b-linux.zip" | sha256sum -c -
unzip -d . android-ndk-r23b-linux.zip
```

Build:

```sh
cd path/to/wrapper
mkdir build
cd build
cmake ..
make -j"$(nproc)"
```

The Linux build produces:

- `rootfs/system/bin/main`, the engine-facing binary.
- `wrapper`, the default privileged launcher.
- `wrapper-rootless`, the rootless launcher for Linux hosts with unprivileged
  user, mount, and PID namespaces enabled.

Both Linux launchers share setup code for signal forwarding, `/dev/urandom`,
`chroot`, runtime binary permissions, and data directory creation. Their
namespace and `/proc` setup remains separate because privileged and rootless
launch paths have different ordering requirements.

## Services

Default ports:

| Port | Service |
| --- | --- |
| `10020` | Decrypt service |
| `20020` | M3U8 URL service |
| `30020` | Account info JSON service |

The account info endpoint returns JSON with `storefront_id`, `dev_token`, and
`music_token`. Responses are built with `cJSON` so token strings are escaped as
valid JSON. Treat this endpoint as sensitive because it returns bearer-style
account tokens; bind it to localhost unless you have added your own access
control in front of it.

The M3U8 URL service chooses the offline/download channel when available and
falls back to the streaming playback channel otherwise. Set
`WRAPPER_DISABLE_OFFLINE=1` in the container environment to force streaming.

Runtime request caps reject oversized inputs before large allocations or
token-bearing responses are processed:

| Service | Limit |
| --- | --- |
| Decrypt adam ID | 32 bytes |
| Decrypt key URI | 240 bytes |
| Decrypt sample | 16 MiB |
| M3U8 adam ID | 32 bytes |
| Account HTTP request | 4 KiB |

## Diagnostics And Checks

Run the doctor script from the source tree:

```sh
cd path/to/wrapper
./scripts/doctor.sh
```

The doctor checks:

- required `rootfs` files
- `rootfs/data` account directory
- account database and token cache files
- Android timezone database availability
- Docker CLI and Docker daemon access
- expected Docker image platform
- default service ports
- Linux container capability requirements

Warnings are not always fatal. For example, the engine binary can be missing
from the source tree before a Docker build because Docker generates it inside
the image. Port warnings mean something is already listening on that port.

Run local checks:

```sh
cd path/to/wrapper
./scripts/check.sh
```

This checks shell syntax, C syntax, doctor diagnostics, and basic wrapper
commands. On macOS, Docker Desktop must be running because doctor diagnostics
check Docker availability.

Run full Docker-backed checks before publishing Linux/Docker changes:

```sh
cd path/to/wrapper
RUN_DOCKER_CHECKS=1 ./scripts/check.sh
```

The extended check builds a `linux/amd64` image, verifies account state from
`rootfs/data` is not baked into the image, runs doctor diagnostics inside a
privileged container, and runs the smoke test below.

Run the Docker smoke test directly:

```sh
cd path/to/wrapper
./scripts/smoke.sh
```

The smoke test builds an image, runs doctor in the container, and, when local
account state exists, starts the service on localhost test ports and verifies
that the account endpoint returns the expected JSON fields without printing
token values. Override ports with `WRAPPER_SMOKE_DECRYPT_PORT`,
`WRAPPER_SMOKE_M3U8_PORT`, and `WRAPPER_SMOKE_ACCOUNT_PORT`.

## Command-Line Options

```text
Usage: wrapper [OPTION]...

  -h, --help                Print help and exit
  -V, --version             Print version and exit
  -H, --host=STRING         Host to bind on (default: 127.0.0.1)
  -D, --decrypt-port=INT    Decrypt service port (default: 10020)
  -M, --m3u8-port=INT       M3U8 service port (default: 20020)
  -A, --account-port=INT    Account service port (default: 30020)
  -P, --proxy=STRING        Proxy string (default: empty)
  -L, --login=STRING        Login username; prompts for password
  -F, --code-from-file      Read 2FA code from 2fa.txt instead of prompting
  -B, --base-dir=STRING     Runtime data directory
  -I, --device-info=STRING  Slash-separated device identity fields
```

## Safety Notes

Use this project only with an active subscription and in situations where you
have the right to access the content.

Keep `rootfs/data` local. It can contain account/session state and should not be
published or committed.

The default Docker path requires `--privileged` so the Linux launcher can set up
its runtime namespace and mounts. Do not run untrusted wrapper images or expose
the service ports beyond localhost without an additional security boundary.

Account tokens, M3U8 URLs, and auth responses can grant access to account-bound
resources. Normal logs avoid printing token prefixes, full playback URLs, and
auth response previews. Debug Android/curl runtime logs are opt-in with
`WRAPPER_VERBOSE_RUNTIME_LOGS=1` and may contain sensitive protocol data.

## Special Thanks

- Anonymous, for the original version of this project and the legacy Frida
  decryption method.
- chocomint, for arm64 support.
- WorldObservationLog, for maintaining the active upstream fork this work was
  based on.
