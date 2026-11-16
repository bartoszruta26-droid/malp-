# YouTube audio / Yoinks-MPD integration audit

Status: **design documentation and single-video Bash FIFO producer prototype committed; MPD playback path not yet proven**.

Branch: `feature/youtube-audio-bridge`  
Repository: https://github.com/bartoszruta26-droid/malp-

## User requirements

- Raspberry Pi already runs MPD and MALP already controls it.
- Android YouTube Share should pass video or playlist URLs to the Pi.
- Use yt-dlp, Yoinks workflow/integration module and MPD.
- Stream bytes as they are fetched; do not save complete audio files or media temporary files.
- The selected direction is direct stdout-to-FIFO producer, subject to MPD input compatibility proof.
- New Raspberry Pi-side implementation and glue must use Bash or C++ only. Do not use Python.
- Preserve existing MPD and MALP functionality.
- Prefer the best available source audio format supported by MPD. Do not transcode lossy audio to FLAC and describe it as lossless.

## Repository baseline

- Repository: `bartoszruta26-droid/malp-`
- Base branch: `master`
- Baseline commit recorded earlier: `fd72e3e41ef2f60af24d53beb0f0b6db7a34d4e2`
- Feature branch: `feature/youtube-audio-bridge`
- Android application is Java/Gradle; the Bash/C++ restriction applies to the new Pi-side integration, not to rewriting the existing Android app.

## Confirmed MPD design constraint

MPD's documented `pipe` plugin is an **output** plugin: it starts a command and writes decoded PCM to the command's standard input. MPD's `fifo` output plugin also sends PCM out of MPD. Neither is an input pipe for feeding yt-dlp bytes into MPD.

The documented MPD input plugin reference does not describe a general-purpose stdin/FIFO input plugin. A local FIFO may or may not work through the local-file input path for a particular MPD build; this must be tested on the actual Raspberry Pi. Do not assume `mpc add /path/to/fifo` is supported.

Official reference: https://mpd.readthedocs.io/en/stable/plugins.html

If the installed MPD cannot read a FIFO as a local source, the direct FIFO architecture is blocked without a native C++ MPD input plugin or a separately approved local HTTP input fallback. Do not silently switch to HTTP.

## Added repository files

- `docs/yoinks-mpd/README.md` — architecture, MPD input limitation, audio policy, playlist design, security, verification plan and implementation phases.
- `scripts/yoinks-mpd/yt-dlp-to-fifo.sh` — experimental single-video producer. It sends yt-dlp stdout to a newly created FIFO, rejects playlist URLs, refuses existing paths/symlinks and removes only the FIFO it created.
- Root `README.md` — links to the new design and script and explicitly marks the approach experimental.

## Prototype limitations

The script is a producer-side proof-of-concept, not a complete Yoinks-MPD daemon:
- It supports one video only; playlist enumeration and per-track FIFO lifecycle are not implemented.
- It does not add items to MPD's queue.
- It does not establish that MPD can open/decode the FIFO.
- It does not implement the Pi-side authenticated request service.
- It does not claim end-to-end playback or Raspberry Pi hardware verification.
- It uses Bash and yt-dlp only; no Python code or runtime is introduced.

The original Yoinks TUI must not be assumed to provide non-interactive stdout streaming or playlist-to-MPD control. Yoinks-MPD is the integration module to be implemented around the yt-dlp engine.

## Required next work

1. Run `bash -n scripts/yoinks-mpd/yt-dlp-to-fifo.sh` in a Bash environment.
2. Test producer bytes with a separate FIFO reader, verify stderr/stdout separation, error propagation, SIGINT/SIGTERM cleanup and no media files on disk.
3. On the Raspberry Pi, record MPD version/build and test whether the local-file input can consume a FIFO and decode the selected source codec.
4. If FIFO input works, implement a Bash/C++ process supervisor and MPD queue integration. If it does not, stop and request approval for a native C++ input plugin or HTTP fallback.
5. Implement playlist expansion as separate ordered items, never as concatenated playlist stdout.
6. Connect MALP Share intents only after the Pi-side contract is stable.
7. Add automated tests and verify the existing MALP functions remain intact.
8. Merge to `master` only after successful CI and Raspberry Pi playback/regression tests.

## Test status

No Bash syntax test, FIFO consumer test, MPD playback test, Raspberry Pi test, Android build or emulator test is claimed by this audit. These require execution in a suitable environment. The GitHub file commits only establish that the source files were written to the feature branch.


## Raspberry Pi installer

Added:
- scripts/yoinks-mpd/install.sh — APT prerequisite bootstrapper; downloads the official architecture-specific standalone yt-dlp release, verifies SHA-256 against upstream SHA2-256SUMS, installs the binary and producer script, and runs version/syntax checks.
- docs/yoinks-mpd/INSTALL_RASPBERRY_PI.md — step-by-step Raspberry Pi OS/Debian installation and safe verification guide.

Safety boundary:
- Does not install, restart, reconfigure or alter MPD.
- Does not alter the MPD queue or expose a network service.
- Does not install Python packages or a system Python runtime.
- Does not claim end-to-end playback. MPD FIFO input remains a target-device compatibility proof.
- Supports aarch64, armv7l and x86_64; unsupported architectures fail closed.

The installer has not been executed on Raspberry Pi hardware in this environment. Target-device download, checksum and runtime verification remain pending.
