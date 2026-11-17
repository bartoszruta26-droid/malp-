#!/usr/bin/env bash
# Read-only evidence collector for the MPD HTTP/CURL compatibility gate.
# Does not connect to MPD, alter its queue, start playback, or write media.
set -Eeuo pipefail

section() { printf "\n=== %s ===\n" "$1"; }
command_info() { local name="$1"; if command -v "$name" >/dev/null 2>&1; then printf "%s: %s\n" "$name" "$(command -v "$name")"; else printf "%s: NOT FOUND\n" "$name"; fi; }

section "timestamp and platform"
printf "UTC: "; date -u "+%Y-%m-%dT%H:%M:%SZ"
printf "kernel: "; uname -a
printf "architecture: "; uname -m

section "MPD and client versions"
command_info mpd
command_info mpc
if command -v mpd >/dev/null 2>&1; then mpd --version 2>&1; else printf "MPD version unavailable: mpd command not found\n"; fi
if command -v mpc >/dev/null 2>&1; then mpc version 2>&1 || true; fi

section "installed package versions"
if command -v dpkg-query >/dev/null 2>&1; then dpkg-query -W -f='${Package} ${Version} ${Architecture}\n' mpd mpc 2>&1 || true; fi

section "MPD configuration paths (names and plugin directives only)"
for candidate in /etc/mpd.conf /etc/mpd/mpd.conf "$HOME/.config/mpd/mpd.conf"; do
  if [[ -r "$candidate" ]]; then
    printf "readable config: %s\n" "$candidate"
    awk 'tolower($1) ~ /^(input|decoder|audio_output)$/ { print NR ":" $0 }' "$candidate"
  fi
done

section "CURL/input capability hints"
if command -v mpd >/dev/null 2>&1; then
  version_output=$(mpd --version 2>&1 || true)
  if grep -Eiq "(^|[^[:alnum:]_])curl([^[:alnum:]_]|$)" <<<"$version_output"; then printf "CURL keyword found in mpd --version. Inspect plugin list; this is a build hint, not playback proof.\n"; else printf "No CURL keyword found. This is inconclusive; verify build and input plugin availability.\n"; fi
fi

section "swap and zram (RAM-only prerequisite)"
if [[ -r /proc/swaps ]]; then cat /proc/swaps; else printf "/proc/swaps unavailable\n"; fi
if command -v zramctl >/dev/null 2>&1; then zramctl 2>&1 || true; else printf "zramctl: NOT FOUND\n"; fi

section "persistent mount points (inventory only)"
if command -v findmnt >/dev/null 2>&1; then findmnt -rn -o TARGET,SOURCE,FSTYPE,OPTIONS; fi

cat <<'EOF'

=== interpretation ===
This report is read-only and does not prove that MPD can play an HTTP URI.
A target-device playback test is still required. Do not test against the
production MPD queue unless a non-destructive isolated test instance is used.
Do not share secrets, MPD passwords, bridge tokens, or private URLs.
EOF
