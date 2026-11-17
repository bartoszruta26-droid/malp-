#!/usr/bin/env bash
# Build, test and install Yoinks-MPD as a restricted systemd service.
# Does not install, edit, restart or send commands to MPD.
# Audio is relayed over HTTP and held only in bounded RAM buffers.
set -Eeuo pipefail
umask 077

readonly RELEASE="https://github.com/yt-dlp/yt-dlp/releases/latest/download"
readonly BIN_DIR="/usr/local/bin"
readonly ETC_DIR="/etc/yoinks-mpd"
readonly ENV_FILE="${ETC_DIR}/yoinks-mpd.env"
readonly UNIT_FILE="/etc/systemd/system/yoinks-mpd.service"
readonly SERVICE_USER="yoinks-mpd"
readonly SERVICE_GROUP="yoinks-mpd"

NO_START=0
RESTART_ACTIVE=0
CHECK_YOUTUBE=0
for arg in "$@"; do
  case "$arg" in
    --no-start) NO_START=1 ;;
    --restart) RESTART_ACTIVE=1 ;;
    --check-youtube) CHECK_YOUTUBE=1 ;;
    -h|--help)
      cat <<'USAGE'
Użycie: sudo bash scripts/yoinks-mpd/install.sh [opcje]

  --no-start       Zainstaluj i włącz jednostkę systemd, ale jej nie uruchamiaj.
  --restart        Zrestartuj działającą usługę (może przerwać bieżący strumień).
  --check-youtube  Wykonaj dodatkowy test pobrania samych metadanych YouTube.
  -h, --help       Pokaż tę pomoc.

Instalator kompiluje program, uruchamia CTest, weryfikuje SHA-256 yt-dlp,
tworzy konto systemowe, zapisuje token i instaluje usługę systemd.
Nie instaluje ani nie modyfikuje MPD, nie zmienia kolejki i nie pobiera audio.
USAGE
      exit 0
      ;;
    *) printf 'Nieznana opcja: %s\n' "$arg" >&2; exit 2 ;;
  esac
done

fail() { printf 'yoinks-mpd-install: BŁĄD: %s\n' "$*" >&2; exit 1; }
info() { printf 'yoinks-mpd: %s\n' "$*" >&2; }
have() { command -v "$1" >/dev/null 2>&1; }

[[ ${EUID} -eq 0 ]] || fail "uruchom przez sudo: sudo bash scripts/yoinks-mpd/install.sh"
have apt-get || fail "wymagany Debian/Raspberry Pi OS z APT"
have sha256sum || fail "brak sha256sum"
have systemctl || fail "brak systemctl; wymagany systemd"
[[ -d /run/systemd/system ]] || fail "systemd nie jest aktywnym menedżerem usług"

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd -P)"
SOURCE_DIR="${REPO_ROOT}/yoinks-mpd"
SERVICE_SOURCE="${SCRIPT_DIR}/yoinks-mpd.service"
[[ -f "${SOURCE_DIR}/CMakeLists.txt" ]] || fail "uruchom skrypt z checkoutu repozytorium"
[[ -f "${SERVICE_SOURCE}" ]] || fail "brak scripts/yoinks-mpd/yoinks-mpd.service"

case "$(uname -m)" in
  aarch64|arm64) asset="yt-dlp_linux_aarch64"; archive=0 ;;
  armv7l|armv7*) asset="yt-dlp_linux_armv7l.zip"; archive=1 ;;
  x86_64|amd64) asset="yt-dlp_linux"; archive=0 ;;
  *) fail "nieobsługiwana architektura CPU: $(uname -m)" ;;
esac

tmp="$(mktemp -d "${TMPDIR:-/var/tmp}/yoinks-mpd-install.XXXXXXXX")"
cleanup() { rm -rf -- "${tmp}"; }
trap cleanup EXIT INT TERM HUP

info "1/8: instalacja zależności kompilacji"
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
  ca-certificates curl unzip cmake g++ make pkg-config openssl \
  libboost-dev nlohmann-json3-dev libcurl4-openssl-dev

