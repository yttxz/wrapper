# wrapper

`wrapper` runs the Apple Music decryption engine from the bundled Linux/Android
runtime. An active Apple Music subscription is still required.

This fork keeps the Linux/Docker workflow as the canonical runtime path and adds
a macOS-friendly launcher as an optional convenience layer. On macOS, the native
`wrapper` command does not run the engine natively; the real Linux engine still
runs inside Docker.

## Platform Support

The functional engine is Linux-based.

| Platform | Status |
| --- | --- |
| Linux x86_64 | Supported |
| Linux arm64 | Supported by the upstream project |
| macOS Apple Silicon | Supported through Docker Desktop |
| macOS Intel | Supported through Docker Desktop |
| macOS launcher | Convenience wrapper around Docker |
| Native macOS engine | Not supported |

The bundled runtime in `rootfs/` contains Linux/Android ELF binaries. macOS
cannot load those binaries directly, so macOS support depends on Docker.

## What This Fork Adds

- Optional Docker-backed macOS launcher built from `macos-native.c`.
- CMake host detection for native macOS launcher builds.
- Docker entrypoint support for first-login and normal service runs.
- `scripts/doctor.sh` for runtime, Docker, port, and capability diagnostics.
- `scripts/check.sh` for local and Docker-backed validation.
- `rootfs/data` mounted at runtime instead of baked into Docker images.
- Docker builds include Android timezone data required by the bundled bionic
  runtime.
- Shared common setup code for the privileged Linux wrapper and rootless wrapper.
- Safer C paths around input parsing, path formatting, token/cache handling,
  account JSON formatting, sockets, offline channel probing, and cleanup.

## Data Directory

`rootfs/data` stores local account state, token caches, and app data. It is
mounted into Docker containers and intentionally excluded from Docker images.
Do not commit it.

The main account database lives under:

```text
rootfs/data/data/com.apple.android.music/files/mpl_db/kvs.sqlitedb
```

Token caches are stored next to the Apple Music app data:

```text
rootfs/data/data/com.apple.android.music/files/STOREFRONT_ID
rootfs/data/data/com.apple.android.music/files/MUSIC_TOKEN
```

Normal service runs reuse these files and should not need `USERNAME` or
`PASSWORD`. Provide credentials only for first login, when the token cache is
missing, or when the saved session has expired.

If a 2FA code is requested while running with `--code-from-file`, write the
six-digit code to:

```text
rootfs/data/data/com.apple.android.music/files/2fa.txt
```

The code file is removed after the wrapper reads it. The default wait is 60
seconds; set `WRAPPER_2FA_TIMEOUT_SECONDS` to a value from `1` to `600` to
adjust it.

## Quick Start On macOS

The direct Docker workflow below is the supported path for validation and bug
reports. The native launcher is convenient for local use, but it is still just a
Docker runner.

Install Docker Desktop first, then build the native launcher:

```sh
mkdir -p build-macos
cmake -S . -B build-macos
cmake --build build-macos
```

Check the setup:

```sh
./build-macos/wrapper --doctor
```

Run the wrapper:

```sh
./build-macos/wrapper
```

On first run, the launcher checks for a local Docker image named
`wrapper-macos:local`. If it is missing, the launcher builds it from this source
tree, mounts `rootfs/data`, publishes the configured ports, and starts the Linux
engine inside Docker.

The launcher reuses an existing local image. Rebuild it after source or
Dockerfile changes with:

```sh
WRAPPER_DOCKER_REBUILD=1 ./build-macos/wrapper
```

Use a custom image tag:

```sh
WRAPPER_DOCKER_IMAGE=wrapper-run:local ./build-macos/wrapper
```

On macOS, `-H` controls the host address used for Docker port publishing. Inside
the container, the Linux engine binds to `0.0.0.0` so published ports work.

## Docker Workflow

The Docker workflow is the recommended way to run the Linux engine on macOS and
is also the integration path used by the full check script. The default wrapper
needs `--privileged` because it bind-mounts `/dev/urandom`, enters `rootfs`,
mounts `/proc`, and starts the engine in a Linux runtime environment.

Build a local image:

```sh
docker buildx build --platform linux/amd64 --load --tag wrapper:local .
```

Docker builds verify downloaded build inputs before unpacking or installing
them. The pinned inputs are:

