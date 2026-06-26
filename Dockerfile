ARG BUILD_PLATFORM=linux/amd64
ARG RUNTIME_PLATFORM=linux/amd64

FROM --platform=${BUILD_PLATFORM} debian:13.2 AS build
ARG TARGET_ARCH=amd64
ARG NDK_VERSION=23
ARG ANDROID_TZDATA_URL=https://android.googlesource.com/platform/system/timezone/+/refs/heads/main/output_data/iana/tzdata?format=TEXT

SHELL ["/bin/bash", "-c"]

WORKDIR /app

RUN --mount=type=cache,target=/var/lib/apt,sharing=locked \
    --mount=type=cache,target=/var/cache/apt,sharing=locked \
    apt-get update; \
    apt-get install -y \
        ca-certificates \
        aria2 \
        build-essential \
        cmake \
        unzip \
        git \
        lsb-release \
        gnupg

RUN aria2c -o android-ndk-r${NDK_VERSION}b-linux.zip https://dl.google.com/android/repository/android-ndk-r${NDK_VERSION}b-linux.zip && \
    unzip -q -d /app android-ndk-r${NDK_VERSION}b-linux.zip && \
    rm android-ndk-r${NDK_VERSION}b-linux.zip

COPY ./ ./
RUN mkdir -p build && \
    cmake -S /app -B /app/build -DTARGET_ARCH=${TARGET_ARCH} && \
    cmake --build /app/build -j$(nproc)

RUN mkdir -p /app/rootfs/system/usr/share/zoneinfo && \
    aria2c -q -d /tmp -o android-tzdata.b64 "${ANDROID_TZDATA_URL}" && \
    base64 --decode /tmp/android-tzdata.b64 > /app/rootfs/system/usr/share/zoneinfo/tzdata && \
    rm /tmp/android-tzdata.b64

FROM --platform=${RUNTIME_PLATFORM} debian:13.2

WORKDIR /app
COPY --from=build /app/wrapper /app/wrapper
COPY --from=build /app/rootfs /app/rootfs
COPY entrypoint.sh /app/entrypoint.sh
COPY scripts/doctor.sh /app/scripts/doctor.sh
RUN mkdir -p /app/rootfs/data/data/com.apple.android.music/files && \
    chmod +x /app/entrypoint.sh /app/scripts/doctor.sh

ENTRYPOINT ["/app/entrypoint.sh"]

EXPOSE 10020 20020 30020
