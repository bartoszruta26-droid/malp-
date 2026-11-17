#!/usr/bin/env bash
set -euo pipefail

fixture_binary="$1"
tls_port=18444
http_port=18791
mpd_port=18792
token="test-token-with-at-least-256-bits-of-entropy-123456789"
tmp="$(mktemp -d)"
tls_pid=""
mpd_pid=""
server_pid=""

cleanup() {
    for pid in "$server_pid" "$mpd_pid" "$tls_pid"; do
        if [[ -n "$pid" ]]; then
            kill "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
        fi
    done
    rm -rf "$tmp"
}
trap cleanup EXIT
trap 'status=$?; echo "Real MPD integration failed at line $LINENO: $BASH_COMMAND" >&2; cat "$tmp/mpd.log" "$tmp/bridge.log" "$tmp/tls.log" >&2 2>/dev/null || true; exit "$status"' ERR

openssl req -x509 -newkey rsa:2048 -nodes -keyout "$tmp/key.pem" -out "$tmp/cert.pem" -days 1 -subj "/CN=rr1.googlevideo.com" -addext "subjectAltName=DNS:rr1.googlevideo.com" -addext "basicConstraints=critical,CA:TRUE" >/dev/null 2>&1

# A valid 10-second, mono, 8 kHz, 16-bit PCM WAV; silence is sufficient for a null output.
printf '%b' 'RIFF\304\217\002\000WAVEfmt \020\000\000\000\001\000\001\000\100\037\000\000\200\076\000\000\002\000\020\000data\240\217\002\000' >"$tmp/audio.wav"
dd if=/dev/zero bs=1 count=160000 >>"$tmp/audio.wav" 2>/dev/null

cat >"$tmp/yt-dlp-fixture" <<'RESOLVER'
#!/usr/bin/env bash
set -euo pipefail
for argument in "$@"; do
    if [[ "$argument" == "--get-url" ]]; then
        printf '%s\n' 'https://rr1.googlevideo.com/audio.wav'
        exit 0
    fi
done
printf '%s\n' '{"id":"qqAYW7uxErI","title":"Real MPD null-output fixture","duration":10}'
RESOLVER
chmod 700 "$tmp/yt-dlp-fixture"

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
    name "integration-test-null"
}
MPDCONF
mkdir -p "$tmp/playlists"

(cd "$tmp" && openssl s_server -accept "$tls_port" -cert cert.pem -key key.pem -WWW -quiet -naccept 1) >"$tmp/tls.log" 2>&1 &
tls_pid=$!
sleep 0.3
kill -0 "$tls_pid"

mpd --no-daemon --stderr "$tmp/mpd.conf" >"$tmp/mpd.stdout" 2>"$tmp/mpd.log" &
mpd_pid=$!
mpd_ready=0
for _ in $(seq 1 100); do
    if mpc -h 127.0.0.1 -p "$mpd_port" status >/dev/null 2>&1; then
        mpd_ready=1
        break
    fi
    if ! kill -0 "$mpd_pid" 2>/dev/null; then break; fi
    sleep 0.1
done
if [[ "$mpd_ready" != "1" ]]; then
    cat "$tmp/mpd.log" "$tmp/mpd.stdout" >&2
    echo "Real MPD daemon did not start" >&2
    exit 1
fi

YOINKS_MPD_TOKEN="$token" YOINKS_MPD_PORT="$http_port" \
YOINKS_MPD_MPD_PORT="$mpd_port" YOINKS_MPD_YTDLP="$tmp/yt-dlp-fixture" \
YOINKS_MPD_TEST_CA_BUNDLE="$tmp/cert.pem" \
YOINKS_MPD_TEST_CONNECT_TO="rr1.googlevideo.com:443:127.0.0.1:$tls_port" \
    "$fixture_binary" >/dev/null 2>"$tmp/bridge.log" &
server_pid=$!

bridge_ready=0
for _ in $(seq 1 100); do
    if curl --silent --fail "http://127.0.0.1:$http_port/api/v1/health" >/dev/null; then
        bridge_ready=1
        break
    fi
    sleep 0.1
done
[[ "$bridge_ready" == "1" ]]

response="$(curl --silent --show-error -H "Authorization: Bearer $token" \
    -H 'Content-Type: application/json' \
    -d '{"url":"https://www.youtube.com/watch?v=qqAYW7uxErI","mode":"play_now"}' \
    "http://127.0.0.1:$http_port/api/v1/queue")"
grep -q '"success":true' <<<"$response"
grep -q '"started":true' <<<"$response"

# Confirm the real MPD daemon accepted and started the HTTP/CURL input stream.
playing=0
for _ in $(seq 1 100); do
    status="$(mpc -h 127.0.0.1 -p "$mpd_port" status 2>/dev/null || true)"
    if grep -q '\[playing\]' <<<"$status"; then
        playing=1
        break
    fi
    sleep 0.1
done
if [[ "$playing" != "1" ]]; then
    cat "$tmp/mpd.log" "$tmp/mpd.stdout" "$tmp/bridge.log" "$tmp/tls.log" >&2
    echo "MPD did not enter playing state for the proxied WAV stream" >&2
    exit 1
fi

# MPD must store only the loopback proxy URI, never the signed CDN URL.
playlist="$(mpc -h 127.0.0.1 -p "$mpd_port" playlist)"
grep -Eq '^http://127\.0\.0\.1:' <<<"$playlist"
if grep -q 'googlevideo\|youtube\.com' <<<"$playlist"; then
    echo "MPD playlist leaked an upstream URL" >&2
    exit 1
fi

echo "Real MPD daemon accepted and played the HTTP proxy WAV stream"
