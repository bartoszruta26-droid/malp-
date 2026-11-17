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

- HTTP listener and HTTP client/upstream relay.
- Control API routing and Bearer token verification.
- yt-dlp resolver and playlist expansion.
- MPD protocol queue manager.
- Stable stream-session registry, source URL refresh and request cancellation.
- systemd service, resource limits and Raspberry Pi installation.
- MPD CURL compatibility and real device playback.

Do not expose this core as a working bridge until these components and integration tests exist. The only permitted media transport remains the local HTTP proxy. No FIFO, named pipes, media files or disk-backed cache are part of this implementation.


## HTTP API bootstrap status

A minimal Boost.Beast HTTP server is now built as `yoinks-mpd`. It exposes an unauthenticated health endpoint and a Bearer-protected queue endpoint that intentionally returns HTTP 503 until the resolver, HTTP media relay and MPD queue manager are integrated. The server enforces a 16 KiB request-body limit, an 8 KiB header limit and socket read/write deadlines. It binds to loopback by default. This is a development bootstrap, not a deployable bridge: it is synchronous, single-connection-at-a-time, has no TLS termination and does not yet enqueue media.


The server also validates YouTube URLs on the Raspberry Pi: HTTPS only, exact host allowlist, no userinfo or fragments, and strict video/playlist identifier and path checks. Client-side validation is not trusted as the security boundary.

The server requires a 43–128 character base64url bearer token (minimum 256-bit entropy) and compares credentials over a fixed 128-byte loop. Generate tokens from a cryptographically secure random source; do not use human passwords.
