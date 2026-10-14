ARG BUILD_PLATFORM=linux/amd64
ARG RUNTIME_PLATFORM=linux/amd64

FROM --platform=${BUILD_PLATFORM} debian:13.2 AS build
ARG TARGET_ARCH=amd64
ARG NDK_VERSION=23
ARG ANDROID_NDK_SHA256=c6e97f9c8cfe5b7be0a9e6c15af8e7a179475b7ded23e2d1c1fa0945d6fb4382
ARG ANDROID_TZDATA_COMMIT=0470df3d38d8e08932ebbe08b3d8ec9bbdcd403f
ARG ANDROID_TZDATA_SHA256=479e83ca4d289b2ae3d08eb222a5167a3c8bff185f2ccac932458e96bd6489ee

SHELL ["/bin/bash", "-c"]

WORKDIR /app

COPY ./ ./

RUN if [[ -x /app/wrapper-lite && ! -f /app/CMakeLists.txt ]]; then \
        touch /use_prebuilt; \
    elif [[ ! -f /app/CMakeLists.txt ]]; then \
        echo "ERROR: build context contains neither CMakeLists.txt nor a prebuilt /app/wrapper-lite." >&2; \
        exit 1; \
    fi

RUN --mount=type=cache,target=/var/lib/apt,sharing=locked \
    --mount=type=cache,target=/var/cache/apt,sharing=locked \
    if [[ ! -f /use_prebuilt ]]; then \
        apt-get update; \
        apt-get install -y \
            ca-certificates \
            aria2 \
            build-essential \
            cmake \
            unzip \
            git \
            lsb-release \
            gnupg; \
    fi

RUN if [[ ! -f /use_prebuilt ]]; then \
        aria2c -o android-ndk-r${NDK_VERSION}b-linux.zip https://dl.google.com/android/repository/android-ndk-r${NDK_VERSION}b-linux.zip; \
        echo "${ANDROID_NDK_SHA256}  android-ndk-r${NDK_VERSION}b-linux.zip" | sha256sum -c -; \
        unzip -q -d /app android-ndk-r${NDK_VERSION}b-linux.zip; \
        rm android-ndk-r${NDK_VERSION}b-linux.zip; \
    fi

RUN if [[ ! -f /use_prebuilt ]]; then \
        test -f /app/CMakeLists.txt || { echo "ERROR: source build context is missing CMakeLists.txt." >&2; exit 1; }; \
        mkdir -p build; \
        cmake -S /app -B /app/build -DCMAKE_BUILD_TYPE=Release; \
        cmake --build /app/build -j$(nproc); \
    else \
        test -x /app/wrapper-lite || { echo "ERROR: prebuilt context is missing executable /app/wrapper-lite." >&2; exit 1; }; \
        chmod +x /app/wrapper-lite; \
    fi

RUN if [[ ! -f /use_prebuilt ]]; then \
        mkdir -p /app/rootfs/system/usr/share/zoneinfo; \
        aria2c -q -d /tmp -o android-tzdata.b64 "https://android.googlesource.com/platform/system/timezone/+/${ANDROID_TZDATA_COMMIT}/output_data/iana/tzdata?format=TEXT"; \
        base64 --decode /tmp/android-tzdata.b64 > /app/rootfs/system/usr/share/zoneinfo/tzdata; \
        rm /tmp/android-tzdata.b64; \
    fi; \
    test -x /app/wrapper-lite; \
    test -f /app/rootfs/system/bin/lite; \
    test -f /app/rootfs/system/bin/linker64; \
    test -f /app/rootfs/system/usr/share/zoneinfo/tzdata; \
    echo "a41b5d356aea97a529fe27e0f7316d2f9d946d75927476cf9cf1b90637d00505  /app/rootfs/system/etc/security/cacert.pem" | sha256sum -c -; \
    echo "${ANDROID_TZDATA_SHA256}  /app/rootfs/system/usr/share/zoneinfo/tzdata" | sha256sum -c -

FROM --platform=${RUNTIME_PLATFORM} debian:13.2

WORKDIR /app
COPY --from=build /app/wrapper-lite /app/wrapper-lite
COPY --from=build /app/wrapper-lite-rootless /app/wrapper-lite-rootless
COPY --from=build /app/rootfs /app/rootfs
COPY entrypoint.sh /app/entrypoint.sh
COPY scripts/doctor.sh /app/scripts/doctor.sh
COPY scripts/runtime-dns.sh /app/scripts/runtime-dns.sh
RUN mkdir -p /app/rootfs/etc && cp /etc/resolv.conf /app/rootfs/etc/resolv.conf
RUN mkdir -p /app/rootfs/data/data/com.apple.android.music/files && \
    chmod +x /app/entrypoint.sh /app/scripts/doctor.sh /app/scripts/runtime-dns.sh

ENTRYPOINT ["/app/entrypoint.sh"]

EXPOSE 12340
