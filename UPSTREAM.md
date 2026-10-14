# Lite migration research and integration

Reviewed on 2026-10-14. The migration merges WorldObservationLog's complete
`lite` history through `2fd7e0eaef081f93cf687428b965674937832ba4` into the fork's
`main` baseline `2f92215`, which already includes the legacy upstream sync.
All 52 commits after upstream `main` (`4d83f2f`) are represented in ancestry.

The client baseline is the **local** checkout at
`/Users/ari/Downloads/apple-music-downloader`, commit `d4100a3`, including its
merge of zhaarey through `4741380`. Its current executable uses `LiteServer`:
M3U8 goes through `internal/wrapper`, ALAC/Atmos use the template-based Go
engine, and AAC/license and lyrics requests use Lite endpoints. The remaining
old TCP config fields are compatibility inputs, not an active backend.

## Research findings

- The current Lite API matches the local downloader's `code/msg/data` envelope,
  six endpoints, `regions` status shape, template fields and `wv`/`pr` license
  requests. The older wrapper's ports and flat key JSON cannot satisfy this
  client by changing a port alone.
- Upstream's [latest successful build](https://github.com/WorldObservationLog/wrapper/actions/runs/36431829492)
  builds native Linux and multi-platform QEMU packages and boots the Linux
  guest on Ubuntu 22.04 and 24.04. This proves build/unauthenticated-startup
  coverage, not subscriber download or macOS runtime stability.
- [Issue #75](https://github.com/WorldObservationLog/wrapper/issues/75) reports
  disabled TLS verification and hardcoded AliDNS. Both were confirmed in
  `lite/apple_api.cpp` and `lite/bridge.cpp` at the merged revision. The fork now
  configures peer/hostname verification for direct web requests and native curl
  calls, supplies a CA file, and copies Docker DNS into the guest. Debug logging
  does not disable TLS; `--insecure-tls` is an explicit development opt-in.
- The upstream file-based 2FA path writes six digits plus a terminator into a
  fixed 64-byte password buffer at an unchecked offset. An isolated sanitizer
  reproduction with a 58-character dummy password confirmed overflow. The
  migration uses bounded password input and dynamically sized composition,
  hidden terminal prompts, explicit file mode, and private regular-file checks.
- [PR #74](https://github.com/WorldObservationLog/wrapper/pull/74) repairs a
  missing `wget` dependency in upstream's Dockerfile. This fork avoids that
  mutable LLVM install script entirely: the pinned NDK supplies the compilers.
- Open upstream registry-publishing PRs #70/#71 and legacy recovery PRs #53/#72
  are not merged behavior and are not imported. QEMU firmware/glibc fixes and
  lyrics/license/token fixes already merged into Lite remain in history.

## Deliberate fork choices

The supported release is still Linux x86_64 in Docker. Native launchers preserve
our checked namespace/mount/directory helpers. Upstream QEMU source is retained
but its packaging workflow is not enabled in this fork; QEMU support needs a
separate host-platform review. Its Apple Silicon launcher uses TCG software
emulation, so a speed improvement over Docker has not been established.

The existing account directory and default legacy device identity are retained
for migrated accounts without `ANDROID_ID`. New logins persist upstream's
username-derived identity. Ownership repair, environment-password login,
file-based 2FA, and raw vendor logging remain explicit opt-ins. New cache files
are bounded on read and replaced atomically with mode `600`. Query strings,
response bodies, token prefixes and full key URIs are omitted from normal logs.

The old engine, generated legacy CLI and superseded tests are removed. No
compatibility proxy invents old endpoint behavior. The test backend is linked
only into a host fixture executable; production uses the Android libraries.
Native linker errors are enforced rather than suppressed.

Lite releases use a distinct tag and ZIP name, preserving the old release for
rollback. ZIP manifests exclude account data, resolver state, test executables,
source and toolchains. The prebuilt Docker path is retained.

## Pinned inputs

| Input | Pin / checksum |
| --- | --- |
| Android NDK | r23b; `c6e97f9c8cfe5b7be0a9e6c15af8e7a179475b7ded23e2d1c1fa0945d6fb4382` |
| cJSON | `v1.7.19` |
| Dobby | `v1.0.5`, matching the instrumentation API; upstream's mutable `master` is not used |
| Android tzdata | Original fork commit `0470df3d38d8e08932ebbe08b3d8ec9bbdcd403f`; SHA-256 `479e83ca4d289b2ae3d08eb222a5167a3c8bff185f2ccac932458e96bd6489ee` |
| Public CA bundle | [curl's Mozilla extraction dated 2026-09-25](https://curl.se/ca/cacert-2026-09-25.pem); published and locally checked SHA-256 `a41b5d356aea97a529fe27e0f7316d2f9d946d75927476cf9cf1b90637d00505` |

The CA file is committed for offline/reproducible packaging. Its provenance,
MPL 2.0 terms and extraction limitations are documented on
[curl's CA extraction page](https://curl.se/docs/caextract.html). The HTTPS setup
follows [curl's certificate verification guidance](https://curl.se/docs/sslcerts.html).

## Validation and limits

- Native NDK builds use the actual pinned dependencies and resolve vendor symbols.
- Sanitizer tests cover long passwords, malformed/NUL 2FA, private-file checks,
  symlinks/FIFOs, ID/URI limits, and private cache replacement.
- The production HTTP handlers run against a separately linked fixture backend.
  All six endpoints, both license types and malformed requests are checked.
  An independent Go probe built from the local downloader accepts these responses.
- Real Docker startup with empty account data checks `/status` and invalid-input
  envelopes. Image and ZIP checks ensure no account state is included.
- A native Android probe uses the production CurlEasy implementation against a
  local TLS server: the untrusted certificate is rejected and an explicitly
  supplied matching CA succeeds.

These checks do not assert a successful subscriber login, token renewal over
hours, media download, or live template/license retrieval. Existing account reuse
and final switching of the running service require authenticated verification.
Rootless namespace execution and QEMU/macOS/Windows guest execution are not
claimed. The existing service is left available during migration review.
