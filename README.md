# wrapper

`wrapper` runs the Apple Music decryption engine from the bundled Linux/Android
runtime. An active Apple Music subscription is still required.

This fork keeps the Linux/Docker workflow as the canonical runtime and release
path. macOS users should run the same Linux container through Docker Desktop;
there is no native macOS launcher or native macOS engine.

## Platform Support

The functional engine is Linux-based.

| Platform | Status |
| --- | --- |
| Linux x86_64 | Supported |
| Linux arm64 | Supported by the upstream project |
| macOS Apple Silicon | Supported through Docker Desktop |
| macOS Intel | Supported through Docker Desktop |
| Native macOS engine | Not supported |

The bundled runtime in `rootfs/` contains Linux/Android ELF binaries. macOS
cannot load those binaries directly, so macOS support is Docker-only.

## What This Fork Adds

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

The Docker entrypoint does not recursively change ownership of mounted
`rootfs/data` by default. If a moved or restored data directory has ownership
that prevents startup, set `WRAPPER_CHOWN_DATA=1` for one repair run.

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

During login, a requested 2FA code is entered in the same terminal and the
input is hidden. File-based 2FA is only used when explicitly requested with
`--code-from-file`; set `WRAPPER_2FA_FROM_FILE=1` for Docker entrypoint runs
that need that mode. In file mode, write the six-digit code to:

```text
rootfs/data/data/com.apple.android.music/files/2fa.txt
```

The code file must be a regular file, not a symlink, and readable only by the
owner, such as with `chmod 600`. The code file is removed after the wrapper
reads a valid, empty, or malformed code. The default wait is 60 seconds; set
`WRAPPER_2FA_TIMEOUT_SECONDS` to a value from `1` to `600` to adjust it.

## Docker Workflow

The Docker workflow is the release path on Linux and macOS. On macOS, install
Docker Desktop first. The default wrapper needs `--privileged` because it
bind-mounts `/dev/urandom`, enters `rootfs`, mounts `/proc`, and starts the
engine in a Linux runtime environment.

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

When 2FA is requested, enter the code at the container prompt. The code is read
like a password and is not echoed to the terminal.

For detached or non-interactive login, or if you prefer the file handoff,
explicitly add `-e WRAPPER_2FA_FROM_FILE=1` to the Docker command and write the
code from another terminal:

```sh
umask 077
printf '%s' 123456 > rootfs/data/data/com.apple.android.music/files/2fa.txt
```

Only exactly six digits are accepted. The file must be a regular, non-symlink
file readable only by its owner. In file mode, empty or malformed code files
are deleted and the wrapper keeps waiting until the 2FA timeout expires; unsafe
handoff files are refused.

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

The published host ports are bound to `127.0.0.1`. Inside the container, the
entrypoint binds the Linux service to `0.0.0.0` so Docker can forward those
localhost-only host ports into the container.

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

## Diagnostics

Run the doctor script from the source tree:

```sh
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

## Development Checks

Run the local smoke check:

```sh
./scripts/check.sh
```

This checks shell syntax, C syntax, doctor diagnostics, and basic wrapper
commands. On macOS, Docker Desktop must be running because doctor diagnostics
check Docker availability.

Run the full Docker-backed check before publishing Linux/Docker changes:

```sh
RUN_DOCKER_CHECKS=1 ./scripts/check.sh
```

The extended check builds a `linux/amd64` image, verifies account state from
`rootfs/data` is not baked into the image, runs doctor diagnostics inside a
privileged container, and runs the smoke test below.

Run the Docker smoke test directly:

```sh
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
  -L, --login=STRING        Login in username:password format
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
