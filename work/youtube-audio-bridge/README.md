# Workspace: YouTube Audio Bridge for MALP

This directory is the working area for implementation, audits, test evidence, and stage tracking for the YouTube-to-MPD integration.

## Repository and branch

- Repository: `bartoszruta26-droid/malp-`
- Working branch: `feature/youtube-audio-bridge`
- Do not merge into the default branch until the Android, service, MPD compatibility, RAM-only, and regression gates pass.

## Non-negotiable requirements

1. Audio is streamed progressively in real time; complete media files are never downloaded or written.
2. Audio payload buffering is bounded and RAM-only. No SD/SSD/HDD/USB/persistent cache or disk-backed temporary media.
3. Raspberry Pi implementation uses Bash/C++ only; do not introduce Python or Node.js runtime services.
4. Android implementation remains Java and integrates with the existing MALP application.
5. MPD queue entries use stable local proxy URIs, never expiring signed YouTube CDN URLs.
6. Playlist items remain separate queue entries in original order.
7. Preserve normal MPD queues and controls. Do not clear queues or change global single/consume modes.
8. Never claim a build, test, or hardware validation passed unless evidence is recorded.

## Main audit\n\n- [Professional architecture audit and implementation plan](AUDYT_ARCHITEKTURY_I_PLAN_REALIZACJI.md) — current-state findings, target architecture, security, RAM-only controls, acceptance tests, risks, and staged delivery plan.\n\n## Work stages

- [x] W0 — Confirm HTTP proxy as the sole transport; remove named-pipe producer and tests.
- [ ] W1 — Verify CURL input and real HTTP playback on the exact Raspberry Pi MPD build (hardware evidence pending).
- [ ] W2 — Define the authenticated API contract and security limits.
- [ ] W3 — Build the Raspberry Pi C++ service skeleton.
- [ ] W4 — Implement yt-dlp metadata resolution and playlist expansion without media downloads.
- [ ] W5 — Implement the stable local HTTP relay with bounded RAM buffers and back-pressure.
- [ ] W6 — Implement MPD queue management, cancellation, process supervision and cleanup.
- [ ] W7 — Complete Android Java share integration and secure token storage.
- [ ] W8 — Run automated tests, Raspberry Pi hardware tests and RAM-only verification.
- [ ] W9 — Complete release audit; do not merge until every acceptance gate passes.

## Evidence policy

For each stage, record:
- files added/changed;
- implementation decisions and compatibility assumptions;
- exact commands/tests executed;
- pass/fail and relevant output;
- tests that remain unverified on physical Raspberry Pi hardware.

Repository-side inspection is not a substitute for compiling or running the service on the target device.

## Current known risks

- The Android client expects `POST /api/v1/resolve`, but the Pi installer previously supplied only an unrelated producer prototype. The producer has now been removed; the HTTP service remains to be implemented.
- The bridge media transport is HTTP proxy only; do not add pipe, named-pipe, local-file or file-cache paths.
- The agreed architecture uses a local HTTP streaming endpoint and requires MPD CURL input support on the exact target build.
- The existing Android share path was previously incomplete; re-audit current source before editing.
- yt-dlp must be invoked for metadata/URL resolution only, with cache disabled; never invoke media download or file-writing postprocessors.


## Latest work (2026-11-17)

- W0 correction completed: HTTP proxy + MPD CURL input is the sole target; the legacy named-pipe producer and tests were removed.
- Added a read-only Raspberry Pi diagnostic: `scripts/yoinks-mpd/diagnose-mpd-http.sh`.
- Added the procedure: `docs/yoinks-mpd/MPD_HTTP_INPUT_DIAGNOSTIC.md`.
- W1 remains **blocked pending target-device evidence**. The diagnostic script has not been run on the Raspberry Pi in this environment, and no successful HTTP playback is claimed.
