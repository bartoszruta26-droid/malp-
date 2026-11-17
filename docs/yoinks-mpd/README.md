# Yoinks-MPD: YouTube audio through an HTTP proxy into MPD

Status: **HTTP proxy architecture selected; proxy service and end-to-end playback are not yet implemented or verified.**

## Goal

MALP shares a YouTube video or playlist URL with a Raspberry Pi service. The service resolves metadata and a short-lived upstream media URL using yt-dlp, then exposes a stable local HTTP stream URI to the existing MPD daemon. Audio bytes are relayed progressively; the complete media item is never downloaded or written to persistent storage.

The only media transport between the bridge and MPD is HTTP. There is no FIFO, named pipe, local media file, or file-backed cache in the design.

## Components

| Component | Responsibility |
|---|---|
| MALP (Android/Java) | Receive ACTION_SEND and ACTION_SEND_MULTIPLE, validate links, select enqueue/play-now, submit authenticated requests |
| Yoinks-MPD control API | Validate requests, expand playlists, manage stream sessions and return stable local stream identifiers |
| yt-dlp | Resolve metadata and source URL only; never download media payload to a file |
| C++ HTTP proxy | Fetch upstream media progressively, relay bytes with bounded RAM buffering and back-pressure |
| MPD | Consume stable local HTTP URIs through its CURL input support; retain existing audio outputs |

## Data path

```text
MALP Android
  -> authenticated control request
  -> Raspberry Pi Yoinks-MPD service
  -> yt-dlp metadata / source URL resolution (no media download)
  -> C++ HTTP proxy <-> YouTube CDN
  -> stable local HTTP URI in MPD queue
  -> MPD CURL input -> existing output / DAC
```

MPD queue entries must never contain expiring signed CDN URLs. Each bridge item receives an opaque, unguessable local identifier and a stable URI such as `http://127.0.0.1:8787/stream/<opaque-id>`. The URI resolves to a managed session whose upstream URL can be refreshed in memory.

## Non-negotiable storage and memory rules

- Audio payload exists only in bounded RAM buffers while streaming.
- No media file, temporary media file, spool, cache, thumbnail, or segment file may be written to SD, SSD, HDD, USB, or another persistent mount.
- yt-dlp is invoked for metadata/source resolution only; do not use download, output-template, postprocessor, or cache paths for media.
- Temporary control/configuration files, if ever required, must be placed on a verified RAM-backed filesystem and must contain no audio payload or secrets.
- Disk-backed swap must be disabled or otherwise proven unable to receive bridge media buffers. zram is RAM-backed; its use must still be measured and bounded.
- Enforce a global memory budget, per-stream buffer limit, maximum concurrent streams, and back-pressure. When the consumer is slower, stop reading upstream until buffer space is available.

## HTTP proxy requirements

The proxy must be a real streaming relay, not a redirector to YouTube's CDN.

- Accept only locally generated opaque stream IDs; never accept arbitrary upstream URLs from clients.
- Resolve upstream URLs server-side and validate HTTPS hostnames against an explicit allowlist or yt-dlp's trusted extractor result.
- Support the request methods and range semantics actually required by the installed MPD CURL input implementation. Verify GET, HEAD, Range, reconnect and seek behavior on the target build before finalizing semantics.
- Stream response headers and body progressively; do not wait for the full item.
- Implement bounded buffers, back-pressure, cancellation, timeouts, upstream reconnect and source URL refresh.
- Correctly propagate content type, content length when known, status codes, EOF and upstream errors.
- Do not claim byte-range seeking works until it is tested end-to-end. If source seeking requires a new upstream range request, map ranges safely and reject unsupported ranges clearly.
- Never log signed CDN URLs, authorization tokens, or raw secrets.

## MPD integration

MPD must consume the proxy URI using its CURL input support. Verify the exact Raspberry Pi MPD build and actual HTTP playback on an isolated MPD instance before implementing assumptions about request headers or range behavior. Do not modify the production queue during compatibility testing.

Queue integration must use the MPD protocol, not shell commands. Preserve normal queue behavior, order, existing items and MALP controls. Enqueue and play-now are distinct actions; play-now occurs only on explicit user selection. Do not globally change single/consume modes or clear the queue.

## Security

- Bind the media listener to loopback where MPD shares the network namespace; otherwise use a narrowly scoped private interface and firewall.
- Protect the control API with a high-entropy token; use HTTPS when crossing an untrusted network.
- Store Android credentials using Android Keystore-backed storage.
- Keep media and control endpoints separate; the media endpoint accepts only opaque stream IDs and minimal methods.
- Run the service as a dedicated unprivileged account.
- Use process APIs with argv arrays, never shell interpolation.
- Apply request, playlist, duration, bitrate, stream-count, CPU and RAM limits.
- Protect against SSRF, DNS rebinding, URL parser confusion, path traversal and token leakage.

## yt-dlp invocation policy

yt-dlp is a resolver, not the streaming transport. Invoke it with a fixed executable path and argument vector, with no shell. Request metadata and the selected format's source URL only. Disable its cache where supported. Do not invoke media download, merge, remux, postprocessing, output-template or file-writing operations. Treat source URLs as secrets and keep them in process memory only.

## Implementation stages

1. Verify MPD CURL input and real HTTP playback on the exact Raspberry Pi build.
2. Finalize control API request/response contract and threat model.
3. Implement C++ service skeleton, configuration, token authentication and process supervision.
4. Implement yt-dlp metadata/source resolver with playlist expansion.
5. Implement bounded-memory C++ HTTP relay, refresh, cancellation and HTTP semantics.
6. Implement MPD queue manager using MPD protocol and stable local URIs.
7. Complete Android Java sharing, secure credentials and enqueue/play-now UX.
8. Add unit, integration, security, resource-limit and regression tests.
9. Validate on Raspberry Pi: sustained playback, seek/reconnect, queue preservation, RAM ceiling, no persistent media writes and no disk-backed swap exposure.

## Current files

- `scripts/yoinks-mpd/install.sh` installs the standalone yt-dlp resolver dependency only. It does not install the HTTP proxy service.
- `scripts/yoinks-mpd/diagnose-mpd-http.sh` collects read-only MPD/build/system evidence.
- `docs/yoinks-mpd/MPD_HTTP_INPUT_DIAGNOSTIC.md` describes the isolated MPD HTTP playback gate.
- `work/youtube-audio-bridge/AUDYT_ARCHITEKTURY_I_PLAN_REALIZACJI.md` contains the full audit and acceptance criteria.

## Verification status

No end-to-end YouTube-to-MPD HTTP playback is claimed. The Raspberry Pi CURL compatibility gate remains open until actual playback is demonstrated on the target hardware. The HTTP proxy implementation is the next engineering deliverable.
