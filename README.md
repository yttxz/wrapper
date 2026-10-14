# wrapper-lite

This fork runs WorldObservationLog's Lite HTTP API for the current
`apple-music-downloader`. An active Apple Music subscription is required.

Docker on `linux/amd64` is the supported release path, including on macOS through
Docker Desktop. The engine still uses the bundled Linux/Android runtime.
Native Linux launchers are included for advanced users. Upstream QEMU sources
remain available for development; this fork does not yet package or validate
QEMU releases.

## Migration from the old wrapper

The API now listens on one port, `12340`. The old binary services on `10020` and
`20020`, account endpoint on `30020`, and flat key response on `40020` have been
replaced. The updated local downloader already expects Lite; set:

```yaml
general:
  lite-server: "http://127.0.0.1:12340"
```

Keep your existing `rootfs/data` directory. This fork continues to store account
files under `rootfs/data/data/com.apple.android.music/files`, so the existing
Docker volume remains valid. Existing accounts without an `ANDROID_ID` file
reuse this fork's legacy device identity; new logins use upstream's persistent
username-derived identity. Account reuse still needs an authenticated check on
your installation. Back up the data directory before switching services.

The old `Wrapper.x86_64.latest.zip` release is retained. Lite releases use
`Wrapper.Lite.x86_64.latest.zip` and tag `wrapper.lite.x86_64.latest`, so the old
release remains available for rollback. See [UPSTREAM.md](UPSTREAM.md) for the
research, integration decisions and validation limits.

## Build and run with Docker

Build from source:

```sh
docker buildx build --platform linux/amd64 --load --tag wrapper-lite:local .
```

The Lite release ZIP is also a complete Docker build context. Its binaries and
runtime inputs are prebuilt; building it skips the compiler and NDK download:

```sh
unzip Wrapper.Lite.x86_64.latest.zip -d wrapper-lite-release
cd wrapper-lite-release
docker buildx build --platform linux/amd64 --load --tag wrapper-lite:local .
```

First login, or refresh an expired account:

```sh
read "APPLE_ID?Apple ID: "
docker run --platform linux/amd64 --privileged --rm -it \
  -v "$PWD/rootfs/data:/app/rootfs/data" \
  -e USERNAME="$APPLE_ID" \
  wrapper-lite:local --login "$APPLE_ID"
```

The password and six-digit 2FA code are prompted with terminal echo disabled.
Login caches the account tokens and exits. Start the service separately:

```sh
docker run --platform linux/amd64 --privileged --name wrapper-lite --rm -it \
  -v "$PWD/rootfs/data:/app/rootfs/data" \
  -p 127.0.0.1:12340:12340 \
  wrapper-lite:local
```

`docker compose up --build` uses the same platform, privileges, data mount and
localhost port. Use the interactive command for first login. The entrypoint can
also log in automatically when `USERNAME` is set and required account caches
are absent; it starts the service only after login succeeds.

For an explicitly chosen non-interactive password, set both
`WRAPPER_PASSWORD_FROM_ENV=1` and `PASSWORD` with `USERNAME`, or use the legacy
`--login USER:PASSWORD` argument. Interactive login avoids storing passwords in
Docker environment variables or process arguments.

File-based 2FA is opt-in with `--code-from-file` or, for automatic login,
`WRAPPER_2FA_FROM_FILE=1`. In another terminal:

```sh
umask 077
printf '%s' 123456 > rootfs/data/data/com.apple.android.music/files/2fa.txt
```

Only a private regular file with exactly six digits is accepted; surrounding
whitespace is allowed. Symlinks and special files are refused. Invalid files are
consumed and the wrapper keeps waiting. `WRAPPER_2FA_TIMEOUT_SECONDS` accepts
`1` through `600` and defaults to `60`.

## HTTP API

Success responses use `{"code":0,"msg":"SUCCESS","data":{...}}`.
Application errors carry a nonzero `code`; clients must inspect that field even
when the HTTP status is `200`, matching upstream's API.

