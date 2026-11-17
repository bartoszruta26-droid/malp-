#!/usr/bin/env bash
set -euo pipefail

fixture_binary="$1"
mock_mpd_binary="$2"
port=18789
mpd_port=18790
token="test-token-with-at-least-256-bits-of-entropy-123456789"
log_file="$(mktemp)"
resolver_file="$(mktemp)"
mpd_log="$(mktemp)"
headers_file="$(mktemp)"
body_file="$(mktemp)"
expected_file="$(mktemp)"
cat >"$resolver_file" <<'RESOLVER'
#!/usr/bin/env bash
set -euo pipefail
for argument in "$@"; do
    if [[ "$argument" == "--get-url" ]]; then
        printf '%s\n' 'https://rr1.googlevideo.com/videoplayback?fixture=1'
        exit 0
    fi
done
printf '%s\n' '{"id":"qqAYW7uxErI","title":"Stream fixture","duration":1}'
RESOLVER
chmod 700 "$resolver_file"

"$mock_mpd_binary" "$mpd_port" "$mpd_log" >/dev/null 2>"$log_file" &
mpd_pid=$!
server_pid=""
cleanup() {
    if [[ -n "$server_pid" ]]; then
        kill "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
    fi
    kill "$mpd_pid" 2>/dev/null || true
    wait "$mpd_pid" 2>/dev/null || true
    rm -f "$log_file" "$resolver_file" "$mpd_log" "$headers_file" "$body_file" "$expected_file"
}
trap cleanup EXIT

mpd_ready=0
for _ in $(seq 1 50); do
    if (exec 3<>"/dev/tcp/127.0.0.1/$mpd_port") 2>/dev/null; then
        mpd_ready=1
        break
    fi
    sleep 0.1
done
[[ "$mpd_ready" == "1" ]]

YOINKS_MPD_TOKEN="$token" YOINKS_MPD_PORT="$port" \
YOINKS_MPD_MPD_PORT="$mpd_port" YOINKS_MPD_YTDLP="$resolver_file" \
    "$fixture_binary" >/dev/null 2>"$log_file" &
server_pid=$!

ready=0
for _ in $(seq 1 50); do
    if curl --silent --fail "http://127.0.0.1:$port/api/v1/health" >/dev/null; then
        ready=1
        break
    fi
    sleep 0.1
done
if [[ "$ready" != "1" ]]; then
    cat "$log_file" >&2
    echo "HTTP stream fixture did not become ready" >&2
    exit 1
fi

queue_response="$(curl --silent --show-error \
    -H "Authorization: Bearer $token" -H 'Content-Type: application/json' \
    -d '{"url":"https://www.youtube.com/watch?v=qqAYW7uxErI","mode":"enqueue"}' \
    "http://127.0.0.1:$port/api/v1/queue")"
grep -q '"success":true' <<<"$queue_response"
grep -q '"queued":1' <<<"$queue_response"
grep -Eq '^addid "http://127\.0\.0\.1:18789/stream/[0-9a-f]{32}"$' "$mpd_log"
stream_id="$(grep -Eo '/stream/[0-9a-f]{32}' "$mpd_log" | cut -d/ -f3 | head -n 1)"
[[ "$stream_id" =~ ^[0-9a-f]{32}$ ]]

stream_status="$(curl --silent --show-error -D "$headers_file" -o "$body_file" \
    -w '%{http_code}' "http://127.0.0.1:$port/stream/$stream_id")"
[[ "$stream_status" == "200" ]]
grep -qi '^transfer-encoding: chunked' "$headers_file"
printf '%s' 'yoinks-mpd-proxy-fixture
second-chunk
third-chunk
' >"$expected_file"
cmp "$expected_file" "$body_file"

kill -TERM "$server_pid"
wait "$server_pid"
server_pid=""

echo "HTTP stream byte integration test passed"
