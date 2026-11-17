#!/usr/bin/env bash
set -euo pipefail

binary="$1"
port=18787
token="test-token-with-at-least-256-bits-of-entropy-123456789"
log_file="$(mktemp)"
resolver_file="$(mktemp)"
cat >"$resolver_file" <<'RESOLVER'
#!/bin/sh
printf '%s\n' '{"id":"qqAYW7uxErI","title":"HTTP integration fixture","duration":1}'
RESOLVER
chmod 700 "$resolver_file"
YOINKS_MPD_TOKEN="$token" YOINKS_MPD_PORT="$port" \
YOINKS_MPD_MPD_PORT=1 YOINKS_MPD_YTDLP="$resolver_file" \
    "$binary" > /dev/null 2>"$log_file" &
server_pid=$!
cleanup() {
    if [[ -n "$server_pid" ]]; then
        kill "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
    fi
    rm -f "$log_file" "$resolver_file"
}
trap cleanup EXIT

ready=0
for _ in $(seq 1 50); do
    if curl --silent --fail "http://127.0.0.1:$port/api/v1/health" >/dev/null; then
        ready=1
        break
    fi
    sleep 0.1
done
if [[ "$ready" -ne 1 ]]; then
    cat "$log_file" >&2
    echo "HTTP server did not become ready" >&2
    exit 1
fi

health="$(curl --silent --show-error "http://127.0.0.1:$port/api/v1/health")"
grep -q '"api_version":1' <<<"$health"
grep -q '"control_ready":false' <<<"$health"

# Stream route rejects unsupported methods and Range rather than returning misleading media.
invalid_stream_status="$(curl --silent -o /dev/null -w '%{http_code}' "http://127.0.0.1:$port/stream/invalid")"
[[ "$invalid_stream_status" == "404" ]]

head_stream_status="$(curl --silent -I -o /dev/null -w '%{http_code}' "http://127.0.0.1:$port/stream/0123456789abcdef0123456789abcdef")"
[[ "$head_stream_status" == "405" ]]

range_stream_status="$(curl --silent -o /dev/null -w '%{http_code}' -H 'Range: bytes=0-99' "http://127.0.0.1:$port/stream/0123456789abcdef0123456789abcdef")"
[[ "$range_stream_status" == "501" ]]

unknown_stream_status="$(curl --silent -o /dev/null -w '%{http_code}' "http://127.0.0.1:$port/stream/0123456789abcdef0123456789abcdef")"
[[ "$unknown_stream_status" == "404" ]]

# Multiple independent clients must be served concurrently.
seq 1 8 | xargs -P 8 -I '{}' sh -c \
    'curl --silent --show-error --fail "http://127.0.0.1:'"$port"'/api/v1/health" >/dev/null'

unauthorized_status="$(curl --silent -o /dev/null -w '%{http_code}' \
    -H 'Content-Type: application/json' \
    -d '{"url":"https://youtu.be/dQw4w9WgXcQ","mode":"enqueue"}' \
    "http://127.0.0.1:$port/api/v1/queue")"
[[ "$unauthorized_status" == "401" ]]

invalid_mode_status="$(curl --silent -o /dev/null -w '%{http_code}' \
    -H "Authorization: Bearer $token" -H 'Content-Type: application/json' \
    -d '{"url":"https://youtu.be/dQw4w9WgXcQ","mode":"clear"}' \
    "http://127.0.0.1:$port/api/v1/queue")"
[[ "$invalid_mode_status" == "400" ]]

invalid_url_status="$(curl --silent -o /dev/null -w '%{http_code}' \
    -H "Authorization: Bearer $token" -H 'Content-Type: application/json' \
    -d '{"url":"https://youtube.com.evil.example/watch?v=dQw4w9WgXcQ","mode":"enqueue"}' \
    "http://127.0.0.1:$port/api/v1/queue")"
[[ "$invalid_url_status" == "422" ]]

# A deterministic yt-dlp fixture supplies metadata; MPD port 1 is deliberately closed.
# The HTTP path must therefore reach the MPD adapter and report failure, never false success.
test_video_status="$(curl --silent -o /dev/null -w '%{http_code}' \
    -H "Authorization: Bearer $token" -H 'Content-Type: application/json' \
    -d '{"url":"https://www.youtube.com/watch?v=qqAYW7uxErI","mode":"enqueue"}' \
    "http://127.0.0.1:$port/api/v1/queue")"
[[ "$test_video_status" == "502" ]]

large_body="$(printf '%*s' 17000 '' | tr ' ' 'a')"
large_status="$(curl --silent -o /dev/null -w '%{http_code}' \
    -H "Authorization: Bearer $token" -H 'Content-Type: application/json' \
    --data-binary "$large_body" "http://127.0.0.1:$port/api/v1/queue")"
[[ "$large_status" == "413" ]]

# SIGTERM must close the listener and drain the worker pools cleanly.
kill -TERM "$server_pid"
shutdown_complete=0
for _ in $(seq 1 50); do
    process_state="$(ps -o stat= -p "$server_pid" 2>/dev/null || true)"
    if [[ -z "$process_state" || "$process_state" == Z* ]]; then
        shutdown_complete=1
        break
    fi
    sleep 0.1
done
if [[ "$shutdown_complete" -ne 1 ]]; then
    echo "HTTP server did not shut down after SIGTERM" >&2
    exit 1
fi
wait "$server_pid"
server_pid=""

echo "HTTP API integration tests passed"
