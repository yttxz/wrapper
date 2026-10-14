# Upstream integration

Reviewed on 2026-10-14 against live GitHub branch heads and fetched Git history.
This fork keeps the existing x86_64 wrapper API. Migration to `lite` is deferred
until a separate stability and client-compatibility review.

## Reviewed revisions

| Repository / branch | Revision | Role |
| --- | --- | --- |
| `yttxz/wrapper main` | `0ff54e8` | Fork baseline, including the merged amd64 release work |
| `WorldObservationLog/wrapper main` | `4d83f2f` | Integrated upstream head |
| `WorldObservationLog/wrapper lite` | `2fd7e0e` | Reviewed for migration scope and applicable fixes |
| `WorldObservationLog/wrapper arm64` | `c0a456f` | Separate architecture/runtime; not merged into x86_64 |

The shared ancestor of the fork and upstream `main` was `0d4823d`. The five
missing commits were:

| Commit | Change | Integration |
| --- | --- | --- |
| [`3630b88`](https://github.com/WorldObservationLog/wrapper/commit/3630b881e45bd4f8b03c0f2355dd26451510a011) | Key HTTP service, runtime template capture, session refresh, native exception barrier, cached-token fallback | Included with the fork's input, logging, socket and path safeguards |
| [`5a2db5e`](https://github.com/WorldObservationLog/wrapper/commit/5a2db5e04810e8b840babd078e9b8db1e5edf0c8) | `--key-port` in the CLI generator source | Included together with generated `cmdline.c` / `cmdline.h` |
| [`c99cf2a`](https://github.com/WorldObservationLog/wrapper/commit/c99cf2a66b19336f38d17a8c38025e5fe40d36c8) | English service documentation | Incorporated into the fork's existing Docker and diagnostics documentation |
| [`0bfc0ea`](https://github.com/WorldObservationLog/wrapper/commit/0bfc0ea4b0daf1f93f777a4dee8a49e2f36d7c40) | Enable template capture in release builds; expose `MYRELEASE` option | Included; `MYRELEASE` defaults to `ON` to preserve the fork's release behavior |
| [`4d83f2f`](https://github.com/WorldObservationLog/wrapper/commit/4d83f2feb396ca26847530ff7010ac6a4877f136) | Merge upstream PR #64 | Preserved through an actual merge of upstream history |

## Integration adjustments

- Port `40020` is exposed in Docker, published on localhost in Compose, and
  included in doctor and smoke checks. Compose explicitly selects `linux/amd64`
  and the privileges required by its existing launcher.
- The new HTTP service shares the fork's listening/accept helpers, handles
  fragmented headers, limits headers to 4 KiB and decoded parameters to 32/240
  bytes, and returns correctly framed HTTP errors.
- The legacy decrypt connection owns the native session until it closes. Key
  requests return `503` while it is active, preventing native context resets
  and hook capture from racing with sample processing. Exceptions release the
  session lock and disarm capture before returning `500`.
- Upstream's unconditional persistent-key log/file dump is removed. Cached
  tokens are read with bounded paths and file sizes, without truncation or
  printing token prefixes. Interactive password and 2FA defaults remain intact.
- The pinned Dobby `v1.0.5` header was checked against the new instrumentation
  callback. Native Linux builds validate the actual dependency, in addition to
  host syntax checks using test stubs.

Three corrections from later `lite` development apply without replacing the API:

- [`82a387e`](https://github.com/WorldObservationLog/wrapper/commit/82a387e):
  enlarge the same vendor `URLRequest` stack objects from 512 to 2048 bytes.
- [`64d1b65`](https://github.com/WorldObservationLog/wrapper/commit/64d1b65):
  require an explicit key URI and reject the shared prefetch URI for real tracks.
- [`d873ca2`](https://github.com/WorldObservationLog/wrapper/commit/d873ca2):
  enforce LF endings for shell scripts.

## Deferred migration

[`lite`](https://github.com/WorldObservationLog/wrapper/tree/lite) replaces the
legacy binary services and account endpoint with a single HTTP API on `12340`:
`/m3u8`, `/key`, `/lyrics`, `/license`, `/webplayback`, and `/status`. It also
introduces QEMU guest assets, new native launchers, background token refresh,
different device identification, and different release workflows.

It is upstream's default branch, but default-branch status alone does not prove
stability for this fork's clients. A future migration should validate client
protocol changes, login/2FA and token renewal, persistence, supported host
platforms, QEMU packaging, and recovery behavior before replacing this release.

The open upstream PRs were also reviewed for scope: [#53](https://github.com/WorldObservationLog/wrapper/pull/53)
and [#72](https://github.com/WorldObservationLog/wrapper/pull/72) propose further
token/daemon recovery; [#71](https://github.com/WorldObservationLog/wrapper/pull/71)
adds registry publishing; [#70](https://github.com/WorldObservationLog/wrapper/pull/70)
and [#74](https://github.com/WorldObservationLog/wrapper/pull/74) concern `lite`
Docker publishing/builds. These unmerged proposals are not treated as released
upstream behavior and are not included in this sync.
