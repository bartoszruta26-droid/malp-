# YouTube → Yoinks-MPD → yt-dlp → MPD — remaining work

Date: 2026-11-17

## Goal

Finish the existing `feature/youtube-audio-bridge` implementation so MALP can share YouTube video and playlist URLs to a Raspberry Pi that streams audio into the user's existing MPD daemon. Preserve ordinary MALP/MPD control and avoid writing complete audio files to storage.

## Current audited state

- The feature branch contains a YouTube URL parser, bridge preferences, and an Android HTTP client.
- It previously contained a named-pipe producer prototype and its tests; these have now been removed. The remaining installer provisions yt-dlp only, while the HTTP proxy service is still to be implemented.
- No named-pipe transport or producer test is part of the accepted design. The required media transport is HTTP proxy only.
- `MainActivity.java` currently contains literal backslash-n sequences in its import list and calls `handleIncomingShare(getIntent())`, but no implementation of that method was found in the file. The Android share path is therefore incomplete and must not be presented as working.
- The Android HTTP client expects `POST /api/v1/resolve`, but no matching Raspberry Pi HTTP API/proxy service is implemented yet.
- Playlist expansion, authenticated request handling, MPD queue insertion, cancellation coordination, and successful end-to-end playback remain unimplemented/unverified.

## MPD CURL compatibility gate

The sole target architecture is a local C++ HTTP proxy consumed by MPD's CURL input plugin. Named pipes and local media files are excluded from the design. Before implementing the relay, verify the exact MPD build on the target Raspberry Pi and demonstrate playback of a known-good HTTP audio URI without changing the production queue. Use scripts/yoinks-mpd/diagnose-mpd-http.sh and docs/yoinks-mpd/MPD_HTTP_INPUT_DIAGNOSTIC.md. If CURL input is absent, stop and decide explicitly whether to rebuild MPD with CURL support or implement a native C++ HTTP input module. Do not substitute named pipes.

## Requirements for the implementation

1. Pi-side integration / orchestration must use Bash or C++ only. Do not introduce Python scripts, services, build steps, or runtime dependencies.
2. Android remains the existing Java MALP application.
3. Stream source audio; do not save complete audio files.
4. Use stable local stream URIs in MPD's queue, not expiring YouTube CDN URLs.
5. Expand playlists into independent, ordered MPD queue entries. Never concatenate multiple tracks into one audio stream.
6. Support enqueue and play-now modes without clearing or interrupting the current queue unless explicitly requested.
7. Handle pause/resume, seek where supported, next, stop, disconnect, cancellation, EOF, and producer cleanup.
8. Apply strict HTTPS YouTube host validation, process/time/playlist/concurrency limits, and a separate bridge token. Never expose MPD credentials in YouTube arguments or API requests.
9. Keep the chosen source codec whenever supported. Do not label lossy-to-FLAC conversion lossless.
10. Repair MainActivity imports and implement ACTION_SEND/ACTION_SEND_MULTIPLE share handling, background network execution, safe result handling, and UI error reporting.
11. Add configuration for bridge URL/token and document local network/TLS deployment.
12. Update the audit with actual test evidence. Never mark hardware/build tests as passed unless they have actually run.

## Acceptance gates

- Android Gradle unit tests and debug assembly succeed.
- Bash syntax and offline integration tests succeed.
- Exact MPD build input compatibility is recorded.
- A real single YouTube item plays on Raspberry Pi.
- A playlist plays in original order as independent tracks.
- MALP continues to control existing local music, volume, outputs, and queues.
- Stop/skip/disconnect release producer processes and resources.
- No complete audio files are created.
- Final documentation differentiates repository-side tests, user-reported Pi tests, and independently verified target-device tests.

## GitHub repository configuration note

GitHub Issues are disabled for this repository (create-issue API returned HTTP 410), so this file records the work plan directly on the existing feature branch instead of creating an issue.


## Additional upstream comparison (2026-11-17)

### pbnoxious/yt2mpd

Inspected `README.md`, `pyproject.toml`, `yt2mpd/config.py`, `yt2mpd/yt2mpd.py`, and its sample configuration.

- GPL-3.0-or-later.
- Uses yt-dlp to extract audio and download/convert it into a temporary subdirectory inside MPD's music directory.
- Runs `mpc update`, then adds resulting local files with `mpc add`.
- Supports video URLs and playlist URLs; playlist items become separate downloaded files.
- Optional cleanup/pruning removes temporary files after they leave the queue.
- Not a streaming solution: it stores full converted audio files before adding them to MPD.
- Uses Python, explicitly conflicting with this project's Pi-side Bash/C++ constraint.
- Has unsafe shell construction in `os.system('mpc add "{}"'.format(filename))` and a shell=True query path; must not be copied as-is.
- The metadata parser depends on yt-dlp human-readable stdout and is brittle.
- Useful only as a behavioral reference for playlist expansion, queue insertion, MPD database update, and cleanup. Do not adopt its download-to-library implementation.

