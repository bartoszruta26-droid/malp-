#!/usr/bin/env bash
set -euo pipefail

fixture_binary="$1"
mock_mpd_binary="$2"
port=18789
mpd_port=18790
tls_port=18443
token="test-token-with-at-least-256-bits-of-entropy-123456789"
log_file="$(mktemp)"
tls_log="$(mktemp)"
resolver_file="$(mktemp)"
mpd_log="$(mktemp)"
headers_file="$(mktemp)"
body_file="$(mktemp)"
expected_file="$(mktemp)"
tls_dir="$(mktemp -d)"
tls_pid=""
mpd_pid=""
server_pid=""

cleanup() {
    if [[ -n "$server_pid" ]]; then
        kill "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
    fi
    if [[ -n "$tls_pid" ]]; then
        kill "$tls_pid" 2>/dev/null || true
        wait "$tls_pid" 2>/dev/null || true
    fi
    if [[ -n "$mpd_pid" ]]; then
        kill "$mpd_pid" 2>/dev/null || true
        wait "$mpd_pid" 2>/dev/null || true
    fi
    rm -f "$log_file" "$tls_log" "$resolver_file" "$mpd_log" "$headers_file" "$body_file" "$expected_file"
    rm -rf "$tls_dir"
}
trap cleanup EXIT

openssl req -x509 -newkey rsa:2048 -nodes -keyout "$tls_dir/key.pem" -out "$tls_dir/cert.pem" -days 1 -subj "/CN=rr1.googlevideo.com" -addext "subjectAltName=DNS:rr1.googlevideo.com" -addext "basicConstraints=critical,CA:TRUE" >/dev/null 2>&1
printf '%s' 'yoinks-mpd-proxy-fixture
second-chunk
third-chunk
' >"$expected_file"
cp "$expected_file" "$tls_dir/audio.bin"

cat >"$resolver_file" <<'RESOLVER'
#!/usr/bin/env bash
set -euo pipefail
for argument in "$@"; do
    if [[ "$argument" == "--get-url" ]]; then
        printf '%s\n' 'https://rr1.googlevideo.com/audio.bin'
        exit 0
    fi
done
printf '%s\n' '{"id":"qqAYW7uxErI","title":"Stream fixture","duration":1}'
RESOLVER
chmod 700 "$resolver_file"

(cd "$tls_dir" && openssl s_server -accept "$tls_port" -cert cert.pem -key key.pem -WWW -quiet -naccept 1) >"$tls_log" 2>&1 &
tls_pid=$!
sleep 0.3
kill -0 "$tls_pid"

"$mock_mpd_binary" "$mpd_port" "$mpd_log" >/dev/null 2>"$log_file" &
mpd_pid=$!
mpd_ready=0
for _ in $(seq 1 50); do
    if (exec 3<>"/dev/tcp/127.0.0.1/$mpd_port") 2>/dev/null; then
        mpd_ready=1
        break
    fi
    sleep 0.1
done
[[ "$mpd_ready" == "1" ]]

YOINKS_MPD_TOKEN="$token" YOINKS_MPD_PORT="$port" YOINKS_MPD_MPD_PORT="$mpd_port" YOINKS_MPD_YTDLP="$resolver_file" YOINKS_MPD_TEST_CA_BUNDLE="$tls_dir/cert.pem" YOINKS_MPD_TEST_CONNECT_TO="rr1.googlevideo.com:443:127.0.0.1:$tls_port" "$fixture_binary" >/dev/null 2>"$log_file" &
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
    cat "$log_file" "$tls_log" >&2
    echo "HTTP stream fixture did not become ready" >&2
    exit 1
fi

queue_response="$(curl --silent --show-error -H "Authorization: Bearer $token" -H 'Content-Type: application/json' -d '{"url":"https://www.youtube.com/watch?v=qqAYW7uxErI","mode":"enqueue"}' "http://127.0.0.1:$port/api/v1/queue")"
grep -q '"success":true' <<<"$queue_response"
grep -q '"queued":1' <<<"$queue_response"
grep -Eq '^addid "http://127\.0\.0\.1:18789/stream/[0-9a-f]{32}"$' "$mpd_log"
stream_id="$(grep -Eo '/stream/[0-9a-f]{32}' "$mpd_log" | cut -d/ -f3 | head -n 1)"
[[ "$stream_id" =~ ^[0-9a-f]{32}$ ]]

stream_status="$(curl --silent --show-error -D "$headers_file" -o "$body_file" -w '%{http_code}' "http://127.0.0.1:$port/stream/$stream_id")"
[[ "$stream_status" == "200" ]]
grep -qi '^transfer-encoding: chunked' "$headers_file"
cmp "$expected_file" "$body_file"

kill -TERM "$server_pid"
wait "$server_pid"
server_pid=""
wait "$tls_pid"
tls_pid=""

echo "HTTP stream TLS byte integration test passed"
