# Yoinks-MPD C++ service core

This directory is the initial C++17 core for the Raspberry Pi HTTP bridge.

## Current implementation

- CMake build for a dependency-light C++17 core.
- `BoundedStreamBuffer`: fixed-capacity ring buffer allocated in RAM.
- Producer back-pressure: writes block when the consumer has not freed capacity.
- Consumer reads available bytes progressively.
- EOF drain via `close()`; immediate discard and wake-up via `cancel()`.
- Unit tests for zero capacity, wraparound, bounded reads, producer back-pressure, close/drain and cancellation.
- GitHub Actions workflow builds with warnings-as-errors and runs CTest.

The buffer does not open files, create temporary paths, spool media, or grow dynamically after construction. It is a primitive for a future HTTP relay, not the relay itself.

## Build and test

```sh
cmake -S yoinks-mpd -B yoinks-mpd/build -DCMAKE_BUILD_TYPE=Release
cmake --build yoinks-mpd/build --parallel
ctest --test-dir yoinks-mpd/build --output-on-failure
```

## Not implemented yet

- Bounded HTTPS upstream producer to RAM, random stream-session registry and local chunked HTTP endpoint `/stream/{opaque-id}`.
- Control API routing and Bearer token verification.
- yt-dlp resolver and playlist expansion.
- MPD protocol queue manager.
- Resolver-driven source URL refresh remains pending; sessions can currently be created only by trusted internal code.
- systemd service, resource limits and Raspberry Pi installation.
- MPD CURL compatibility and real device playback.

Do not expose this core as a working bridge until these components and integration tests exist. The only permitted media transport remains the local HTTP proxy. No FIFO, named pipes, media files or disk-backed cache are part of this implementation.


## HTTP API bootstrap status

A minimal Boost.Beast HTTP server is built as `yoinks-mpd`. It exposes an unauthenticated health endpoint and a Bearer-protected queue endpoint that intentionally returns HTTP 503 until the resolver and MPD queue manager are integrated. The server enforces a 16 KiB request-body limit, an 8 KiB header limit and socket read/write deadlines. It binds to loopback by default. Control requests use a fixed four-thread pool; media streaming is dispatched to a separate two-thread pool, so long-lived streams do not occupy control workers. This remains a development component, not a deployable bridge: there is no TLS termination, no trusted queue-to-session creation path, and media is not yet enqueued.


The server also validates YouTube URLs on the Raspberry Pi: HTTPS only, exact host allowlist, no userinfo or fragments, and strict video/playlist identifier and path checks. Client-side validation is not trusted as the security boundary.

The server requires a 43–128 character base64url bearer token (minimum 256-bit entropy) and compares credentials over a fixed 128-byte loop. Generate tokens from a cryptographically secure random source; do not use human passwords.

## HTTPS upstream relay component — implementation status

Added `HttpUpstreamStream`, a C++17 producer that uses libcurl to fetch media progressively and writes callback chunks into the fixed-capacity `BoundedStreamBuffer`. The default RAM buffer is 256 KiB and the per-stream byte ceiling is 512 MiB. Back-pressure blocks the libcurl write callback until the consumer frees RAM; cancellation wakes a blocked writer and aborts the transfer.

Security constraints: HTTPS only, exact `*.googlevideo.com` host boundary, standard port 443 only, no URL userinfo, TLS peer/host verification enabled, redirects disabled, connect timeout and low-speed timeout enabled. Redirects are deliberately not followed because they could escape the CDN host policy; an expired or redirected source must be resolved again by the trusted yt-dlp resolver. Upstream URLs are never returned to Android or logged.

The producer is connected to a local chunked HTTP response endpoint through `StreamSessionRegistry`. The endpoint sends progressive 16 KiB chunks with bounded RAM/back-pressure and does not support Range (501) or HEAD (405). It emits the terminating HTTP chunk only after successful upstream completion; failed/incomplete streams are closed without a false successful EOF. Sessions are erased after stream completion or failure. The endpoint is not yet reachable through the normal queue flow: the resolver and trusted session-creation path are not implemented. The end-to-end bridge is not ready.


## HTTP server concurrency and exposure policy

The control server uses a fixed four-thread worker pool and caps accepted/queued connections at 16; excess connections receive 503. A separate two-thread pool handles stream responses, with a hard cap of two active streams. The listener rejects every non-loopback bind address until a separately reviewed TLS termination design exists. The stream executor is separate so a slow media client cannot consume control-plane worker capacity.


## Bounded stream-session registry

Added `StreamSessionRegistry` with a default maximum of four sessions (hard cap 32). Each session owns one `HttpUpstreamStream` and therefore one fixed-size RAM buffer; aggregate buffer allocation is hard-capped at 8 MiB across all sessions, including non-default configurations. Session IDs are 128-bit values obtained from the Linux kernel `getrandom(2)` interface and encoded as 32 lowercase hexadecimal characters. The registry does not expose source URLs and supports lookup, cancellation/removal and bulk shutdown. It is not yet wired to an MPD-facing HTTP route or the queue API; no playable URI is issued at this stage.


## Local HTTP stream endpoint — current limitations

`GET /stream/{32-char-lowercase-hex-id}` looks up an existing trusted in-memory session and streams its upstream bytes using HTTP/1.1 chunked transfer encoding. Unknown IDs return 404; unsupported methods return 405; Range returns 501 with `Accept-Ranges: none`. At most two streams run concurrently in the dedicated stream pool. The current integration tests cover route rejection and unknown sessions, not a successful end-to-end media stream. The queue API still fails closed with 503 because no resolver, queue manager or trusted session-creation path exists. Do not expose the service beyond loopback or treat it as production-ready.


## Deterministic chunked-transfer tests

The HTTP endpoint and its unit-tested `writeChunkedStream` helper share the same chunk-framing implementation. Tests cover progressive output in bounded 16 KiB blocks, successful terminal-chunk emission, omission of the terminal chunk after upstream failure, and immediate stop after a socket-write failure. These tests validate framing and producer/consumer control flow without creating a production session-injection endpoint. They do not replace a live MPD/CURL playback test.
