#!/usr/bin/env bash
set -euo pipefail

binary="$1"
port=18787
token="test-token-with-at-least-256-bits-of-entropy-123456789"
log_file="$(mktemp)"
YOINKS_MPD_TOKEN="$token" YOINKS_MPD_PORT="$port" "$binary" > /dev/null 2>"$log_file" &
server_pid=$!
cleanup() {
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
    rm -f "$log_file"
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

not_ready_status="$(curl --silent -o /dev/null -w '%{http_code}' \
    -H "Authorization: Bearer $token" -H 'Content-Type: application/json' \
    -d '{"url":"https://youtu.be/dQw4w9WgXcQ","mode":"enqueue"}' \
    "http://127.0.0.1:$port/api/v1/queue")"
[[ "$not_ready_status" == "503" ]]

large_body="$(printf '%*s' 17000 '' | tr ' ' 'a')"
large_status="$(curl --silent -o /dev/null -w '%{http_code}' \
    -H "Authorization: Bearer $token" -H 'Content-Type: application/json' \
    --data-binary "$large_body" "http://127.0.0.1:$port/api/v1/queue")"
[[ "$large_status" == "413" ]]

echo "HTTP API integration tests passed"
