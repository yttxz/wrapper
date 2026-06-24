# wrapper

`wrapper` runs the Apple Music decryption engine from the bundled Linux/Android
runtime. An active Apple Music subscription is still required.

This fork keeps the original Linux/Docker workflow and adds a macOS-friendly
launcher that builds a small native command on macOS, then runs the real Linux
engine through Docker.

## What Changed In This Fork

- Added a Docker-backed macOS launcher built from `macos-native.c`.
- Added automatic macOS detection in CMake.
- Added `scripts/doctor.sh` for Docker, port, mount, and runtime checks.
- Added `scripts/check.sh` for a local smoke test.
- Updated Docker usage so `rootfs/data` is mounted instead of baked into images.
- Added safer Docker examples for Apple Silicon and x86_64 macOS.
- Cleaned up several C paths around login parsing, path construction, sockets,
  token/cache file handling, and basic input validation.

## Platform Support

The functional decryption engine is still Linux-based.

| Platform | Status |
| --- | --- |
| Linux x86_64 | Supported |
| Linux arm64 | Supported by the upstream project |
| macOS Apple Silicon | Supported through Docker Desktop |
| macOS Intel | Supported through Docker Desktop |
| Native macOS engine | Not supported |

The bundled runtime in `rootfs/` contains Linux/Android ELF binaries. macOS
cannot load those binaries directly, so the macOS launcher is a wrapper around
Docker, not a native port of the engine.

## Notes On The macOS Launcher

The macOS launcher is mostly a convenience layer. It lets you build and run a
normal-looking `wrapper` command on macOS, but the actual work still happens in
Docker because the bundled engine is Linux-based.

That means the Mac build is easier to use than typing the full Docker command
every time. It keeps the same wrapper options, mounts `rootfs/data` for account
state, publishes the usual local ports, and can run a doctor check when
something feels off.

It also means Docker Desktop is not optional. Docker has to be installed and
running, the container still needs privileged mode, and the first run can take a
while because the image may need to build. On Apple Silicon Macs, Docker may
also run the `linux/amd64` image through emulation.

Be careful with `rootfs/data`. It can contain account/session state, so it
should stay local and should not be committed.

## Quick Start On macOS

Install Docker Desktop first, then build the launcher:

```sh
mkdir -p build-macos
cmake -S . -B build-macos
cmake --build build-macos
```

Check your setup:

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

Use a custom image tag:

```sh
WRAPPER_DOCKER_IMAGE=wrapper-run:local ./build-macos/wrapper
```

## Docker Workflow

Build the image:

```sh
docker build --platform linux/amd64 --tag ghcr.io/worldobservationlog/wrapper:local .
```

Login when no account database exists:

```sh
docker run --platform linux/amd64 --privileged --rm -it \
  -v "$PWD/rootfs/data:/app/rootfs/data" \
  -e USERNAME="username" \
  -e PASSWORD="password" \
  ghcr.io/worldobservationlog/wrapper:local
```

Quit after login completes.

Run the service:

```sh
docker run --platform linux/amd64 --privileged --rm -it \
  -v "$PWD/rootfs/data:/app/rootfs/data" \
  -p 127.0.0.1:10020:10020 \
  -p 127.0.0.1:20020:20020 \
  -p 127.0.0.1:30020:30020 \
  ghcr.io/worldobservationlog/wrapper:local
```

Run diagnostics inside Docker:

```sh
docker run --platform linux/amd64 --privileged --rm \
  -v "$PWD/rootfs/data:/app/rootfs/data" \
  ghcr.io/worldobservationlog/wrapper:local --doctor
```

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
- Docker CLI and Docker daemon access
- expected Docker image platform
- default service ports
- Linux container capability requirements

Warnings are not always fatal. For example, the engine binary may be missing
from the source tree before a Docker build because Docker generates it inside
the image.

## Development Checks

Run the local smoke check:

```sh
./scripts/check.sh
```

By default, this checks shell syntax, C syntax, the macOS launcher build, and
basic wrapper commands. Docker image/container checks are skipped unless enabled:

```sh
RUN_DOCKER_CHECKS=1 ./scripts/check.sh
```

## Build From Source On Linux

Install dependencies:

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
unzip -d . android-ndk-r23b-linux.zip
```

Build:

```sh
mkdir build
cd build
cmake ..
make -j"$(nproc)"
```

## Command-Line Options

```text
Usage: wrapper [OPTION]...

  -h, --help                Print help and exit
  -V, --version             Print version and exit
  -H, --host=STRING         Host to bind on (default: 127.0.0.1)
  -D, --decrypt-port=INT    Decrypt service port (default: 10020)
  -M, --m3u8-port=INT       m3u8 service port (default: 20020)
  -A, --account-port=INT    Account service port (default: 30020)
  -P, --proxy=STRING        Proxy string (default: empty)
  -L, --login=STRING        Login in username:password format
  -F, --code-from-file      Read 2FA code from 2fa.txt
  -B, --base-dir=STRING     Runtime data directory
  -I, --device-info=STRING  Slash-separated device identity fields
```

On macOS, `-H` controls the host address used for Docker port publishing. Inside
the container, the Linux engine still binds to `0.0.0.0` so published ports work.

## Data And Safety Notes

`rootfs/data` stores local account state, tokens, and cache files. It is mounted
into Docker containers and intentionally excluded from Docker images. Do not
commit it.

Use this project only with an active subscription and in situations where you
have the right to access the content.

## Special Thanks

- Anonymous, for the original version of this project and the legacy Frida
  decryption method.
- chocomint, for arm64 support.
- WorldObservationLog, for maintaining the active upstream fork this work was
  based on.
