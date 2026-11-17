# YouTube → Yoinks-MPD → yt-dlp → MPD — remaining work

Date: 2026-11-17

## Goal

Finish the existing `feature/youtube-audio-bridge` implementation so MALP can share YouTube video and playlist URLs to a Raspberry Pi that streams audio into the user's existing MPD daemon. Preserve ordinary MALP/MPD control and avoid writing complete audio files to storage.

## Current audited state

- The feature branch contains a YouTube URL parser, bridge preferences, and an Android HTTP client.
- It contains a Bash single-video yt-dlp-to-FIFO producer, an APT installer, Raspberry Pi installation instructions, an offline FIFO producer test, and this integration design.
- The documented FIFO producer test has six cases and the audit records that the user reported running them successfully on the Pi. This report is not an independent reproduction and does not establish MPD FIFO-input compatibility.
- `MainActivity.java` currently contains literal backslash-n sequences in its import list and calls `handleIncomingShare(getIntent())`, but no implementation of that method was found in the file. The Android share path is therefore incomplete and must not be presented as working.
- The Android HTTP client expects `POST /api/v1/resolve`, but the Raspberry Pi installation currently installs only the FIFO producer script. No matching HTTP API service is present.
- Playlist expansion, authenticated request handling, MPD queue insertion, cancellation coordination, and successful end-to-end playback remain unimplemented/unverified.

## Important MPD compatibility gate

MPD's documented `pipe` and `fifo` plugins are output plugins (PCM output), not generic input sources. Do not assume a named FIFO can be added as a local MPD song. First test the exact MPD version/build on the target Raspberry Pi.

If FIFO input is not supported, stop and make an explicit architecture decision:
1. implement and maintain a compatible native C++ MPD input plugin, or
2. use a local streaming HTTP endpoint through MPD's documented CURL input plugin.

Do not silently switch to HTTP while claiming the requested FIFO architecture.

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
