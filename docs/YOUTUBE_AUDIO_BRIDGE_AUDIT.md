# YouTube audio / Yoinks-MPD integration audit

Status: **C++ local HTTP proxy implementation present; latest target-device build and all 11 CTest cases passed on Raspberry Pi; live YouTube/CDN and audible hardware playback remain unverified.**

> Current-state notice: this audit contains chronological historical sections. The later dated addenda supersede earlier prototype status, architecture, and test statements where they conflict. The accepted transport is the local C++ HTTP proxy, not a FIFO.

Branch: `feature/youtube-audio-bridge`  
Repository: https://github.com/bartoszruta26-droid/malp-

## User requirements

- Raspberry Pi already runs MPD and MALP already controls it.
- Android YouTube Share should pass video or playlist URLs to the Pi.
- Use yt-dlp, Yoinks workflow/integration module and MPD.
- Stream bytes as they are fetched; do not save complete audio files or media temporary files.
- The accepted media transport is a local C++ HTTP proxy consumed through MPD's CURL input plugin; FIFO transport is historical and superseded.
- New Raspberry Pi-side implementation and glue must use Bash or C++ only. Do not use Python.
- Preserve existing MPD and MALP functionality.
- Prefer the best available source audio format supported by MPD. Do not transcode lossy audio to FLAC and describe it as lossless.

## Repository baseline

- Repository: `bartoszruta26-droid/malp-`
- Base branch: `master`
- Baseline commit recorded earlier: `fd72e3e41ef2f60af24d53beb0f0b6db7a34d4e2`
- Feature branch: `feature/youtube-audio-bridge`
- Android application is Java/Gradle; the Bash/C++ restriction applies to the new Pi-side integration, not to rewriting the existing Android app.

## Confirmed MPD design constraint

MPD's documented `pipe` plugin is an **output** plugin: it starts a command and writes decoded PCM to the command's standard input. MPD's `fifo` output plugin also sends PCM out of MPD. Neither is an input pipe for feeding yt-dlp bytes into MPD.

The documented MPD input plugin reference does not describe a general-purpose stdin/FIFO input plugin. A local FIFO may or may not work through the local-file input path for a particular MPD build; this must be tested on the actual Raspberry Pi. Do not assume `mpc add /path/to/fifo` is supported.

Official reference: https://mpd.readthedocs.io/en/stable/plugins.html

If the installed MPD cannot read a FIFO as a local source, the direct FIFO architecture is blocked without a native C++ MPD input plugin or a separately approved local HTTP input fallback. Do not silently switch to HTTP.

## Added repository files

- `docs/yoinks-mpd/README.md` — architecture, MPD input limitation, audio policy, playlist design, security, verification plan and implementation phases.
- `scripts/yoinks-mpd/yt-dlp-to-fifo.sh` — experimental single-video producer. It sends yt-dlp stdout to a newly created FIFO, rejects playlist URLs, refuses existing paths/symlinks and removes only the FIFO it created.
- Root `README.md` — links to the new design and script and explicitly marks the approach experimental.

## Prototype limitations

The script is a producer-side proof-of-concept, not a complete Yoinks-MPD daemon:
- It supports one video only; playlist enumeration and per-track FIFO lifecycle are not implemented.
- It does not add items to MPD's queue.
- It does not establish that MPD can open/decode the FIFO.
- It does not implement the Pi-side authenticated request service.
- It does not claim end-to-end playback or Raspberry Pi hardware verification.
- It uses Bash and yt-dlp only; no Python code or runtime is introduced.

The original Yoinks TUI must not be assumed to provide non-interactive stdout streaming or playlist-to-MPD control. Yoinks-MPD is the integration module to be implemented around the yt-dlp engine.

## Required next work

1. Run `bash -n scripts/yoinks-mpd/yt-dlp-to-fifo.sh` in a Bash environment.
2. Test producer bytes with a separate FIFO reader, verify stderr/stdout separation, error propagation, SIGINT/SIGTERM cleanup and no media files on disk.
3. On the Raspberry Pi, record MPD version/build and test whether the local-file input can consume a FIFO and decode the selected source codec.
4. If FIFO input works, implement a Bash/C++ process supervisor and MPD queue integration. If it does not, stop and request approval for a native C++ input plugin or HTTP fallback.
5. Implement playlist expansion as separate ordered items, never as concatenated playlist stdout.
6. Connect MALP Share intents only after the Pi-side contract is stable.
7. Add automated tests and verify the existing MALP functions remain intact.
8. Merge to `master` only after successful CI and Raspberry Pi playback/regression tests.

## Test status

No Bash syntax test, FIFO consumer test, MPD playback test, Raspberry Pi test, Android build or emulator test is claimed by this audit. These require execution in a suitable environment. The GitHub file commits only establish that the source files were written to the feature branch.


## Raspberry Pi installer

