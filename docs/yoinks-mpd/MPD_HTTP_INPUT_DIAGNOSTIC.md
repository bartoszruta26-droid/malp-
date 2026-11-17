# MPD HTTP/CURL input diagnostic — Raspberry Pi

## Purpose and safety

This procedure closes the MPD compatibility gate (W1). The selected architecture uses a local C++ HTTP relay and MPD CURL input. The existing FIFO producer is not the target transport.

The evidence collector is read-only: it does not connect to the running MPD daemon, modify its queue, start playback, change configuration, or write audio. Run it without root if the current account can inspect the installation.

## 1. Collect exact build evidence

From the repository root on the Raspberry Pi:

```bash
bash -n scripts/yoinks-mpd/diagnose-mpd-http.sh
bash scripts/yoinks-mpd/diagnose-mpd-http.sh 2>&1 | tee /tmp/yoinks-mpd-http-diagnostic.txt
```

The report records kernel/architecture, MPD and MPC versions, package versions, plugin-related configuration directives, CURL keyword hints, swap devices, zram and mount points. Inspect the report before sharing; do not include credentials or tokens.

**Important:** MPD version output is a build-capability clue only. The presence of the word CURL does not prove that the running daemon can play the intended HTTP stream, and absence of that word is not conclusive for every distribution build.

## 2. Prove actual HTTP playback without touching the production queue

Use a separate, temporary MPD instance with its own configuration, state directory, playlist directory and Unix socket. Do not use the production MPD port/socket or its music/state directories. The isolated instance should have:

- a dedicated test port and Unix socket;
- a RAM-backed runtime directory (for example /run/user/$UID or /dev/shm);
- a null output or a specifically selected test audio output;
- no persistent media directory;
- a known-good short HTTP audio test URI whose source and codec are understood.

Start the isolated daemon with its dedicated config and verify it is listening on the isolated socket/port. Point mpc to that socket/port using its documented environment variables or command-line options. Add the known-good HTTP URI to this isolated instance, start playback, and record mpc status, daemon logs and observed HTTP request behavior. Stop the isolated instance and remove only its temporary configuration/runtime files. Never use queue commands against the production daemon for this test.

If a fully isolated instance cannot be configured safely, stop and provide the diagnostic report; do not experiment on the production queue.

## 3. Record the following evidence

- Exact Raspberry Pi model / OS release and architecture.
- Complete mpd --version output and package version.
- Whether the isolated MPD instance accepts the HTTP URI.
- Whether playback starts and reaches EOF for the test item.
- HTTP methods observed (GET/HEAD), status codes and whether Range requests occur.
- Codec/container and decoder selected by MPD.
- Seek, pause, resume, stop and reconnect behavior.
- Swap devices and whether any disk-backed swap is enabled.

Do not mark W1 complete from version output alone. W1 passes only after actual HTTP playback is demonstrated on the target Pi and the exact evidence is added to the audit. Disk-backed swap must be separately disabled or proven unable to receive bridge memory before any RAM-only claim.

## 4. Return the evidence

Share the sanitized diagnostic report plus isolated playback observations. Remove local addresses, usernames, credentials, tokens and private URLs first. Implementation of the relay remains blocked until the result is reviewed.
