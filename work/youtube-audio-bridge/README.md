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

## Work stages

- [ ] W0 — Inspect current branch, repository layout, existing implementation, and build/test setup.
- [ ] W1 — Repair and complete Android share/preferences/client integration.
- [ ] W2 — Define and implement the Raspberry Pi C++ bridge service and authenticated API.
- [ ] W3 — Implement stable local HTTP streaming relay with bounded RAM buffers and back-pressure.
- [ ] W4 — Implement yt-dlp metadata/URL resolution and playlist expansion without media downloads.
- [ ] W5 — Integrate with the existing MPD daemon and verify CURL input compatibility.
- [ ] W6 — Implement cancellation, process supervision, error handling, and safe cleanup.
- [ ] W7 — Add automated tests for security, queue behavior, RAM bounds, and absence of persistent media writes.
- [ ] W8 — Run Android build/tests, Pi-side tests, and target-device integration/regression tests.
- [ ] W9 — Update installation documentation and final audit; only then consider merge.

## Evidence policy

For each stage, record:
- files added/changed;
- implementation decisions and compatibility assumptions;
- exact commands/tests executed;
- pass/fail and relevant output;
- tests that remain unverified on physical Raspberry Pi hardware.

Repository-side inspection is not a substitute for compiling or running the service on the target device.

## Current known risks

- The Android client previously expected `POST /api/v1/resolve`, while the Pi installer supplied only a FIFO prototype.
- MPD FIFO/pipe plugins are output plugins; do not assume arbitrary FIFO input is supported.
- The agreed architecture uses a local HTTP streaming endpoint and requires MPD CURL input support on the exact target build.
- The existing Android share path was previously incomplete; re-audit current source before editing.
- yt-dlp must be invoked for metadata/URL resolution only, with cache disabled; never invoke media download or file-writing postprocessors.