| Input | Pin |
| --- | --- |
| Android NDK | `android-ndk-r23b-linux.zip` with SHA-256 `c6e97f9c8cfe5b7be0a9e6c15af8e7a179475b7ded23e2d1c1fa0945d6fb4382` |
| Android tzdata | `platform/system/timezone` commit `0470df3d38d8e08932ebbe08b3d8ec9bbdcd403f` with SHA-256 `479e83ca4d289b2ae3d08eb222a5167a3c8bff185f2ccac932458e96bd6489ee` |

All examples below assume the current directory is the repository root. If you
run from another directory, use the absolute mount path:

```sh
-v "$HOME/Downloads/wrapper/rootfs/data:/app/rootfs/data"
```

Login when no account database exists, or refresh login when the token cache is
missing or expired:

```sh
read "APPLE_ID?Apple ID: "
read -rs "APPLE_PASSWORD?Password: "; echo

docker run --platform linux/amd64 --privileged --name wrapper --rm -it \
  -v "$PWD/rootfs/data:/app/rootfs/data" \
  -e USERNAME="$APPLE_ID" \
  -e PASSWORD="$APPLE_PASSWORD" \
  wrapper:local
```

When 2FA is requested, leave the container running and write the code from
another terminal:

```sh
umask 077
printf '%s' 123456 > rootfs/data/data/com.apple.android.music/files/2fa.txt
```

Only exactly six digits are accepted. Empty or malformed code files are deleted
and the wrapper keeps waiting until the 2FA timeout expires.

Quit after login completes. Later service runs reuse the mounted account state
and should not include credentials.

Run the service:

```sh
docker run --platform linux/amd64 --privileged --name wrapper --rm -it \
  -v "$PWD/rootfs/data:/app/rootfs/data" \
  -p 127.0.0.1:10020:10020 \
  -p 127.0.0.1:20020:20020 \
  -p 127.0.0.1:30020:30020 \
  wrapper:local
```

The M3U8 service uses the offline/download channel when the account reports that
it is available. To force the streaming channel instead, add:

```sh
-e WRAPPER_DISABLE_OFFLINE=1
```

Run diagnostics inside Docker:

```sh
docker run --platform linux/amd64 --privileged --rm \
  -v "$PWD/rootfs/data:/app/rootfs/data" \
  wrapper:local --doctor
```

## Linux Workflow

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
curl -fLO https://dl.google.com/android/repository/android-ndk-r23b-linux.zip
echo "c6e97f9c8cfe5b7be0a9e6c15af8e7a179475b7ded23e2d1c1fa0945d6fb4382  android-ndk-r23b-linux.zip" | sha256sum -c -
unzip -d . android-ndk-r23b-linux.zip
```

Build:

```sh
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
valid JSON.

The M3U8 URL service chooses the offline/download channel when available and
falls back to the streaming playback channel otherwise. Set
`WRAPPER_DISABLE_OFFLINE=1` in the container environment to force streaming.

## Diagnostics

Run the doctor script from the source tree:

```sh
./scripts/doctor.sh
```

Run the same check through the macOS launcher:

```sh
./build-macos/wrapper --doctor
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

## Development Checks

Run the local smoke check:

```sh
./scripts/check.sh
```

This checks shell syntax, C syntax, doctor diagnostics, macOS launcher build
behavior on macOS, and basic wrapper commands. On macOS, Docker Desktop must be
running because doctor diagnostics check Docker availability.

Run the full Docker-backed check before publishing Linux/Docker changes:

```sh
RUN_DOCKER_CHECKS=1 ./scripts/check.sh
```

The extended check builds a `linux/amd64` image, verifies account state from
`rootfs/data` is not baked into the image, and runs doctor diagnostics inside a
privileged container.

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
  -L, --login=STRING        Login in username:password format
  -F, --code-from-file      Read 2FA code from 2fa.txt
  -B, --base-dir=STRING     Runtime data directory
  -I, --device-info=STRING  Slash-separated device identity fields
```

## Safety Notes

Use this project only with an active subscription and in situations where you
have the right to access the content.

Keep `rootfs/data` local. It can contain account/session state and should not be
published or committed.

## Special Thanks

- Anonymous, for the original version of this project and the legacy Frida
  decryption method.
- chocomint, for arm64 support.
- WorldObservationLog, for maintaining the active upstream fork this work was
  based on.
