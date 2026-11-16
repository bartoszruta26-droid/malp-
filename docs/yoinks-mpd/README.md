# Yoinks-MPD: YouTube audio to an existing MPD server

Status: **experimental design and producer prototype; end-to-end playback is not yet verified**.

## Goal

Use Android's standard Share action in MALP to send a YouTube video or playlist URL to a Raspberry Pi. The Pi uses Yoinks as the workflow/integration layer and yt-dlp as the extractor/transfer engine. Audio bytes are streamed as they are produced; the complete media file is never written to disk.

The existing MPD daemon and MALP control connection remain in place. This work must not replace MPD, change existing MPD profiles, or break ordinary MALP queue controls.

## Components and responsibilities

| Component | Responsibility |
|---|---|
| MALP (Android) | Receive ACTION_SEND / ACTION_SEND_MULTIPLE, validate YouTube links, select queue action, send request to Pi |
| Yoinks-MPD | Orchestrate a request, playlist enumeration, process lifecycle, cancellation and status |
| yt-dlp | Resolve metadata and fetch the selected source audio; binary media output goes to stdout |
| Bash / C++ | Glue, process supervision, FIFO lifecycle, MPD protocol integration and tests |
| MPD | Continue to own playback, outputs, volume, status and queue |

The original Yoinks terminal UI is not a streaming daemon. Do not assume its current interactive workflow supports stdout piping or playlist-to-MPD queue insertion. The integration layer in this repository is named Yoinks-MPD and must be implemented explicitly.

## Data path

Intended producer path:

```text
YouTube URL
   -> Yoinks-MPD request handler
   -> yt-dlp (metadata / best compatible source format)
   -> stdout (binary audio bytes only)
   -> named FIFO (bounded kernel pipe buffer)
   -> MPD input path (only if verified for the installed MPD build)
   -> existing MPD outputs
```

A FIFO is not a media file. It carries bytes between processes and has a bounded kernel buffer. The producer blocks when the reader is slower and resumes when the reader consumes data. This provides back-pressure and avoids an application-managed full-file cache.

## Critical MPD compatibility finding

Do not confuse MPD's `pipe` **output** plugin with an input pipe. The documented `pipe` output plugin starts a command and writes decoded PCM to that command's standard input. The `fifo` output plugin similarly writes raw PCM from MPD to a FIFO. Neither is an input mechanism for YouTube bytes.

The documented MPD input plugin list includes local-file and network inputs, but does not document a general-purpose stdin/FIFO input plugin. The Bash producer script therefore demonstrates only the yt-dlp-to-FIFO producer side. Before connecting it to MPD, test whether the exact MPD build can open and decode a FIFO exposed as a local file. If it cannot, stop: do not claim that `mpc add /path/to/fifo` works.

If the installed MPD cannot consume a FIFO, the acceptable next choices are:
1. a small native C++ MPD input plugin (requires building/maintaining a compatible MPD plugin and is a larger integration); or
2. a local HTTP streaming endpoint consumed through MPD's documented CURL input plugin.

The second choice is a fallback only. It is not the requested direct FIFO path and must be discussed before changing the selected architecture.

References:
- MPD plugin reference: https://mpd.readthedocs.io/en/stable/plugins.html
- MPD user manual: https://mpd.readthedocs.io/en/stable/user.html

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

## Bash producer prototype

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