| Endpoint | Method | Parameters |
| --- | --- | --- |
| `/status` | GET | Returns `data.regions`; an unauthenticated account reports an empty array |
| `/m3u8` | GET | Numeric `adamId` |
| `/key` | GET | Numeric `adamId`, explicit percent-encoded `skd://` `uri` |
| `/lyrics` | GET | `adamId`, optional `language`, `syllable`, `script` |
| `/webplayback` | GET | `adamId` |
| `/license` | POST | JSON `adamId`, `challenge`, `uri`; optional `drm-type` of `wv` or `pr` |

The `/key` data includes the content template expected by the downloader.
The shared prefetch URI is accepted only with `adamId=0`. Adam IDs are limited
to 32 bytes and key URIs to 240 bytes. HTTP payloads are capped at 1 MiB;
license challenges are capped at 256 KiB and license URIs at 4 KiB.

Tokens refresh in the background every 1800 seconds by default. Set
`--token-refresh-interval` to change the interval. Native refresh exceptions are
logged without exposing protocol data and leave the service running.

## Runtime settings

- `--host`, `--port`: listen address and port. Native defaults are `127.0.0.1`
  and `12340`; Docker binds internally to `0.0.0.0` for forwarding.
- `--base-dir`: account directory inside the runtime. The fork default is
  `/data/data/com.apple.android.music/files`.
- `--proxy`: HTTP/SOCKS proxy for runtime requests.
- `--device-info`: override the nine slash-separated device identity fields.
- `--debug` or `--log-level`: select logging detail. Debug logging keeps TLS
  verification enabled. `--insecure-tls` explicitly disables verification for
  development.
- `WRAPPER_CA_BUNDLE`: override the CA file inside the runtime. The default is
  `/system/etc/security/cacert.pem`.
- `WRAPPER_DNS`: optionally provide comma-separated nameserver IP addresses.
  Docker copies its resolver configuration into the runtime; public DNS servers
  are not hardcoded.
- `WRAPPER_DISABLE_OFFLINE=1`: force the streaming playback channel.
- `WRAPPER_CHOWN_DATA=1`: explicitly request ownership repair for mounted data.
- `WRAPPER_VERBOSE_RUNTIME_LOGS=1`: opt into raw vendor logs at debug level;
  these can contain sensitive protocol data.

New token caches are written atomically with mode `600`. Normal logs omit token
prefixes, key URIs, response bodies and request query strings. Keep account data
local and publish port `12340` on localhost unless an external access boundary
has been configured. The API itself has no authentication.

## Checks and releases

```sh
./scripts/doctor.sh
./scripts/check.sh
RUN_DOCKER_CHECKS=1 WRAPPER_CHECK_DATA_DIR=/tmp/wrapper-empty-data ./scripts/check.sh
```

Docker checks start the real engine with the selected data directory and verify
`/status` plus invalid-input response envelopes. Use an empty directory to avoid
reading or refreshing your usual account state.

`./scripts/http-contract-check.sh build/_deps/cjson-src` tests the production
HTTP handlers with a separately linked fixture backend. It exercises all six
endpoints and both license types without calling Apple. The fixture backend is
never linked into the production engine or included in releases.

Native Linux builds use Android NDK r23b and CMake 3.25 or newer:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
```

Build outputs are `rootfs/system/bin/lite`, `wrapper-lite`, and
`wrapper-lite-rootless`. Native launchers preserve the fork's checked namespace,
mount and runtime-directory setup. Rootless execution requires permitted Linux
user namespaces.

CI verifies the NDK download, timezone data and dated CA bundle by SHA-256,
builds native artifacts and Docker contexts, runs input/HTTP/startup tests, and
checks that actual Android libcurl rejects an untrusted TLS certificate. It then
packages the explicit release manifest and validates the extracted prebuilt
Docker context before updating the Lite release tag.

```sh
./scripts/package-release.sh Wrapper.Lite.x86_64.latest.zip
```

Releases exclude account data, resolver state, source files, test programs and
toolchains. The public CA bundle comes from curl's Mozilla extraction dated
2026-09-25 and is distributed under the Mozilla source's MPL 2.0 terms; its source
and checksum are recorded in [UPSTREAM.md](UPSTREAM.md).

## Acknowledgements

WorldObservationLog maintains upstream Lite; zhaarey and the original authors
provided the earlier wrapper lineage. This project's code is licensed under the
[MIT license](LICENSE). Use it with an active subscription and content you have
the right to access.
