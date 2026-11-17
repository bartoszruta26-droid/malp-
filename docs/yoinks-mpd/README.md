# Yoinks-MPD: YouTube audio to an existing MPD server

Status: **experimental design and producer prototype; end-to-end playback is not yet verified**. The selected target architecture is a local C++ HTTP relay consumed by MPD's CURL input plugin. FIFO remains a producer-only prototype, not the target transport.

## Goal

Use Android's standard Share action in MALP to send a YouTube video or playlist URL to a Raspberry Pi. The Pi uses Yoinks as the workflow/integration layer and yt-dlp as the extractor/transfer engine. Audio bytes are streamed as they are produced; the complete media file is never written to disk.

The existing MPD daemon and MALP control connection remain in place. This work must not replace MPD, change existing MPD profiles, or break ordinary MALP queue controls.

## Components and responsibilities

| Component | Responsibility |
|---|---|
| MALP (Android) | Receive ACTION_SEND / ACTION_SEND_MULTIPLE, validate YouTube links, select queue action, send request to Pi |
| Yoinks-MPD | Orchestrate a request, playlist enumeration, process lifecycle, cancellation and status |
| yt-dlp | Resolve metadata and fetch the selected source audio; binary media output goes to stdout |
| C++ / Bash | HTTP relay, process supervision, MPD protocol integration, diagnostics and tests |
| MPD | Continue to own playback, outputs, volume, status and queue |

The original Yoinks terminal UI is not a streaming daemon. Do not assume its current interactive workflow supports stdout piping or playlist-to-MPD queue insertion. The integration layer in this repository is named Yoinks-MPD and must be implemented explicitly.

## Target data path

The selected target path is:

```text
MALP Android share
   -> authenticated Yoinks-MPD control API (Raspberry Pi)
   -> yt-dlp metadata / source URL resolution (no media download)
   -> C++ local HTTP relay with bounded RAM buffer and back-pressure
   -> MPD CURL input plugin using a stable local URI
   -> existing MPD outputs / DAC
```

MPD queue entries must contain stable local relay URIs, never expiring YouTube CDN URLs. The relay resolves the upstream source on demand and streams bytes progressively. It must not spool media to a file, cache directory, temporary file, or persistent volume.

The repository's yt-dlp-to-fifo.sh remains a narrowly scoped producer prototype and offline test target. It does not establish a working MPD input path and is not the selected end-state architecture.

## MPD input compatibility gate

MPD's documented pipe and fifo plugins are **outputs**: they send decoded PCM from MPD to a command or FIFO. They are not generic inputs for feeding YouTube audio into MPD. Do not use mpc add /path/to/fifo unless that exact behavior has been demonstrated on the target build.

The selected approach is a local HTTP endpoint consumed by MPD's CURL input plugin. Before implementing the relay, verify on the exact Raspberry Pi installation:

1. mpd --version reports CURL input support (record the complete output).
2. The running MPD instance can open and decode a known-good local HTTP audio URI.
3. Actual request behavior (GET, HEAD, Range, reconnect and seek) is recorded.
4. The exact codec/container is supported by the installed decoder build.
5. The test does not modify or clear the user's existing MPD queue.

Run scripts/yoinks-mpd/diagnose-mpd-http.sh and follow MPD_HTTP_INPUT_DIAGNOSTIC.md. The script is evidence collection only; a successful version check is not a playback test.

If CURL input is absent, stop. Decide explicitly whether to update/rebuild MPD with CURL support or maintain a native C++ MPD input plugin. Do not silently revert to FIFO and do not claim compatibility without a recorded target-device playback test.

## Audio format and quality policy

- Ask yt-dlp for audio-only formats and choose the best available source format compatible with the actual MPD decoder build.
- Preserve the source codec whenever possible.
- Do not convert Opus/AAC/MP3 to FLAC and call the result lossless. Such a conversion cannot restore discarded information.
- Do not claim YouTube provides FLAC unless yt-dlp's format metadata for that specific item actually lists FLAC.
- If a decoder or transport requires conversion, document the conversion and its quality implications. Prefer no transcoding.

## Playlist handling

A playlist is a sequence of independent media streams, not one concatenated audio stream. Never pass a complete playlist to one `yt-dlp -o -` process and enqueue the concatenated stdout as if it were a single MPD track.

Required orchestration:
1. Enumerate playlist entries using yt-dlp metadata mode (no media download).
2. Preserve source order and retain each entry's ID/title/duration.
3. Create one independently managed stream/FIFO per item, or another verified MPD-compatible source per item.
4. Enqueue each item through the MPD protocol in original order.
5. Start the producer for an item only when its consumer is ready; stop and clean it up on skip, stop, cancellation, or MPD disconnect.
6. Bound playlist size and concurrent processes; report unavailable entries without silently reordering the rest.

The current Bash prototype is intentionally single-item only. Playlist orchestration is not yet implemented.

## Security and operational requirements