for tool in cmake c++ ctest curl unzip install openssl getent; do
  have "$tool" || fail "brak wymaganego narzędzia: $tool"
done

info "2/8: konfiguracja i kompilacja C++17"
cmake -S "${SOURCE_DIR}" -B "${tmp}/build" \
  -DCMAKE_BUILD_TYPE=Release -DYOINKS_MPD_BUILD_TESTS=ON
cmake --build "${tmp}/build" --parallel 2
[[ -x "${tmp}/build/yoinks-mpd" ]] || fail "brak pliku wynikowego yoinks-mpd"

info "3/8: pełny zestaw testów CTest"
ctest --test-dir "${tmp}/build" --output-on-failure
info "CTest: wszystkie testy zaliczone"

info "4/8: pobranie yt-dlp i weryfikacja sumy SHA-256"
curl --fail --location --silent --show-error "${RELEASE}/${asset}" -o "${tmp}/${asset}"
curl --fail --location --silent --show-error "${RELEASE}/SHA2-256SUMS" -o "${tmp}/SHA2-256SUMS"
expected="$(awk -v n="${asset}" '$2 == n || $2 == "*"n {print $1; exit}' "${tmp}/SHA2-256SUMS")"
[[ -n "${expected}" ]] || fail "brak SHA-256 dla ${asset} w oficjalnym pliku sum"
actual="$(sha256sum "${tmp}/${asset}" | awk '{print $1}')"
[[ "${actual}" == "${expected}" ]] || fail "niezgodna suma SHA-256 yt-dlp"

if (( archive )); then
  unzip -q "${tmp}/${asset}" -d "${tmp}/yt-dlp-unpacked"
  candidate="$(find "${tmp}/yt-dlp-unpacked" -type f -name yt-dlp -print -quit)"
else
  candidate="${tmp}/${asset}"
fi
[[ -n "${candidate:-}" && -f "${candidate}" ]] || fail "nie znaleziono pliku yt-dlp"
chmod 0755 "${candidate}"
"${candidate}" --version >/dev/null

if (( CHECK_YOUTUBE )); then
  info "opcjonalny test YouTube: tylko metadane, bez pobierania audio"
  "${candidate}" --ignore-config --no-cache-dir --no-playlist --skip-download \
    --no-progress --no-warnings --socket-timeout 20 --retries 1 \
    --extractor-retries 1 --print '%(id)s' \
    "https://www.youtube.com/watch?v=qqAYW7uxErI" >/dev/null \
    || fail "test metadanych YouTube nie powiódł się"
fi

info "5/8: przygotowanie dedykowanego konta systemowego"
if ! getent group "${SERVICE_GROUP}" >/dev/null; then
  groupadd --system "${SERVICE_GROUP}"
fi
if ! getent passwd "${SERVICE_USER}" >/dev/null; then
  useradd --system --gid "${SERVICE_GROUP}" --home-dir /nonexistent \
    --no-create-home --shell /usr/sbin/nologin "${SERVICE_USER}"
fi
install -d -o root -g root -m 0755 "${BIN_DIR}"
install -d -o root -g root -m 0750 "${ETC_DIR}"

info "6/8: bezpieczna konfiguracja tokenu"
if [[ ! -f "${ENV_FILE}" ]]; then
  token="$(openssl rand -base64 48 | tr -d '\n=' | tr '+/' '-_')"
  [[ "${#token}" -ge 43 && "${#token}" -le 128 ]] || fail "nie udało się wygenerować tokenu"
  printf 'YOINKS_MPD_TOKEN=%s\n' "${token}" > "${tmp}/yoinks-mpd.env"
  install -o root -g root -m 0600 "${tmp}/yoinks-mpd.env" "${ENV_FILE}"
  unset token
