#!/usr/bin/env bash
# One-shot verification, build, test and install entry point for Raspberry Pi.
# Installs the Yoinks-MPD bootstrap binary and yt-dlp resolver only.
# It NEVER installs, edits, restarts or sends commands to MPD.
# Media remains HTTP-proxy-only; this script does not download media.
set -Eeuo pipefail
umask 077

readonly RELEASE="https://github.com/yt-dlp/yt-dlp/releases/latest/download"
readonly BIN_DIR="/usr/local/bin"
readonly YOINKS_BIN="${BIN_DIR}/yoinks-mpd"
readonly YTDLP_BIN="${BIN_DIR}/yt-dlp"
readonly TEST_VIDEO_URL="https://www.youtube.com/watch?v=qqAYW7uxErI"
readonly TEST_VIDEO_ID="qqAYW7uxErI"

fail() { printf 'yoinks-mpd-install: ERROR: %s\n' "$*" >&2; exit 1; }
info() { printf 'yoinks-mpd-install: %s\n' "$*" >&2; }
have() { command -v "$1" >/dev/null 2>&1; }

[[ ${EUID} -eq 0 ]] || fail "uruchom przez sudo: sudo bash scripts/yoinks-mpd/install.sh"
have apt-get || fail "wymagany jest Debian/Raspberry Pi OS z APT"
have sha256sum || fail "brak sha256sum"

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd -P)"
SOURCE_DIR="${REPO_ROOT}/yoinks-mpd"
[[ -f "${SOURCE_DIR}/CMakeLists.txt" ]] || fail "nie znaleziono yoinks-mpd/CMakeLists.txt; uruchom skrypt z checkoutu repozytorium"

case "$(uname -m)" in
  aarch64|arm64) asset="yt-dlp_linux_aarch64"; archive=0 ;;
  armv7l|armv7*) asset="yt-dlp_linux_armv7l.zip"; archive=1 ;;
  x86_64|amd64) asset="yt-dlp_linux"; archive=0 ;;
  *) fail "nieobsługiwana architektura CPU: $(uname -m); obsługiwane: aarch64, armv7l, x86_64" ;;
esac

tmp="$(mktemp -d "${TMPDIR:-/var/tmp}/yoinks-mpd-install.XXXXXXXX")"
cleanup() { rm -rf -- "${tmp}"; }
trap cleanup EXIT INT TERM HUP

info "krok 1/7: instalacja narzędzi kompilacji i bibliotek"
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
  ca-certificates curl unzip cmake g++ make pkg-config \
  libboost-dev nlohmann-json3-dev libcurl4-openssl-dev

for tool in cmake c++ ctest curl unzip install; do
  have "$tool" || fail "brak wymaganego narzędzia po instalacji pakietów: $tool"
done

info "krok 2/7: konfiguracja CMake (C++17, Release, testy włączone)"
cmake -S "${SOURCE_DIR}" -B "${tmp}/build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DYOINKS_MPD_BUILD_TESTS=ON

info "krok 3/7: kompilacja z -Wall -Wextra -Wpedantic -Werror"
cmake --build "${tmp}/build" --parallel 2
[[ -x "${tmp}/build/yoinks-mpd" ]] || fail "kompilacja nie utworzyła pliku yoinks-mpd"

info "krok 4/7: uruchomienie pełnego zestawu CTest"
ctest --test-dir "${tmp}/build" --output-on-failure
info "wszystkie testy CTest zakończyły się powodzeniem"

info "krok 5/7: pobranie oficjalnego yt-dlp i kontrola SHA-256"
curl --fail --location --silent --show-error "${RELEASE}/${asset}" -o "${tmp}/${asset}"
curl --fail --location --silent --show-error "${RELEASE}/SHA2-256SUMS" -o "${tmp}/SHA2-256SUMS"
expected="$(awk -v n="${asset}" '$2 == n || $2 == "*"n {print $1; exit}' "${tmp}/SHA2-256SUMS")"
[[ -n "${expected}" ]] || fail "brak wpisu SHA-256 dla ${asset} w upstreamowym SHA2-256SUMS"
actual="$(sha256sum "${tmp}/${asset}" | awk '{print $1}')"
[[ "${actual}" == "${expected}" ]] || fail "weryfikacja SHA-256 yt-dlp nie powiodła się"