Added:
- scripts/yoinks-mpd/install.sh — APT prerequisite bootstrapper; downloads the official architecture-specific standalone yt-dlp release, verifies SHA-256 against upstream SHA2-256SUMS, installs the binary and producer script, and runs version/syntax checks.
- docs/yoinks-mpd/INSTALL_RASPBERRY_PI.md — step-by-step Raspberry Pi OS/Debian installation and safe verification guide.

Safety boundary:
- Does not install, restart, reconfigure or alter MPD.
- Does not alter the MPD queue or expose a network service.
- Does not install Python packages or a system Python runtime.
- Does not claim end-to-end playback. MPD FIFO input remains a target-device compatibility proof.
- Supports aarch64, armv7l and x86_64; unsupported architectures fail closed.

The installer has not been executed on Raspberry Pi hardware in this environment. Target-device download, checksum and runtime verification remain pending.


## Offline regression tests — next implementation increment

Added `tests/yoinks-mpd/test-yt-dlp-to-fifo.sh`. It runs against a mock `yt-dlp` executable and does not contact YouTube or issue commands to MPD. The cases cover:
- byte-for-byte transfer through a FIFO and separation of diagnostics onto stderr;
- rejection of playlist URLs and lookalike YouTube hosts;
- preservation of existing regular files and symlink targets;
- propagation of a non-zero yt-dlp exit status and cleanup of the producer-owned FIFO.

Run with:

```bash
bash -n scripts/yoinks-mpd/yt-dlp-to-fifo.sh
bash -n tests/yoinks-mpd/test-yt-dlp-to-fifo.sh
bash tests/yoinks-mpd/test-yt-dlp-to-fifo.sh
```

**Execution status:** the user executed the following commands on the Raspberry Pi checkout and reported successful completion:

```bash
bash -n scripts/yoinks-mpd/yt-dlp-to-fifo.sh
bash -n tests/yoinks-mpd/test-yt-dlp-to-fifo.sh
bash tests/yoinks-mpd/test-yt-dlp-to-fifo.sh
```

All six reported cases passed: binary FIFO transfer and cleanup; playlist rejection; lookalike-host rejection; preservation of existing files; symlink rejection; and yt-dlp failure propagation with cleanup. This is user-reported target-device execution, not an independently reproduced run in the development environment. It does not establish MPD FIFO input compatibility or end-to-end playback.


## Verification addendum — session-registry lock fix (2026-11-17)

- Commit verified: `2f40a5f8f3c5fc438d0462e9b44961126bbee8dd` (`Avoid joining upstream worker under session registry lock`).
- The user pulled this commit onto the Raspberry Pi and confirmed that `discard_stream` is decided while holding the registry mutex, while `stream->cancel()` and `stream->wait()` execute after the lock scope.
- User-reported target-device build: `cmake --build /tmp/yoinks-mpd-audit --parallel 2` completed successfully.
- User-reported target-device CTest: **11/11 passed**, total 6.70 seconds, including `lazy_stream_session_registry`, HTTP API integration, byte-stream integration, and `real_mpd_http_proxy_playback`.
- The matching GitHub Actions C++ workflow run **44104070635** for the exact commit `2f40a5f8f3c5fc438d0462e9b44961126bbee8dd` completed with conclusion `success`.
- The production service remained `active/running`, with `NRestarts=0`. Production binary SHA-256 remained `21bbe326959062c7d036a5f069bcd6dca7de0a02eccb0ee3a048d9c6eadbf8f8`; no production deployment or restart occurred. The local audit build is not the installed production binary.
- Git checkout reported HEAD and origin at `2f40a5f8` and an empty `git status --short`.
- Review note: existing registry tests cover resolver-in-flight cancellation and queue reconciliation, but do not yet deterministically force the narrow race after upstream worker start and before stream publication. Add a controlled regression test before declaring this concurrency edge fully covered.
- This verification does not establish live YouTube/CDN playback, audible hardware output, or persistent-storage/swap behavior during a long real stream.


## Follow-up review — release during lazy activation (2026-11-17)

- Code change under verification: commits `25030068`, `5fb5d0fa`, and `36d9f70a` on `feature/youtube-audio-bridge`.
- Review identified a lifecycle gap: `release(id, true)` could set and then immediately clear the activation reservation while an earlier resolver was still running; it also did not signal that resolver's cancellation token. This could permit overlapping activation attempts.
- The implementation now signals the current session cancellation token during `release(id, true)`. If activation is already in progress, release leaves its reservation intact for the activation to finish. A later explicit retry receives a new uncancelled token.
- Added a deterministic regression test that holds the resolver until cancellation, verifies release signals it, verifies overlapping activation is rejected while the resolver remains blocked, and verifies a later retry gets a fresh token.
- The latest test commit is `36d9f70aed0bef8e9863c277165a8ad1dea28a65`. GitHub Actions was still running at the time this addendum was written. Raspberry Pi build/CTest verification is pending; do not treat this change as validated until the user reports those results.


