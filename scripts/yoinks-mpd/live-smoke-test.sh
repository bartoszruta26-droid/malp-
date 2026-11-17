#!/usr/bin/env bash
# Opt-in live YouTube -> yt-dlp -> local proxy -> isolated MPD test.
# Uses a null output, temporary MPD state and no media-file/cache download.
set -Eeuo pipefail
umask 077

fail() { printf 'yoinks-mpd-live-test: ERROR: %s\n' "$*" >&2; exit 1; }
info() { printf 'yoinks-mpd-live-test: %s\n' "$*" >&2; }
have() { command -v "$1" >/dev/null 2>&1; }

[[ ${EUID} -ne 0 ]] || fail "run as an ordinary user, not root"
[[ $# -eq 1 ]] || fail "usage: bash scripts/yoinks-mpd/live-smoke-test.sh <YouTube-URL>"
video_url="$1"

for tool in mpd mpc curl openssl; do have "$tool" || fail "required command not found: $tool"; done
mpd_bin="${YOINKS_MPD_BIN:-/usr/local/bin/yoinks-mpd}"
ytdlp_bin="${YOINKS_MPD_YTDLP:-/usr/local/bin/yt-dlp}"
[[ -x "$mpd_bin" ]] || fail "Yoinks-MPD binary not executable: $mpd_bin"
[[ -x "$ytdlp_bin" ]] || fail "yt-dlp binary not executable: $ytdlp_bin"
mpd --version 2>&1 | grep -Eiq 'curl' || fail "installed MPD build does not advertise CURL input support"

choose_port() {
    local excluded="${1:-}" port
    for port in $(seq 19000 29999); do
        [[ "$port" == "$excluded" ]] && continue
        if ! (exec 3<>"/dev/tcp/127.0.0.1/$port") 2>/dev/null; then
            printf '%s\n' "$port"
            return 0
        fi
    done
    return 1
}

tmp="$(mktemp -d "${TMPDIR:-/tmp}/yoinks-mpd-live.XXXXXXXX")"
mpd_pid=""
bridge_pid=""
cleanup() {
    for pid in "$bridge_pid" "$mpd_pid"; do
        if [[ -n "$pid" ]]; then
            kill -TERM "$pid" 2>/dev/null || true
            for _ in $(seq 1 30); do
                kill -0 "$pid" 2>/dev/null || break
                sleep 0.1
            done
            kill -KILL "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
        fi
    done
    rm -rf -- "$tmp"
}
trap cleanup EXIT INT TERM HUP
trap 'status=$?; echo "Live test failed at line $LINENO: $BASH_COMMAND" >&2; for f in "$tmp/mpd.log" "$tmp/bridge.log"; do [[ ! -f "$f" ]] || cat "$f" >&2; done; exit "$status"' ERR

mpd_port="$(choose_port)" || fail "no free TCP port for isolated MPD"
api_port="$(choose_port "$mpd_port")" || fail "no free TCP port for isolated bridge"

token="$(openssl rand -base64 48 | tr -d '\n=' | tr '+/' '-_')"
[[ ${#token} -ge 43 ]] || fail "could not generate API token"

mkdir -p "$tmp/playlists"
cat >"$tmp/mpd.conf" <<MPDCONF
music_directory "$tmp"
playlist_directory "$tmp/playlists"
db_file "$tmp/database"
state_file "$tmp/state"
sticker_file "$tmp/sticker.sql"
log_file "$tmp/mpd.log"
pid_file "$tmp/mpd.pid"
bind_to_address "127.0.0.1"
port "$mpd_port"
auto_update "no"
restore_paused "yes"
audio_output {
    type "null"
    name "yoinks-live-test-null"
}
MPDCONF

info "starting isolated MPD on 127.0.0.1:$mpd_port (null audio output)"
mpd --no-daemon --stderr "$tmp/mpd.conf" >"$tmp/mpd.stdout" 2>"$tmp/mpd.stderr" &
mpd_pid=$!
mpd_ready=0
for _ in $(seq 1 100); do
    if mpc -h 127.0.0.1 -p "$mpd_port" status >/dev/null 2>&1; then
        mpd_ready=1
        break
    fi
    kill -0 "$mpd_pid" 2>/dev/null || break
    sleep 0.1
done
[[ "$mpd_ready" == "1" ]] || fail "isolated MPD did not start"

info "starting Yoinks-MPD with temporary token and isolated MPD port"
YOINKS_MPD_BIND=127.0.0.1 YOINKS_MPD_PORT="$api_port" \
YOINKS_MPD_MPD_PORT="$mpd_port" YOINKS_MPD_TOKEN="$token" \
YOINKS_MPD_YTDLP="$ytdlp_bin" \
    "$mpd_bin" >/dev/null 2>"$tmp/bridge.log" &
bridge_pid=$!
bridge_ready=0
for _ in $(seq 1 100); do
    if curl --silent --fail "http://127.0.0.1:$api_port/api/v1/health" >/dev/null; then
        bridge_ready=1
        break
    fi
    kill -0 "$bridge_pid" 2>/dev/null || break
    sleep 0.1
done
[[ "$bridge_ready" == "1" ]] || fail "Yoinks-MPD bridge did not start"

info "resolving and streaming the supplied YouTube URL (yt-dlp cache/download disabled)"
[[ "$video_url" != *'"'* && "$video_url" != *'\\'* && "$video_url" != *$'\n'* && "$video_url" != *$'\r'* ]] \
    || fail "URL contains characters that cannot be safely embedded in this JSON request"
payload="$(printf '{"url":"%s","mode":"play_now"}' "$video_url")"
response="$(curl --silent --show-error --fail --max-time 75 \
    -H "Authorization: Bearer $token" -H 'Content-Type: application/json' \
    --data-binary "$payload" "http://127.0.0.1:$api_port/api/v1/queue")"
grep -q '"success":true' <<<"$response" || fail "queue API did not confirm success: $response"
grep -q '"started":true' <<<"$response" || fail "MPD did not confirm play_now: $response"

playing=0
for _ in $(seq 1 200); do
    status="$(mpc -h 127.0.0.1 -p "$mpd_port" status 2>/dev/null || true)"
    if grep -q '\[playing\]' <<<"$status"; then
        playing=1
        break
    fi
    sleep 0.1
done
[[ "$playing" == "1" ]] || fail "isolated MPD did not enter playing state"

playlist="$(mpc -h 127.0.0.1 -p "$mpd_port" playlist)"
grep -Eq "^http://127\.0\.0\.1:$api_port/stream/[0-9a-f]{32}$" <<<"$playlist" \
    || fail "MPD playlist does not contain the expected local proxy URI"
if grep -q 'googlevideo\|youtube\.com' <<<"$playlist"; then
    fail "MPD playlist contains an upstream URL instead of the local proxy URI"
fi

if find "$tmp" -type f \( -iname '*.mp3' -o -iname '*.m4a' -o -iname '*.webm' -o -iname '*.opus' -o -iname '*.ogg' -o -iname '*.wav' \) -print -quit | grep -q .; then
    fail "unexpected persistent media file found in temporary test directory"
fi

info "PASS: live YouTube -> yt-dlp -> HTTPS CDN -> RAM proxy -> real MPD CURL input"
info "MPD entered playing state; audio was discarded by the null output, not sent to speakers"
info "MPD queue, state and database were isolated under $tmp and will be removed on exit"
\n'/\\n}"
json_url="${json_url//
grep -q '"success":true' <<<"$response" || fail "queue API did not confirm success: $response"
grep -q '"started":true' <<<"$response" || fail "MPD did not confirm play_now: $response"

playing=0
for _ in $(seq 1 200); do
    status="$(mpc -h 127.0.0.1 -p "$mpd_port" status 2>/dev/null || true)"
    if grep -q '\[playing\]' <<<"$status"; then
        playing=1
        break
    fi
    sleep 0.1
done
[[ "$playing" == "1" ]] || fail "isolated MPD did not enter playing state"

playlist="$(mpc -h 127.0.0.1 -p "$mpd_port" playlist)"
grep -Eq "^http://127\.0\.0\.1:$api_port/stream/[0-9a-f]{32}$" <<<"$playlist" \
    || fail "MPD playlist does not contain the expected local proxy URI"
if grep -q 'googlevideo\|youtube\.com' <<<"$playlist"; then
    fail "MPD playlist contains an upstream URL instead of the local proxy URI"
fi

if find "$tmp" -type f \( -iname '*.mp3' -o -iname '*.m4a' -o -iname '*.webm' -o -iname '*.opus' -o -iname '*.ogg' -o -iname '*.wav' \) -print -quit | grep -q .; then
    fail "unexpected persistent media file found in temporary test directory"
fi

info "PASS: live YouTube -> yt-dlp -> HTTPS CDN -> RAM proxy -> real MPD CURL input"
info "MPD entered playing state; audio was discarded by the null output, not sent to speakers"
info "MPD queue, state and database were isolated under $tmp and will be removed on exit"
\r'/\\r}"
response="$(curl --silent --show-error --fail --max-time 75 \
    -H "Authorization: Bearer $token" -H 'Content-Type: application/json' \
    --data-binary "{\"url\":\"$json_url\",\"mode\":\"play_now\"}" \
    "http://127.0.0.1:$api_port/api/v1/queue")"
grep -q '"success":true' <<<"$response" || fail "queue API did not confirm success: $response"
grep -q '"started":true' <<<"$response" || fail "MPD did not confirm play_now: $response"

playing=0
for _ in $(seq 1 200); do
    status="$(mpc -h 127.0.0.1 -p "$mpd_port" status 2>/dev/null || true)"
    if grep -q '\[playing\]' <<<"$status"; then
        playing=1
        break
    fi
    sleep 0.1
done
[[ "$playing" == "1" ]] || fail "isolated MPD did not enter playing state"

playlist="$(mpc -h 127.0.0.1 -p "$mpd_port" playlist)"
grep -Eq "^http://127\.0\.0\.1:$api_port/stream/[0-9a-f]{32}$" <<<"$playlist" \
    || fail "MPD playlist does not contain the expected local proxy URI"
if grep -q 'googlevideo\|youtube\.com' <<<"$playlist"; then
    fail "MPD playlist contains an upstream URL instead of the local proxy URI"
fi

if find "$tmp" -type f \( -iname '*.mp3' -o -iname '*.m4a' -o -iname '*.webm' -o -iname '*.opus' -o -iname '*.ogg' -o -iname '*.wav' \) -print -quit | grep -q .; then
    fail "unexpected persistent media file found in temporary test directory"
fi

info "PASS: live YouTube -> yt-dlp -> HTTPS CDN -> RAM proxy -> real MPD CURL input"
info "MPD entered playing state; audio was discarded by the null output, not sent to speakers"
info "MPD queue, state and database were isolated under $tmp and will be removed on exit"