### Nuffic/youtube-mpd

Inspected `README.md`, `package.json`, `Dockerfile`, `docker-compose.yml`, `src/index.ts`, `src/player/MpdPlayer.ts`, `src/player/MpdStatus.ts`, and `src/eventListener.ts`.

- TypeScript/Node.js implementation based on `yt-cast-receiver` and `mpd-api`.
- Receives YouTube Cast / YouTube Music Cast sessions (the README explicitly says it is not a generic receiver for arbitrary apps).
- Extracts a video audio URL using `yt-dlp --get-url`, then adds that URL directly to MPD's queue. This is the closest of the two projects to direct, no-full-file streaming.
- The MPD instance is configured as a single renderer; its implementation clears the queue and enables single/consume behavior. This is unsuitable as-is for MALP's shared general-purpose MPD queue and could disrupt ordinary playback.
- It constructs shell commands with `child_process.exec` and interpolates the video ID. Replace with `spawn`/argument arrays and strict video-ID validation if reusing the approach.
- The direct CDN URL is time-limited and stored in MPD's queue. There is no robust local URL-refresh proxy in the inspected code; queued items can fail after URL expiry.
- Docker example uses host networking and bundles MPD plus receiver, while this project must integrate with an already configured Raspberry Pi MPD and its audio output.
- Repository metadata/package references point to `dgalli1/ytm-mpd`; confirm upstream lineage and license obligations before copying implementation code. The inspected package declares ISC; verify the exact repository license file before code reuse.

### Architecture decision after comparison

Use a hybrid design, not a direct copy of either project:

1. Keep the existing MALP Android share/bridge flow as the user interface.
2. On Raspberry Pi, resolve YouTube IDs and metadata with yt-dlp using machine-readable output (`--dump-single-json` / `--flat-playlist` where appropriate), never parse human-readable progress output.
3. Expand playlists into separate ordered MPD entries.
4. Use a local HTTP streaming proxy on Raspberry Pi as the stable MPD URI. The proxy resolves/refreshes expiring upstream CDN URLs and streams bytes with back-pressure; it must not persist complete audio files.
5. Configure and verify MPD's CURL input support for the local HTTP URI. This verifies the only supported bridge media path: local HTTP proxy to MPD. Keep the local endpoint restricted to loopback/LAN and protect control endpoints with a token.
6. Preserve normal MPD queue semantics. Do not clear the queue, enable global consume/single mode, or take over playback unless the user explicitly selected play-now.
7. For the required Bash/C++ Pi-side constraint, implement the proxy/service and process supervision in C++ (or a carefully constrained Bash launcher); yt-dlp remains an external executable dependency. Do not introduce a Python daemon or Node.js runtime.
8. Treat Nuffic's direct URL approach as a prototype/reference only; the local stable-URI proxy is required for long queues and URL expiry.
9. Treat pbnoxious's playlist and cleanup workflow as behavioral reference only; do not download whole audio files.

### Revised test gates

- Confirm MPD was built with CURL input enabled (for example, inspect MPD build features and test an HTTP URI on the exact target build).
- Test one YouTube item through local proxy into MPD without creating a complete audio file.
- Test a playlist with ordered, independent queue entries and verify that ordinary local tracks remain intact.
- Simulate CDN URL expiry and verify transparent re-resolution while the local MPD URI remains unchanged.
- Test pause/resume/seek/next/stop and queue behavior against the existing MPD instance.
- Test service authentication, loopback/LAN binding, process limits, cancellation, and cleanup.


## Hard requirement: RAM-only, real-time playback (confirmed 2026-11-17)

The user explicitly requires music to be played in-flight. No complete audio file may be written to persistent storage. RAM buffering is permitted; disk, SSD, SD-card, USB-storage, and persistent-cache buffering is forbidden.

This is a hard acceptance requirement, not an optimization.

### Required runtime behavior

- yt-dlp is used only for metadata / stream URL resolution (e.g. machine-readable JSON and URL extraction). Do not use its download-to-file mode.
- Stream audio bytes from YouTube through a bounded in-memory buffer to the local HTTP endpoint consumed by MPD.
- Never create temporary audio files, converted audio files, segment files, playlist downloads, or persistent cache entries.
- Disable yt-dlp cache (for example, pass `--no-cache-dir` where applicable); do not set a persistent cache directory.
- Do not use `--extract-audio`, `--output`, `-o`, or any postprocessor that writes media files.
- Avoid shell pipelines that can spill to disk. Use C++ process APIs with argv arrays / pipes, not shell interpolation.
- Keep only bounded buffers in RAM; apply back-pressure so slow MPD consumption cannot cause unbounded memory growth.
- Playlist expansion resolves each item independently and queues separate local stream URIs; it does not download playlist media.
- On stop, next, client disconnect, failure, or service shutdown, cancel the producer and release RAM buffers, sockets, and child processes.
- Do not persist upstream signed CDN URLs or authentication tokens in MPD's database, logs, or config. MPD queue entries should contain only local stable proxy URIs.
- Do not log full signed media URLs or request credentials. Logs may contain sanitized video IDs, status, and error classes.
- Review Linux swap/zram policy on supported Raspberry Pi images. zram is RAM-backed and acceptable; disk-backed swap can write pages to persistent storage and must be disabled or otherwise proven not to persist media buffers if the user's no-disk requirement is to be guaranteed.