## Follow-up review — release after upstream start, before publication (2026-11-17)

- Review found a second lifecycle edge distinct from resolver cancellation: if `release(id, true)` occurs after `HttpUpstreamStream::start()` succeeds but before the stream is published into the session entry, the release path cannot see or cancel that local stream object. The activation subsequently discards and joins it, but the previous implementation did not clear its `activating` reservation. This could leave the session permanently consuming an active-stream slot.
- The discard path now cancels and joins the unpublished upstream first, then clears `activating` only if the session still exists and retains the same cancellation token. It does not mark an expected cancellation as a resolver failure. Erased/cleared sessions are not recreated.
- Added a test-only post-start/pre-publication synchronization hook, unset in production, so the race can be reproduced deterministically without relying on scheduler timing or live YouTube.
- Added regression test `testReleaseDuringStartedButUnpublishedStreamFreesReservation()`. It pauses immediately after worker start, calls `release(id, true)`, verifies a concurrent activation is rejected while the first activation owns the reservation, resumes publication, and checks that the discarded worker is joined, the slot is freed, and a fresh activation can proceed.
- Implementation/test commits: `b854b2f2` (test seam), `06aead23` (reservation release fix), `5c202ecd` (deterministic regression test).
- **Verification status:** source changes are pushed to `feature/youtube-audio-bridge`; GitHub Actions and Raspberry Pi build/CTest are pending. Do not treat this fix as verified until both are checked. No production binary was installed and no service was restarted.

- **CI result:** GitHub Actions run `44107862029` for exact test commit `5c202ecdd2491933f3f32427afcf6260bb318d1e` completed with conclusion `success`; configure, build, and test steps all passed. Raspberry Pi execution of this new test remains pending.

## Lifecycle race: erase() and clear() after upstream start, before publication — 2026-11-17

- Added deterministic regression tests in commit `7914ae676fc6f646d87853807353d6dff8cf5987`:
  - `testEraseDuringStartedButUnpublishedStreamDoesNotRecreateSession()` verifies that `erase()` removes the session and releases its visible reservation while the activation worker is paused after upstream start; after cancellation and worker completion, the erased session is not recreated.
  - `testClearDuringStartedButUnpublishedStreamDoesNotRecreateSession()` verifies equivalent behavior for `clear()`.
- The test seam remains an unset-by-default configuration callback used to pause the activation deterministically. The current header and implementation both use `after_upstream_start_for_tests`.
- GitHub Actions run `44108836398` for the exact commit `7914ae676fc6f646d87853807353d6dff8cf5987` completed successfully: configure/build/test job passed.
- Raspberry Pi verification: the user pulled exact commit `7914ae67`; build succeeded, targeted lazy-session tests passed, and all **11/11 CTest tests passed** in 5.49 seconds.
- Production service remained `active/running`, `NRestarts=0`; production binary SHA-256 remained `21bbe326959062c7d036a5f069bcd6dca7de0a02eccb0ee3a048d9c6eadbf8f8`. No production binary installation, service restart, MPD queue change, or playback intervention occurred.
- These tests prove the targeted registry lifecycle behavior under deterministic scheduling. They do not establish live YouTube/CDN playback, audible hardware output, or the full DNS/SSRF threat model.

## Verification addendum — retainOnly() unpublished activation and slot accounting (2026-11-17)

- Corrected the lifecycle regression test's live-ID set. The test now calls `retainOnly({second_id})`, so the in-flight first session must be retained while the second, inactive session is a valid live queue entry. The prior empty set correctly caused the inactive second session to be pruned; the earlier test expectation of `removed == 0` was therefore invalid.
- Commit: `6e880cb9e5a15014a5e3e290bb019548147d23ba` (`Correct retainOnly lifecycle test live queue set`).
- The test also verifies that the second activation cannot exceed `maximum_active_streams = 1`, then that releasing the first stream frees the slot for the second.
- User-reported Raspberry Pi build succeeded; the targeted executable reported `All lazy stream session tests passed`.
- User-reported full CTest on the Raspberry Pi: **11/11 passed**, including `lazy_stream_session_registry`, HTTP API integration, HTTP byte-stream integration, and `real_mpd_http_proxy_playback`; total 5.17 seconds.
- Raspberry Pi checkout reported clean `git status --short`, with HEAD and origin both at `6e880cb9`.
- GitHub Actions run `44118536311` for the exact commit `6e880cb9e5a15014a5e3e290bb019548147d23ba` completed successfully.
- This closes the specific `retainOnly()` test gate. It does not independently establish live YouTube/CDN retrieval, audible hardware output, or RAM-only/no-persistent-storage behavior. No production binary installation, service restart, MPD queue change, or playback intervention was performed as part of this verification.