else
  token_count="$(grep -c '^YOINKS_MPD_TOKEN=' "${ENV_FILE}" || true)"
  [[ "${token_count}" == "1" ]] || fail "plik ${ENV_FILE} musi zawierać dokładnie jeden YOINKS_MPD_TOKEN"
  token="$(sed -n 's/^YOINKS_MPD_TOKEN=//p' "${ENV_FILE}")"
  [[ "${#token}" -ge 43 && "${#token}" -le 128 ]] \
    || fail "istniejący token ma nieprawidłową długość; zachowano plik konfiguracyjny"
  if [[ ! "${token}" =~ ^[A-Za-z0-9_-]+$ ]]; then
    fail "istniejący token musi być zapisany w formacie base64url"
  fi
  unset token
  chown root:root "${ENV_FILE}"
  chmod 0600 "${ENV_FILE}"
fi

info "7/8: instalacja binariów i jednostki systemd"
install -m 0755 "${tmp}/build/yoinks-mpd" "${BIN_DIR}/.yoinks-mpd.new"
install -m 0755 "${candidate}" "${BIN_DIR}/.yt-dlp.new"
mv -f -- "${BIN_DIR}/.yoinks-mpd.new" "${BIN_DIR}/yoinks-mpd"
mv -f -- "${BIN_DIR}/.yt-dlp.new" "${BIN_DIR}/yt-dlp"
install -o root -g root -m 0644 "${SERVICE_SOURCE}" "${tmp}/yoinks-mpd.service"
install -o root -g root -m 0644 "${tmp}/yoinks-mpd.service" "/etc/systemd/system/.yoinks-mpd.service.new"
mv -f -- "/etc/systemd/system/.yoinks-mpd.service.new" "${UNIT_FILE}"

# The service reads only its root-owned EnvironmentFile. The application itself
# runs without privileges and has no writable persistent media directory.
systemctl daemon-reload
systemctl enable yoinks-mpd.service

if (( NO_START )); then
  info "usługa została włączona, ale nie uruchomiona (--no-start)"
elif systemctl is-active --quiet yoinks-mpd.service; then
  if (( RESTART_ACTIVE )); then
    info "restart aktywnej usługi na jawne żądanie (--restart)"
    systemctl restart yoinks-mpd.service
  else
    info "usługa już działa; pozostawiono ją bez restartu, aby nie przerwać aktywnego strumienia"
  fi
else
  systemctl start yoinks-mpd.service
fi
if (( ! NO_START )); then
  systemctl is-active --quiet yoinks-mpd.service || {
    systemctl --no-pager --full status yoinks-mpd.service >&2 || true
    fail "usługa nie uruchomiła się poprawnie"
  }
fi

info "8/8: kontrola zainstalowanych plików"
"${BIN_DIR}/yt-dlp" --version >/dev/null
[[ -x "${BIN_DIR}/yoinks-mpd" ]] || fail "brak wykonywalnego yoinks-mpd"
[[ "$(stat -c '%a' "${ENV_FILE}")" == "600" ]] || fail "nieprawidłowe uprawnienia pliku tokenu"

if have mpd; then
  info "wykryto MPD ($(mpd --version 2>/dev/null | head -n 1)); pozostawiono bez zmian"
else
  info "MPD nie jest zainstalowany; instalator go nie doinstalowuje"
fi

cat <<'DONE'
Instalacja Yoinks-MPD zakończona.

Usługa:       yoinks-mpd.service
API lokalne:  http://127.0.0.1:8788
Konfiguracja: /etc/yoinks-mpd/yoinks-mpd.env (root:root, 0600)
Logi:         journalctl -u yoinks-mpd.service

Aby odczytać token do skonfigurowania klienta MALP:
  sudo cat /etc/yoinks-mpd/yoinks-mpd.env

Kontrola stanu:
  systemctl status yoinks-mpd.service
  curl -fsS http://127.0.0.1:8788/api/v1/health

WAŻNE:
- MPD nie został zainstalowany, skonfigurowany ani zrestartowany.
- Nie zmieniono kolejki ani trybów MPD.
- Nie wykonano testu odtwarzania i nie potwierdzono zgodności MPD/CURL.
- API nasłuchuje wyłącznie na loopback; zdalny dostęp wymaga osobno
  skonfigurowanego i sprawdzonego TLS reverse proxy.
- Audio jest przesyłane przez HTTP proxy i buforowane wyłącznie w RAM.
DONE
