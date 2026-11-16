# Instalacja Yoinks-MPD na Raspberry Pi

Status: instalator zależności i producenta; odtwarzanie przez MPD pozostaje niezweryfikowane.

## Zakres

Instalator obsługuje Raspberry Pi OS / Debian z APT i architektury aarch64, armv7l oraz x86_64. Pobiera oficjalny plik yt-dlp dla danej architektury, sprawdza jego SHA-256 względem upstreamowego pliku SHA2-256SUMS, instaluje go jako /usr/local/bin/yt-dlp i kopiuje producenta FIFO do /usr/local/libexec/yoinks-mpd/.

Nie instaluje interpretera ani pakietów Python. Używa samodzielnych wydań yt-dlp. Nie instaluje, nie zatrzymuje, nie restartuje i nie konfiguruje MPD. Nie zmienia kolejki, profili MALP ani ustawień audio. Nie tworzy usługi systemd ani portu sieciowego.

## 1. Sprawdź urządzenie

W terminalu Raspberry Pi lub przez SSH:

```bash
uname -m
cat /etc/os-release
mpd --version | head -n 2
command -v mpc || true
```

Typowe wyniki: aarch64 dla systemu 64-bitowego i armv7l dla 32-bitowego ARMv7. Architektura armv6l nie jest obsługiwana przez ten instalator.

## 2. Pobierz gałąź projektu

Jeżeli repozytorium nie jest jeszcze sklonowane:

```bash
sudo apt-get update
sudo apt-get install -y git
git clone --branch feature/youtube-audio-bridge --single-branch https://github.com/bartoszruta26-droid/malp-.git
cd malp-
```

Jeżeli repozytorium już istnieje:

```bash
cd /ścieżka/do/malp-
git fetch origin
git switch feature/youtube-audio-bridge
git pull --ff-only origin feature/youtube-audio-bridge
```

## 3. Uruchom instalator

Z katalogu głównego repozytorium:

```bash
sudo bash scripts/yoinks-mpd/install.sh
```

Instalator wymaga połączenia z Internetem. Pobiera aktualne wydanie yt-dlp z oficjalnego repozytorium, weryfikuje sumę kontrolną, a następnie sprawdza wersję programu i składnię skryptu producenta.

## 4. Zweryfikuj instalację

```bash
yt-dlp --version
sudo bash -n /usr/local/libexec/yoinks-mpd/yt-dlp-to-fifo.sh
ls -l /usr/local/bin/yt-dlp /usr/local/libexec/yoinks-mpd/yt-dlp-to-fifo.sh
```

## 5. Zapisz informacje o MPD

```bash
mpd --version
mpc version
mpc status
```

Zachowaj wyniki i przekaż je do dalszego etapu integracji. Na tym etapie nie wykonuj mpc clear, mpc add, mpc play ani zmian w mpd.conf.

## 6. Ważne: test FIFO

Obecny skrypt yt-dlp-to-fifo.sh obsługuje pojedynczy film i czeka, aż inny proces otworzy FIFO do odczytu. Nie uruchamiaj go samodzielnie bez czytnika, ponieważ będzie oczekiwał na odbiorcę.

Samo utworzenie FIFO i przesłanie przez nie bajtów nie dowodzi, że MPD może otworzyć FIFO jako źródło lokalnego pliku. Udokumentowane wtyczki pipe i fifo MPD są wyjściami PCM. Zgodność wejścia trzeba sprawdzić na dokładnej wersji MPD zainstalowanej na Raspberry Pi.

## 7. Bramka przed uruchomieniem odtwarzania

Przed podłączeniem MALP i kolejki MPD trzeba potwierdzić:
1. czy lokalny input MPD potrafi otworzyć FIFO;
2. czy dekoder MPD obsługuje kodek wybrany przez yt-dlp;
3. czy poprawnie działają EOF, stop, skip i anulowanie procesu;
4. czy producent kończy pracę i usuwa wyłącznie własne FIFO;
5. czy zwykłe sterowanie MPD przez MALP pozostaje bez zmian.

Jeżeli FIFO nie działa jako wejście, nie wdrażamy fikcyjnego mpc add /ścieżka/do/fifo. Dalsze warianty to natywny moduł wejściowy MPD w C++ albo lokalny strumień HTTP obsługiwany przez wtyczkę CURL MPD. Zmiana architektury na HTTP wymaga osobnej decyzji.

## 8. Bezpieczeństwo

- Nie wystawiaj MPD bezpośrednio do Internetu.
- Nie umieszczaj hasła MPD w adresie YouTube ani w poleceniach yt-dlp.
- Nie zmieniaj mpd.conf w ramach instalacji zależności.
- Nie czyść kolejki MPD podczas testów.
- Nie zapisuj pełnych plików audio; docelowy przepływ ma być strumieniowy.
- Instalator nie uruchamia usługi sieciowej i nie dodaje kont uprzywilejowanych.

## 9. Odinstalowanie

Usunięcie plików dodanych przez instalator:

```bash
sudo rm -f /usr/local/bin/yt-dlp
sudo rm -rf /usr/local/libexec/yoinks-mpd
```

Nie odinstalowuje to pakietów systemowych ani nie zmienia konfiguracji MPD.
