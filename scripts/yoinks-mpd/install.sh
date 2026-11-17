#!/usr/bin/env bash
# Install yt-dlp as a metadata/source resolver dependency for the HTTP proxy.
# This script does not install or configure the proxy service or alter MPD.
set -Eeuo pipefail
umask 077

readonly RELEASE="https://github.com/yt-dlp/yt-dlp/releases/latest/download"
readonly BIN="/usr/local/bin/yt-dlp"

fail() { printf 'yoinks-mpd-install: ERROR: %s\n' "$*" >&2; exit 1; }
info() { printf 'yoinks-mpd-install: %s\n' "$*" >&2; }

[[ $EUID -eq 0 ]] || fail "run with sudo"
command -v apt-get >/dev/null 2>&1 || fail "APT-based Debian/Raspberry Pi OS is required"
command -v curl >/dev/null 2>&1 || fail "curl is required"

case "$(uname -m)" in
  aarch64|arm64) asset=yt-dlp_linux_aarch64; archive=0 ;;
  armv7l|armv7*) asset=yt-dlp_linux_armv7l.zip; archive=1 ;;
  x86_64|amd64) asset=yt-dlp_linux; archive=0 ;;
  *) fail "unsupported CPU architecture: $(uname -m); supported: aarch64, armv7l, x86_64" ;;
esac

info "installing curl, unzip and CA certificates; MPD and its queue remain untouched"
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
"$BIN" --version >/dev/null || fail "yt-dlp version check failed"

info "installed yt-dlp $("$BIN" --version)"
info "yt-dlp is a resolver dependency only; no audio file download workflow is installed"
info "HTTP proxy service is not installed by this bootstrap script"
info "MPD was not installed, restarted, reconfigured, or given queue commands"
info "continue with docs/yoinks-mpd/INSTALL_RASPBERRY_PI.md"
