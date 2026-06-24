# wrapper

A tool to decrypt Apple Music songs. An active subscription is still needed.

Supports only x86_64 and arm64 Linux for the functional decryption engine.

Native macOS builds are detected by CMake and produce a command-compatible
Docker-backed launcher. The bundled runtime in `rootfs/` is made of
Android/Linux x86-64 ELF binaries, which macOS cannot load natively, so the
launcher builds and runs the Linux engine through Docker while mounting the
local `rootfs/data` account state.

## Installation

Installation methods:

- [Docker](#docker) (recommended)
- [macOS launcher](#macos-launcher)
- [Diagnostics](#diagnostics)
- Prebuilt binaries (from [releases](https://github.com/WorldObservationLog/wrapper/releases) or [actions](https://github.com/WorldObservationLog/wrapper/actions))
- [Build from source](#build-from-source)

### Docker

Available for x86_64 and arm64. Need to download prebuilt version from releases or actions.

1. Build image:

```
docker build --platform linux/amd64 --tag ghcr.io/worldobservationlog/wrapper:local .
```

Local `rootfs/data` account state is intentionally excluded from Docker images.
Always mount `rootfs/data` at runtime.

2. Login:

```
docker run --platform linux/amd64 --privileged --rm -it \
  -v "$PWD/rootfs/data:/app/rootfs/data" \
  -e USERNAME="username" \
  -e PASSWORD="password" \
  ghcr.io/worldobservationlog/wrapper:local
```

Quit after this (using Ctrl-C).

3. Run:

```
docker run --platform linux/amd64 --privileged --rm -it \
  -v "$PWD/rootfs/data:/app/rootfs/data" \
  -p 127.0.0.1:10020:10020 \
  -p 127.0.0.1:20020:20020 \
  -p 127.0.0.1:30020:30020 \
  ghcr.io/worldobservationlog/wrapper:local
```

### macOS launcher

Docker Desktop is required.

Build the launcher:

```
mkdir -p build-macos
cmake -S . -B build-macos
cmake --build build-macos
```

Run it like the normal wrapper:

```
./build-macos/wrapper
```

On first run, the launcher builds a local `wrapper-macos:local` Docker image,
mounts `rootfs/data`, publishes the decrypt, m3u8, and account ports, and starts
the Linux engine in Docker. The wrapper's `-H` value is used as the macOS host
bind address; inside the container the engine binds to `0.0.0.0` so published
ports work correctly.

To use a different image tag:

```
WRAPPER_DOCKER_IMAGE=wrapper-run:after ./build-macos/wrapper
```

### Diagnostics

Run the shared doctor check when Docker, mounts, cached account data, ports, or
the macOS launcher are not behaving as expected:

```
./scripts/doctor.sh
```

The macOS launcher exposes the same check:

```
./build-macos/wrapper --doctor
```

Inside the Docker image:

```
docker run --platform linux/amd64 --privileged --rm \
  -v "$PWD/rootfs/data:/app/rootfs/data" \
  wrapper-macos:local --doctor
```

For a fast local development smoke check:

```
./scripts/check.sh
```

Set `RUN_DOCKER_CHECKS=1` to include the Docker build and container doctor
checks.

### Build from source

On macOS ARM64:

```
mkdir build
cd build
cmake ..
make
```

This builds the Docker-backed launcher. It does not run the bundled
Android/Linux engine as native Mach-O code.

On Linux:

1. Install dependencies:

- Build tools:

  ```
  sudo apt install build-essential cmake curl unzip git
  ```

- LLVM:

  ```
  sudo bash -c "$(wget -O - https://apt.llvm.org/llvm.sh)"
  ```

- Android NDK r23b:
  ```
  curl -fLO https://dl.google.com/android/repository/android-ndk-r23b-linux.zip
  unzip -d . android-ndk-r23b-linux.zip
  ```

2. Build:

```
git clone https://github.com/WorldObservationLog/wrapper
cd wrapper
mkdir build
cd build
cmake ..
make -j$(nproc)
```

## Usage

```
Usage: wrapper [OPTION]...

  -h, --help              Print help and exit
  -V, --version           Print version and exit
  -H, --host=STRING         (default=`127.0.0.1')
  -D, --decrypt-port=INT    (default=`10020')
  -M, --m3u8-port=INT       (default=`20020')
  -A, --account-port=INT    (default=`30020')
  -P, --proxy=STRING        (default=`')
  -L, --login=STRING        (username:password)
  -F, --code-from-file      (default=off)
```

## Special thanks

- Anonymous, for providing the original version of this project and the legacy Frida decryption method.
- chocomint, for providing support for arm64 arch.
