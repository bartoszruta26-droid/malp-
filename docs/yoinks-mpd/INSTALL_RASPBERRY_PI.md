# Instalacja zależności Yoinks-MPD na Raspberry Pi

Status: **instalator przygotowuje wyłącznie yt-dlp jako narzędzie rozwiązywania metadanych i źródła. Nie instaluje jeszcze usługi HTTP proxy.**

## Zakres

Instalator obsługuje Raspberry Pi OS / Debian dla architektur aarch64, armv7l i x86_64. Pobiera oficjalny samodzielny plik yt-dlp, weryfikuje jego SHA-256 względem upstreamowego pliku SHA2-256SUMS i instaluje go jako `/usr/local/bin/yt-dlp`.

Nie instaluje, nie zatrzymuje, nie restartuje i nie konfiguruje MPD. Nie zmienia kolejki, profili MALP ani ustawień audio. Nie pobiera ani nie zapisuje plików audio. Nie tworzy jeszcze usługi HTTP proxy ani jednostki systemd.

## 1. Sprawdź urządzenie

```bash
uname -m
cat /etc/os-release
mpd --version
command -v mpc || true
```

## 2. Pobierz gałąź projektu

```bash
git clone --branch feature/youtube-audio-bridge --single-branch https://github.com/bartoszruta26-droid/malp-.git
cd malp-
```

Dla istniejącego repozytorium:

```bash
git fetch origin
git switch feature/youtube-audio-bridge
git pull --ff-only origin feature/youtube-audio-bridge
```

## 3. Zainstaluj yt-dlp

```bash
sudo bash scripts/yoinks-mpd/install.sh
yt-dlp --version
```

Instalator weryfikuje sumę SHA-256 przed instalacją. Tymczasowe pobranie samego programu instalacyjnego jest sprzątane po zakończeniu. Żadne audio nie jest pobierane.

## 4. Zbierz informacje o MPD

```bash
bash scripts/yoinks-mpd/diagnose-mpd-http.sh
```

Skrypt diagnostyczny nie łączy się z produkcyjną kolejką, nie zmienia konfiguracji i nie uruchamia odtwarzania. Zbiera informacje o wersji MPD, możliwościach kompilacji, swap, zram i montowaniach.

## 5. Bramka zgodności HTTP

Przed implementacją i uruchomieniem proxy trzeba potwierdzić na dokładnym Raspberry Pi, że MPD odtwarza znany, krótki strumień HTTP na osobnej, odizolowanej instancji MPD. Należy zapisać rzeczywiste zachowanie GET, HEAD, Range, seek, reconnect, EOF oraz obsługę kodeka.

Instrukcja: [MPD HTTP input diagnostic](MPD_HTTP_INPUT_DIAGNOSTIC.md).

Nie wykonuj testów na produkcyjnej kolejce. Nie używaj nazwanych potoków ani lokalnych plików audio jako transportu.

## 6. Ograniczenia

- Usługa HTTP proxy nie jest jeszcze zaimplementowana ani instalowana.
- Samo zainstalowanie yt-dlp nie oznacza, że integracja działa.
- Nie zapisuj plików audio ani cache na nośniku trwałym.
- Docelowe bufory audio muszą być ograniczone i przechowywane wyłącznie w RAM.
- Nie zmieniaj mpd.conf ani istniejącej kolejki podczas diagnostyki.