if (( archive )); then
  unzip -q "${tmp}/${asset}" -d "${tmp}/yt-dlp-unpacked"
  candidate="${tmp}/yt-dlp-unpacked/yt-dlp"
  if [[ ! -f "${candidate}" ]]; then
    candidate="$(find "${tmp}/yt-dlp-unpacked" -type f -name yt-dlp -print -quit)"
  fi
else
  candidate="${tmp}/${asset}"
fi
[[ -n "${candidate:-}" && -f "${candidate}" ]] || fail "nie znaleziono wykonywalnego pliku yt-dlp"
chmod 0755 "${candidate}"
"${candidate}" --version >/dev/null || fail "kontrola wersji yt-dlp nie powiodła się"

info "krok 6/7: test online resolvera YouTube (wyłącznie metadane)"
resolved_id="$("${candidate}" \
  --ignore-config \
  --no-cache-dir \
  --no-playlist \
  --skip-download \
  --no-progress \
  --no-warnings \
  --socket-timeout 20 \
  --retries 1 \
  --extractor-retries 1 \
  --print '%(id)s' \
  "${TEST_VIDEO_URL}" 2>/dev/null)" || fail "yt-dlp nie zdołał odczytać metadanych filmu testowego; sprawdź łączność i dostępność filmu"
[[ "${resolved_id}" == "${TEST_VIDEO_ID}" ]] || fail "resolver zwrócił inny identyfikator niż oczekiwany film testowy"
info "resolver rozpoznał oczekiwany film: ${TEST_VIDEO_ID}; pobieranie audio było wyłączone"

info "krok 7/7: instalacja zweryfikowanych plików wykonywalnych"
install -d -m 0755 "${BIN_DIR}"
install -m 0755 "${tmp}/build/yoinks-mpd" "${BIN_DIR}/.yoinks-mpd.new"
install -m 0755 "${candidate}" "${BIN_DIR}/.yt-dlp.new"
mv -f -- "${BIN_DIR}/.yoinks-mpd.new" "${YOINKS_BIN}"
mv -f -- "${BIN_DIR}/.yt-dlp.new" "${YTDLP_BIN}"

"${YTDLP_BIN}" --version >/dev/null || fail "zainstalowany yt-dlp nie przechodzi kontroli uruchomienia"
[[ -x "${YOINKS_BIN}" ]] || fail "zainstalowany yoinks-mpd nie jest wykonywalny"

info "WYNIK: kompilacja, testy, instalacja i test metadanych zakończone powodzeniem"
info "yoinks-mpd: ${YOINKS_BIN}"
info "yt-dlp: ${YTDLP_BIN} (resolver metadanych/adresu źródłowego; bez pobierania mediów)"
if have mpd; then
  info "wykryto MPD: $(mpd --version 2>/dev/null | head -n 1)"
  if have systemctl && systemctl is-active --quiet mpd; then
    info "usługa MPD jest aktywna; pozostawiono ją bez zmian"
  elif have pgrep && pgrep -x mpd >/dev/null 2>&1; then
    info "proces MPD działa; pozostawiono go bez zmian"
  else
    info "MPD jest zainstalowany, ale stan aktywności usługi nie został potwierdzony"
  fi
else
  info "nie wykryto polecenia mpd; nie instalowano MPD"
fi

info "UWAGA: test online potwierdza wyłącznie rozpoznanie filmu. To wydanie Yoinks-MPD jest nadal szkieletem API; nie ma gotowego endpointu strumienia HTTP ani integracji kolejki MPD."
info "Skrypt nie tworzy ani nie uruchamia usługi systemd, nie zmienia konfiguracji MPD, nie czyści kolejki i nie zapisuje mediów na dysku."
info "Transport mediów pozostaje wyłącznie HTTP proxy; żadne FIFO, potoki nazwane ani pliki audio nie są używane."
