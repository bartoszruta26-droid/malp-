#!/usr/bin/env bash
#
# Experimental Yoinks-MPD producer:
#   yt-dlp binary stdout -> named FIFO
#
# This script does NOT prove that MPD can consume a FIFO. See
# docs/yoinks-mpd/README.md before attempting MPD integration.
#
set -Eeuo pipefail
umask 077

usage() {
    printf 'Usage: %s YOUTUBE_VIDEO_URL FIFO_PATH\n' "${0##*/}" >&2
    printf 'Streams one video to a FIFO; waits until a reader opens it.\n' >&2
}

fail() {
    printf 'yoinks-mpd: %s\n' "$*" >&2
    exit 1
}

[[ $# -eq 2 ]] || { usage; exit 2; }

url=$1
fifo=$2

command -v yt-dlp >/dev/null 2>&1 || fail "yt-dlp is required"
command -v mkfifo >/dev/null 2>&1 || fail "mkfifo is required"
command -v stat >/dev/null 2>&1 || fail "stat is required"

# Accept only HTTPS YouTube video/share hosts. Playlist orchestration is
# deliberately excluded from this single-item prototype.
case "$url" in
    https://www.youtube.com/watch\?*|https://youtube.com/watch\?*|https://m.youtube.com/watch\?*|https://music.youtube.com/watch\?*|https://youtu.be/*|https://www.youtube.com/shorts/*|https://www.youtube.com/live/*)
        ;;
    *)
        fail "unsupported URL; provide an HTTPS YouTube video URL (not a playlist)"
        ;;
esac

# Reject playlist URLs rather than accidentally concatenating multiple tracks.
case "$url" in
    *list=*) fail "playlist URLs are not supported by this single-item producer" ;;
esac

[[ "$fifo" = /* ]] || fail "FIFO path must be absolute"
parent=${fifo%/*}
[[ -d "$parent" ]] || fail "FIFO parent directory does not exist"
[[ ! -L "$fifo" ]] || fail "refusing symlink target"
[[ ! -e "$fifo" ]] || fail "target already exists; refusing to overwrite"

created_fifo=0
producer_pid=

cleanup() {
    local status=$?
    trap - EXIT INT TERM HUP PIPE
    if [[ -n "${producer_pid:-}" ]] && kill -0 "$producer_pid" 2>/dev/null; then
        kill "$producer_pid" 2>/dev/null || true
        wait "$producer_pid" 2>/dev/null || true
    fi
    if (( created_fifo )) && [[ -p "$fifo" ]]; then
        rm -f -- "$fifo"
    fi
    exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
trap 'exit 141' PIPE

mkfifo -m 600 -- "$fifo" || fail "could not create FIFO"
created_fifo=1

printf 'yoinks-mpd: waiting for a FIFO reader: %s\n' "$fifo" >&2
printf 'yoinks-mpd: streaming source audio; no media file is created\n' >&2

# yt-dlp writes media bytes to stdout. Diagnostics stay on stderr.
# --no-playlist prevents a playlist from becoming concatenated stdout.
# -f bestaudio/best chooses the best available audio-only format first.
# No ffmpeg conversion is requested, preserving the selected source codec.
yt-dlp \
    --no-playlist \
    --no-progress \
    --no-warnings \
    --no-part \
    --no-continue \
    --format 'bestaudio/best' \
    --output '-' \
    -- "$url" > "$fifo" &
producer_pid=$!

wait "$producer_pid"
status=$?
producer_pid=
exit "$status"
