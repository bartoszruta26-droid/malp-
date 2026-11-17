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

- Bounded HTTPS upstream producer to RAM (implemented; local MPD-facing HTTP streaming endpoint and session registry pending).
- Control API routing and Bearer token verification.
- yt-dlp resolver and playlist expansion.
- MPD protocol queue manager.
- Stable stream-session registry, source URL refresh and request cancellation.
- systemd service, resource limits and Raspberry Pi installation.
- MPD CURL compatibility and real device playback.

Do not expose this core as a working bridge until these components and integration tests exist. The only permitted media transport remains the local HTTP proxy. No FIFO, named pipes, media files or disk-backed cache are part of this implementation.


## HTTP API bootstrap status

A minimal Boost.Beast HTTP server is now built as `yoinks-mpd`. It exposes an unauthenticated health endpoint and a Bearer-protected queue endpoint that intentionally returns HTTP 503 until the resolver, HTTP media relay and MPD queue manager are integrated. The server enforces a 16 KiB request-body limit, an 8 KiB header limit and socket read/write deadlines. It binds to loopback by default. This is a development bootstrap, not a deployable bridge: request handling is synchronous per connection on a fixed worker pool, there is no TLS termination, and media is not yet enqueued.


The server also validates YouTube URLs on the Raspberry Pi: HTTPS only, exact host allowlist, no userinfo or fragments, and strict video/playlist identifier and path checks. Client-side validation is not trusted as the security boundary.

The server requires a 43–128 character base64url bearer token (minimum 256-bit entropy) and compares credentials over a fixed 128-byte loop. Generate tokens from a cryptographically secure random source; do not use human passwords.

## HTTPS upstream relay component — implementation status

Added `HttpUpstreamStream`, a C++17 producer that uses libcurl to fetch media progressively and writes callback chunks into the fixed-capacity `BoundedStreamBuffer`. The default RAM buffer is 256 KiB and the per-stream byte ceiling is 512 MiB. Back-pressure blocks the libcurl write callback until the consumer frees RAM; cancellation wakes a blocked writer and aborts the transfer.

Security constraints: HTTPS only, exact `*.googlevideo.com` host boundary, standard port 443 only, no URL userinfo, TLS peer/host verification enabled, redirects disabled, connect timeout and low-speed timeout enabled. Redirects are deliberately not followed because they could escape the CDN host policy; an expired or redirected source must be resolved again by the trusted yt-dlp resolver. Upstream URLs are never returned to Android or logged.

The component is not yet connected to a local HTTP response endpoint. It is a bounded upstream producer only; the next implementation step is a stable stream-session registry and chunked local HTTP response consumed by MPD. The end-to-end bridge is not ready.


## HTTP server concurrency and exposure policy

The control server now uses a fixed four-thread worker pool and caps accepted/queued connections at 16; excess connections receive 503. The listener rejects every non-loopback bind address until a separately reviewed TLS termination design exists. This removes the earlier one-connection-at-a-time limitation, but long-lived stream responses will still require reserved capacity or a separate bounded streaming executor.


## Bounded stream-session registry

Added `StreamSessionRegistry` with a default maximum of four sessions (hard cap 32). Each session owns one `HttpUpstreamStream` and therefore one fixed-size RAM buffer; aggregate buffer allocation is bounded by the session cap. Session IDs are 128-bit values obtained from the Linux kernel `getrandom(2)` interface and encoded as 32 lowercase hexadecimal characters. The registry does not expose source URLs and supports lookup, cancellation/removal and bulk shutdown. It is not yet wired to an MPD-facing HTTP route or the queue API; no playable URI is issued at this stage.