- Accept only HTTPS YouTube URLs from a strict host allowlist; reject lookalike domains.
- Do not pass untrusted URL text through a shell command string. Use argument arrays in C++ process creation or safely quoted positional arguments in Bash.
- Never expose the MPD password to yt-dlp or the Yoinks-MPD request API.
- Keep the control endpoint bound to loopback/LAN, protected by a separate token if a network API is added.
- Validate FIFO parent directory, ownership and permissions. Refuse symlinks and pre-existing non-FIFO paths.
- Run as a dedicated low-privilege account with only the permissions required to access MPD and the FIFO directory.
- Apply timeouts, process limits, cancellation, SIGPIPE handling and cleanup traps.
- Do not write media to Downloads, cache directories, temporary files, logs, shell variables or command output. Metadata and diagnostic logs may be stored separately; never log authorization tokens or full private URLs.
- Keep stdout exclusively for binary audio. Send diagnostics to stderr.

## FIFO producer prototype (non-target transport)

See [yt-dlp-to-fifo.sh](../../scripts/yoinks-mpd/yt-dlp-to-fifo.sh).

It:
- accepts one YouTube video URL and one FIFO path;
- checks required commands and validates the YouTube host;
- creates a FIFO only if the target does not exist;
- streams yt-dlp stdout into the FIFO;
- does not download or retain a complete media file;
- removes only the FIFO it created when the process exits.

The script blocks until a reader opens the FIFO. That is expected for a FIFO, but it does not prove that MPD can use the FIFO. Use a separate test reader first.

## Verification plan

### Producer-only tests
- `bash -n scripts/yoinks-mpd/yt-dlp-to-fifo.sh`
- Invalid URL is rejected before yt-dlp starts.
- Existing regular file, symlink or FIFO is never overwritten.
- FIFO is removed only if this script created it.
- A reader receives bytes while yt-dlp is running.
- No audio file appears in the working directory, Downloads or temporary directories.
- yt-dlp errors are reported on stderr and return a non-zero exit status.
- SIGINT/SIGTERM closes the producer and cleans up its own FIFO.

### MPD compatibility tests (required before claiming playback)
- Record exact MPD version, build options and enabled input plugins.
- Determine whether MPD can open a FIFO as a local media source.
- Test a short known-good audio stream with the exact source codec.
- Verify MPD state, duration behavior, stop, skip, reconnect and EOF handling.
- Confirm the MPD process does not hang indefinitely waiting for a FIFO writer.
- Repeat on the Raspberry Pi hardware.

### Integration tests (later)
- MALP cold-start and warm-start Share intents.
- Single video and playlist ordering.
- Enqueue versus play-now behavior.
- Current queue remains unchanged if resolution fails.
- Stop/skip cancels the producer and closes the FIFO.
- Concurrent share requests are serialized or rejected cleanly.
- No media files are written.

## Implementation phases

1. **Documentation and producer prototype** — this directory and Bash script.
2. **FIFO/MPD compatibility proof** — run the test on the target Raspberry Pi and record the MPD version and results.
3. **Yoinks-MPD supervisor** — Bash prototype first, then C++ daemon/CLI if lifecycle and playlist control require it. No Python.
4. **MPD queue integration** — use MPD protocol commands; preserve current playback unless play-now was explicitly selected.
5. **MALP share integration** — connect Android share intents to the Pi-side service without changing normal MPD control.
6. **Automated tests and CI** — Bash syntax/integration tests and C++ tests; Android Gradle build remains Java/Gradle as required by the app.
7. **Release gate** — no merge to master until target-device playback, cleanup, playlist and regression tests pass.

## Language/tooling restriction

Implementation glue and Pi-side services in this approach must use **Bash or C++ only**. Do not add Python scripts, Python services, Python build steps or Python runtime dependencies. Existing Android Java code is part of the pre-existing MALP application; this restriction concerns the new Raspberry Pi integration approach.

## Raspberry Pi installation

Follow [INSTALL_RASPBERRY_PI.md](INSTALL_RASPBERRY_PI.md) to install the standalone yt-dlp binary and the Bash producer prerequisites. The installer deliberately leaves MPD configuration and its current queue untouched. This prepares the producer environment only; it does not establish that the installed MPD build accepts FIFO input.


## Offline regression test suite

Run the producer regression suite without contacting YouTube or changing MPD playback:

```bash
bash -n scripts/yoinks-mpd/yt-dlp-to-fifo.sh
bash -n tests/yoinks-mpd/test-yt-dlp-to-fifo.sh
bash tests/yoinks-mpd/test-yt-dlp-to-fifo.sh
```

The test uses a mock `yt-dlp` executable and a separate FIFO reader. It checks binary byte integrity, stderr diagnostics, playlist and lookalike-host rejection, refusal to overwrite files or follow symlinks, non-zero error propagation, and cleanup of the FIFO created by the producer. It does not test MPD input compatibility or real YouTube extraction. The test is intentionally invoked through `bash`; executable file mode is not required.
