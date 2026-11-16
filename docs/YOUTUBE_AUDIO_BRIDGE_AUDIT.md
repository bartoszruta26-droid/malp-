# YouTube Audio Bridge – MALP integration specification

Status: implementation baseline / design record  
Base branch: master  
Feature branch: feature/youtube-audio-bridge

## User requirements
- Raspberry Pi already runs MPD; MALP already controls that MPD instance.
- Android Share from YouTube must pass video or playlist URLs into MALP.
- MALP must submit these URLs to a service running on the Raspberry Pi.
- Playback must be streamed; do not download or persist audio files.
- YouTube playlists must be expanded into queue entries.
- Prefer the best source audio format exposed by YouTube/yt-dlp. Do not transcode lossy audio to FLAC and describe it as lossless. Only select a true FLAC source if the provider actually exposes one; otherwise use the best original audio stream supported by MPD.
- Preserve existing MPD profiles, connection lifecycle, queue controls, notifications and playback behavior.

## Repository audit (2026-11-16)
- Repository: https://github.com/bartoszruta26-droid/malp-
- Default branch: master
- Baseline commit: fd72e3e41ef2f60af24d53beb0f0b6db7a34d4e2
- App version: 1.3.2, versionCode 42
- Android implementation: Java, Gradle 8.7 / AGP 8.5.0, minSdk 21, targetSdk 34.
- AndroidManifest declares ACTION_SEND with text/plain for MainActivity.
- MainActivity currently handles ACTION_SEND in onCreate only when extras are present and savedInstanceState is null. It reads EXTRA_TEXT, treats any parseable URL as a media URL, registers ServerConnectionListener and calls MPDQueryHandler.playSongNext(mediaUrl) after MPD connection. This generic behavior must be narrowed: YouTube URLs must go to the bridge, while existing generic URL sharing behavior should remain intentional and tested.
- MainActivity has no inspected onNewIntent handler. Must support both cold and warm activity starts, avoid duplicate submissions on recreation, extract ClipData/text safely, parse text containing URL/title, validate HTTPS host allowlist and URI syntax.
- MPDQueryHandler.playSongNext(String) and MPDInterface.addSong(String) already support enqueueing external URI; keep MPD protocol layer unchanged.
- Existing per-profile streamingURL is for MPD audio output, not the YouTube bridge. Do not overload this field.
- MPDServerProfile currently stores profile fields in parcel and SQLite. Avoid adding bridge configuration into the profile until schema migration and profile editor are fully implemented. Prefer dedicated app preferences initially: bridge base URL and bearer token, with host/IP and port configurable in settings. A later profile-scoped design can be implemented as a separately reviewed migration.
- MainActivity currently registers MPD connection listener during pre-super onCreate handling; refactor carefully to process intent after UI/activity setup and coordinate connection state. Use current active MPD connection; if disconnected, preserve pending share and submit only after reconnection.
- Database profile schema currently version 5. Do not change it for the first integration phase.

## Intended architecture
Android MALP -> HTTPS/HTTP local REST bridge -> yt-dlp metadata/format resolver + streaming proxy -> MPD queue.
The Raspberry Pi bridge must not save audio or cache complete media. Use bounded in-memory metadata/session state only, and stream bytes on demand to MPD via a stable local HTTP URL. Do not give MPD a short-lived YouTube CDN URL.

## Proposed REST API (versioned)
- POST /api/v1/resolve
  Request: { "url": "https://www.youtube.com/watch?v=...", "mode": "enqueue"|"play_now" }
  Response: { "items": [{ "id": "...", "title": "...", "artist": "...", "duration": 123, "stream_url": "http://bridge-host:PORT/api/v1/stream/<opaque-id>" }] }
- POST /api/v1/resolve-playlist
  Request: { "url": "https://www.youtube.com/playlist?list=...", "mode": "enqueue"|"play_now" }
  Response: playlist metadata and ordered items, each having stable local stream_url.
- GET /api/v1/stream/{opaque-id}
  Proxies selected upstream audio format on demand; no full-file download, no disk persistence.
- GET /api/v1/health
  Returns bridge readiness and yt-dlp version.
- POST /api/v1/session/{id}/refresh
  Optional refresh of expiring upstream URL before stream starts.