### RAM and storage verification tests

1. Run the bridge with a dedicated temporary directory and assert that no audio/media files are created anywhere on persistent mount points.
2. Trace file opens/writes by yt-dlp, the bridge, and MPD during playback; reject writes of media payloads to persistent storage.
3. Verify yt-dlp cache is disabled and no postprocessor/download path is invoked.
4. Monitor resident memory during a long track and playlist; prove buffer usage remains under a configured bound.
5. Simulate a slow MPD reader and verify producer back-pressure rather than buffer growth or disk spill.
6. Stop playback and verify producer processes, sockets, and buffers are released.
7. Test a long playlist and ensure only metadata / small queue state is retained, not audio payloads.
8. Record swap configuration in the Raspberry Pi installation audit; distinguish RAM-backed zram from disk-backed swap.

### Architecture clarification

The local HTTP proxy is a streaming relay, not a download manager. It must stream bytes progressively from the upstream YouTube CDN to MPD with a bounded RAM buffer. A design that first downloads a complete item and then serves it over HTTP is explicitly rejected.


## Post-audit continuation — 2026-11-17 (repository review)

### Verified repository state
- Feature head reviewed: `be8f2176fc696c628567674a7b957e7204b001cb`.
- Latest Yoinks-MPD workflow run `44066634026`: success (configure, build, CTest, installer shell syntax and systemd unit validation).
- Latest Android workflow run `44064310295`: success (Gradle debug assemble and APK artifact upload).
- These are repository CI results, not Raspberry Pi playback or Android emulator evidence.
- The feature branch and `master` have diverged (feature ahead by 333 commits, master ahead by 7). Do not merge the entire branch without a dedicated reconciliation/rebase plan. The master tree contains no feature-only paths, while the feature branch carries the Android bridge, Yoinks-MPD service, CI and audit documents.

### Current technical blockers confirmed by source inspection
1. `GET /api/v1/health` currently returns the literal `"control_ready":false`, even though the queue service is constructed and wired in `main.cpp`. Health must report dependency-aware readiness, not a hard-coded value. It should separately identify process liveness, control-plane readiness and media/MPD compatibility; a healthy HTTP process does not prove MPD playback.
2. The Android manifest still sets `android:usesCleartextTraffic="true"`. The new bridge client requires HTTPS, but global cleartext remains enabled for the legacy application. Review actual legacy HTTP use and replace the global allowance with a narrowly scoped Network Security Config exception only if required; retain HTTPS-only bridge validation and add a regression test that bearer credentials are never sent over cleartext.
3. The installer documentation `docs/yoinks-mpd/INSTALL_RASPBERRY_PI.md` is stale: it says the installer provisions only yt-dlp and does not create the proxy/systemd service, while the current `scripts/yoinks-mpd/install.sh` builds, installs and enables the C++ service. Bring docs into alignment before handing the installer to an operator.
4. The current Yoinks-MPD CI workflow compiles/tests C++ and checks installer shell syntax and a transformed systemd unit, but does not execute the installer in a disposable Debian/Raspberry Pi-like root environment. Add safe shell-level tests for idempotence, token preservation, --no-start, --restart gating and failure cleanup without touching a host MPD or systemd service.
5. Android build CI creates a debug APK artifact for 14 days; emulator/instrumentation tests and a release-signed artifact are not yet evidenced.

### Next implementation sequence
- W1: add explicit readiness model and deterministic tests for health states; health must not return `control_ready=true` until configured dependencies are constructed, and must not imply live MPD/CURL playback.
- W2: test and narrow Android cleartext network policy without breaking existing MPD profile connections.
- W3: reconcile Raspberry Pi install guide and the root bridge audit with the current installer behavior.
- W4: add isolated installer tests and expand CI.
- W5: add HTTP contract success-path tests with mock yt-dlp and mock MPD, then run MPD/CURL compatibility tests on the target Pi in a safe isolated instance.
- W6: Android share-sheet instrumentation/emulator tests, including ACTION_SEND, malformed and multiple links, saved URL/token, HTTPS rejection, redirect rejection, enqueue/play_now and activity recreation.
- Keep the hard requirement: audio payload only in bounded RAM buffers; no media file or persistent audio cache.
- Keep production MPD queue and audio configuration untouched until the user explicitly runs isolated target-device tests.
