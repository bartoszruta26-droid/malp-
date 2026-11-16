#!/usr/bin/env bash
# Raspberry Pi OS / Debian bootstrapper for the Yoinks-MPD producer.
# Deliberately does not configure, restart, or alter MPD.
set -Eeuo pipefail
umask 077

readonly RELEASE="https://github.com/yt-dlp/yt-dlp/releases/latest/download"
readonly BIN="/usr/local/bin/yt-dlp"
readonly LIB="/usr/local/libexec/yoinks-mpd"
readonly SOURCE="scripts/yoinks-mpd/yt-dlp-to-fifo.sh"

fail() { printf 'yoinks-mpd-install: ERROR: %s\n' "$*" >&2; exit 1; }
info() { printf 'yoinks-mpd-install: %s\n' "$*" >&2; }

[[ $EUID -eq 0 ]] || fail "run with sudo"
command -v apt-get >/dev/null 2>&1 || fail "APT-based Debian/Raspberry Pi OS is required"
command -v curl >/dev/null 2>&1 || fail "curl is required"

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$script_dir/../.." && pwd)
producer="$root/$SOURCE"
[[ -f "$producer" ]] || fail "repository checkout is incomplete; missing $SOURCE"

case "$(uname -m)" in
  aarch64|arm64) asset=yt-dlp_linux_aarch64; archive=0 ;;
  armv7l|armv7*) asset=yt-dlp_linux_armv7l.zip; archive=1 ;;
  x86_64|amd64) asset=yt-dlp_linux; archive=0 ;;
  *) fail "unsupported CPU architecture: $(uname -m); supported: aarch64, armv7l, x86_64" ;;
esac

info "installing curl, unzip and CA certificates; MPD will remain untouched"
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends ca-certificates curl unzip

tmp=$(mktemp -d)
cleanup() { rm -rf -- "$tmp"; }
trap cleanup EXIT INT TERM HUP

info "downloading official yt-dlp release asset: $asset"
curl -fLsS "$RELEASE/$asset" -o "$tmp/$asset"
curl -fLsS "$RELEASE/SHA2-256SUMS" -o "$tmp/SHA2-256SUMS"

expected=$(awk -v n="$asset" '$2 == n || $2 == "*"n {print $1; exit}' "$tmp/SHA2-256SUMS")
[[ -n "$expected" ]] || fail "upstream SHA-256 entry not found for $asset"
actual=$(sha256sum "$tmp/$asset" | awk '{print $1}')
[[ "$actual" == "$expected" ]] || fail "SHA-256 verification failed"

if (( archive )); then
  unzip -q "$tmp/$asset" -d "$tmp/unpacked"
  candidate="$tmp/unpacked/yt-dlp"
  [[ -f "$candidate" ]] || candidate=$(find "$tmp/unpacked" -type f -name yt-dlp -print -quit)
else
  candidate="$tmp/$asset"
fi
[[ -n "${candidate:-}" && -f "$candidate" ]] || fail "yt-dlp executable not found"
install -D -m 0755 "$candidate" "$BIN"
install -d -m 0755 "$LIB"
install -m 0755 "$producer" "$LIB/yt-dlp-to-fifo.sh"

"$BIN" --version >/dev/null || fail "yt-dlp version check failed"
bash -n "$LIB/yt-dlp-to-fifo.sh" || fail "producer Bash syntax check failed"

info "installed yt-dlp $("$BIN" --version)"
info "installed producer at $LIB/yt-dlp-to-fifo.sh"
info "MPD was not installed, restarted, reconfigured, or given queue commands"
info "continue with docs/yoinks-mpd/INSTALL_RASPBERRY_PI.md"