The endpoint names can be consolidated to a single resolve endpoint if implementation simplicity warrants it. Keep API versioned and typed.

## Stream handling requirements
- Select best source audio format using yt-dlp format metadata, prefer codecs/bitrates based on real source formats. Preserve original codec whenever MPD supports it. FLAC is only a preference when source actually exists as FLAC; no lossy-to-FLAC transcode.
- The local stream endpoint must return correct Content-Type, status codes, support Range if feasible and forward required upstream headers/cookies only where legitimate.
- Stream source URL should be resolved shortly before playback, not playlist enqueue, to avoid expiring links.
- For playlists, enumerate metadata and enqueue local stable URLs; resolve each individual upstream source lazily when MPD requests it.
- Must avoid writing media to disk, including temporary files. Disable yt-dlp download mode; use metadata extraction and stream proxy / stdout piping.
- Apply concurrency limits, request timeouts, maximum playlist size, bounded memory, no shell interpolation, subprocess argument arrays, URL/host validation, loopback/LAN-only default bind, bearer token authentication, rate limits and SSRF protection.
- Explicitly distinguish login-required/private/region-restricted/deleted videos and unavailable entries in playlist.
- Expose clear error codes to MALP.

## MALP behavior
- ACTION_SEND text/plain from YouTube: identify YouTube URL; present lightweight choice "Add to queue" / "Play now" (default add to queue), or support user-configured default.
- Playlist share: present count/title and enqueue all valid items in original order.
- Queue behavior: enqueue uses MPDQueryHandler.playSongNext? No: that existing method means play next after current item, not necessarily append. Confirm exact behavior. Use an MPD command path that genuinely appends playlist items to tail in order (existing MPDInterface.addSong / MPDQueryHandler action ACTION_ADD_PATH). Play now should clear queue only after successful resolution, add URLs in order, then play index 0; design as a single serialized operation.
- Preserve queue and current playback on failed resolve: do not clear queue or replace current song until the bridge returns a complete validated item list.
- Add UI feedback for resolving, queued count, unavailable entries, bridge offline, authentication failure and stream errors.
- Use active profile MPD hostname and configured bridge host when default bridge host is unset; allow explicit bridge URL for different host topology.
- Never put MPD password into bridge requests. Bridge auth token is separate and stored in Android private preferences.
- Support ACTION_SEND and ACTION_SEND_MULTIPLE if practical; handle ClipData and onNewIntent.
- Avoid duplicate processing across activity recreation; persist pending request only as needed, clear once accepted or cancelled.

## Test plan
Android unit/instrumented:
1. URL parsing from plain URL, title + URL, trailing punctuation, youtu.be, youtube.com/watch, youtube.com/playlist, music.youtube.com, shorts.
2. Reject lookalike hosts (youtube.com.evil.test), non-HTTPS YouTube, file://, localhost and arbitrary private URLs.
3. Cold start ACTION_SEND and warm onNewIntent.
4. No active MPD profile / disconnected MPD / bridge timeout / 401 / 413 / 429 / 5xx / malformed response.
5. Verify enqueue does not clear current queue and preserves order.
6. Play-now clears only after successful resolution.
7. Playlist partial failures preserve available entries and report skipped count.

Bridge tests:
1. Single-video metadata resolution and stream.
2. Playlist enumeration; preserve order and bounded max entries.
3. Validate no files are written to disk.
4. Validate stable local stream URL still works after resolver URL refresh.
5. HTTP Range and correct MIME where supported.
6. Upstream 403/410/429, deleted/private/unavailable, region/age/login restricted.
7. Concurrent playlist streams, timeout, cancellation and resource limits.
8. SSRF and authentication tests.
9. Verify no lossy-to-FLAC conversion is claimed or performed.

## Work sequence
1. Audit and record baseline. DONE.
2. Create feature branch. DONE.
3. Implement bridge service and deployment artifacts with tests.
4. Add MALP preferences for bridge address/token.
5. Add robust share-intent routing and queue operations.
6. Add tests and CI build; run Android emulator tests if runner permits.
7. Review diff, update CHANGELOG and merge only after green tests.

## Current limitations
The GitHub connector can inspect and modify repository files but does not provide an Android SDK/emulator runtime. Build and emulator verification must be done through repository CI or a local Android build environment. Do not claim APK compilation or playback testing unless actual workflow results are observed.
