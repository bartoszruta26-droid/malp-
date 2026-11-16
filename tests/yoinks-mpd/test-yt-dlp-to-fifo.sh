#!/usr/bin/env bash
# Offline regression tests for the Yoinks-MPD FIFO producer.
set -Eeuo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
PRODUCER="$ROOT/scripts/yoinks-mpd/yt-dlp-to-fifo.sh"
TMP=$(mktemp -d)
trap 'rm -rf -- "$TMP"' EXIT
MOCK_BIN="$TMP/bin"
mkdir -p "$MOCK_BIN"
export MOCK_BIN
export MOCK_YTDLP_MODE=success

fail() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }
pass() { printf 'PASS: %s\n' "$*"; }

[[ -f bash "$PRODUCER" ]] || fail "producer is missing"
command -v bash >/dev/null
command -v cmp >/dev/null
command -v mkfifo >/dev/null

cat > "$MOCK_BIN/yt-dlp" <<'MOCK'
#!/usr/bin/env bash
set -eu
case "${MOCK_YTDLP_MODE:-success}" in
  success)
    printf 'YOINKS_TEST_AUDIO_BYTES\000\377'
    printf 'mock yt-dlp diagnostic\n' >&2
    ;;
  fail)
    printf 'mock yt-dlp failure\n' >&2
    exit 23
    ;;
  *) exit 99 ;;
esac
MOCK
chmod 0755 "$MOCK_BIN/yt-dlp"
export PATH="$MOCK_BIN:$PATH"

VIDEO_URL='https://www.youtube.com/watch?v=offline-test'
EXPECTED="$TMP/expected.bin"
printf 'YOINKS_TEST_AUDIO_BYTES\000\377' > "$EXPECTED"

# Successful stream: a separate reader consumes the FIFO.
FIFO="$TMP/audio.fifo"
OUTPUT="$TMP/output.bin"
bash "$PRODUCER" "$VIDEO_URL" "$FIFO" 2>"$TMP/producer.stderr" &
producer_pid=$!
for _ in {1..100}; do
  [[ -p "$FIFO" ]] && break
  sleep 0.02
done
[[ -p "$FIFO" ]] || { kill "$producer_pid" 2>/dev/null || true; fail "FIFO was not created"; }
cat "$FIFO" > "$OUTPUT" &
reader_pid=$!
if ! wait "$producer_pid"; then
  wait "$reader_pid" 2>/dev/null || true
  fail "producer returned failure for successful mock stream"
fi
wait "$reader_pid"
cmp -s "$EXPECTED" "$OUTPUT" || fail "streamed bytes differ from expected payload"
[[ ! -e "$FIFO" ]] || fail "producer did not remove its FIFO"
grep -q 'mock yt-dlp diagnostic' "$TMP/producer.stderr" || fail "yt-dlp diagnostics were not captured on stderr"
pass "streams binary stdout through FIFO and cleans up"

# Playlist URL must be rejected before yt-dlp runs.
if bash "$PRODUCER" 'https://www.youtube.com/watch?v=x&list=PL123' "$TMP/playlist.fifo" 2>"$TMP/playlist.stderr"; then
  fail "playlist URL was accepted"
fi
[[ ! -e "$TMP/playlist.fifo" ]] || fail "playlist rejection created a FIFO"
pass "rejects playlist URLs"

# Unsupported host must be rejected.
if bash "$PRODUCER" 'https://www.youtube.com.evil.example/watch?v=x' "$TMP/evil.fifo" 2>"$TMP/evil.stderr"; then
  fail "lookalike host was accepted"
fi
[[ ! -e "$TMP/evil.fifo" ]] || fail "invalid URL created a FIFO"
pass "rejects lookalike YouTube hosts"

# Existing regular file must remain untouched.
EXISTING="$TMP/existing"
printf 'preserve-me' > "$EXISTING"
if bash "$PRODUCER" "$VIDEO_URL" "$EXISTING" 2>"$TMP/existing.stderr"; then
  fail "existing regular file was accepted as FIFO target"
fi
[[ "$(cat "$EXISTING")" == 'preserve-me' ]] || fail "existing file was modified"
pass "refuses to overwrite existing files"

# Existing symlink must remain untouched.
TARGET="$TMP/target"
LINK="$TMP/link.fifo"
: > "$TARGET"
ln -s "$TARGET" "$LINK"
if bash "$PRODUCER" "$VIDEO_URL" "$LINK" 2>"$TMP/link.stderr"; then
  fail "symlink target was accepted"
fi
[[ -L "$LINK" && -f "$TARGET" ]] || fail "symlink or its target was modified"
pass "refuses symlink targets"

# yt-dlp failure must propagate and the producer-owned FIFO must be removed.
export MOCK_YTDLP_MODE=fail
FAIL_FIFO="$TMP/failure.fifo"
bash "$PRODUCER" "$VIDEO_URL" "$FAIL_FIFO" 2>"$TMP/failure.stderr" &
producer_pid=$!
for _ in {1..100}; do
  [[ -p "$FAIL_FIFO" ]] && break
  sleep 0.02
done
[[ -p "$FAIL_FIFO" ]] || { kill "$producer_pid" 2>/dev/null || true; fail "failure-test FIFO was not created"; }
cat "$FAIL_FIFO" >/dev/null &
reader_pid=$!
if wait "$producer_pid"; then
  wait "$reader_pid" 2>/dev/null || true
  fail "yt-dlp exit status was not propagated"
fi
wait "$reader_pid"
[[ ! -e "$FAIL_FIFO" ]] || fail "FIFO remained after yt-dlp failure"
pass "propagates yt-dlp failure and cleans up"

printf '\nAll Yoinks-MPD producer tests passed.\n'
