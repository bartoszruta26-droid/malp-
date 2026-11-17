# Instalacja Yoinks-MPD na Raspberry Pi

## Wymagania

- Raspberry Pi OS / Debian z aktywnym systemd i APT.
- Architektura ARM64 (`aarch64`) albo ARMv7 (`armv7l`). Instalator obsługuje również `x86_64` do testów.
- Dostęp do Internetu podczas instalacji.
- Checkout repozytorium na gałęzi `feature/youtube-audio-bridge`.

## Instalacja

Z katalogu głównego repozytorium:

```bash
sudo bash scripts/yoinks-mpd/install.sh
```

Instalator:

1. Instaluje pakiety wymagane do kompilacji.
2. Buduje usługę C++ w trybie Release z włączonymi testami.
3. Uruchamia pełny CTest; przy niepowodzeniu przerywa instalację.
4. Pobiera oficjalny plik wykonywalny yt-dlp i weryfikuje jego SHA-256 względem upstreamowego `SHA2-256SUMS`.
5. Tworzy nieuprzywilejowane konto systemowe `yoinks-mpd`.
6. Generuje token API o wysokiej entropii, jeśli plik konfiguracji jeszcze nie istnieje.
7. Instaluje binaria i ograniczoną jednostkę `systemd`.
8. Włącza usługę i domyślnie ją uruchamia.

Opcje:

- `--no-start` — instaluje i włącza jednostkę, ale nie uruchamia jej w tej operacji.
- `--restart` — jawnie restartuje już działającą usługę; może przerwać aktualny strumień. Bez tej opcji ponowna instalacja pozostawia aktywną usługę bez restartu.
- `--check-youtube` — wykonuje dodatkowy test pobrania samych metadanych jednego filmu; nie pobiera audio.
- `--help` — wyświetla pomoc.

## Token API

Token jest przechowywany w:

`/etc/yoinks-mpd/yoinks-mpd.env`

Plik ma uprawnienia `0600` i właściciela `root:root`. Odczyt:

```bash
sudo cat /etc/yoinks-mpd/yoinks-mpd.env
```

Wprowadź wartość `YOINKS_MPD_TOKEN` do konfiguracji klienta MALP. Nie publikuj tokenu, nie umieszczaj go w zgłoszeniach ani logach. Ponowna instalacja zachowuje istniejący poprawny token.

## Kontrola usługi

```bash
systemctl status yoinks-mpd.service
journalctl -u yoinks-mpd.service -n 100 --no-pager
curl -fsS http://127.0.0.1:8788/api/v1/health
```

Domyślne parametry:

- API: `127.0.0.1:8788`
- MPD: `127.0.0.1:6600`
- yt-dlp: `/usr/local/bin/yt-dlp`

Jednostka systemd uruchamia proces jako użytkownik `yoinks-mpd`, bez uprawnień roota. Ogranicza pamięć do 256 MiB, liczbę zadań do 32, dostęp do systemu plików i rodziny gniazd. Logi trafiają do journald.


## Rzeczywisty test YouTube na odizolowanym MPD

Po zainstalowaniu Yoinks-MPD, yt-dlp, MPD z wejściem CURL oraz klienta `mpc`, można wykonać jawnie uruchamiany test rzeczywistego źródła:

```bash
bash scripts/yoinks-mpd/live-smoke-test.sh 'https://www.youtube.com/watch?v=ID_FILMU'
```

Skrypt:
- uruchamia osobny proces MPD na wolnym porcie loopback, z wyjściem `null`;
- uruchamia osobny proces Yoinks-MPD z tymczasowym tokenem i wskazaniem wyłącznie tego testowego MPD;
- używa zainstalowanego yt-dlp do rozpoznania wskazanego filmu i pobrania adresu CDN;
- żąda od MPD odtworzenia lokalnego adresu proxy i sprawdza stan `playing`;
- sprawdza, że kolejka zawiera URI lokalnego proxy, a nie adres YouTube/CDN;
- usuwa tymczasową konfigurację, bazę, stan i logi po zakończeniu.

Test nie łączy się z produkcyjnym MPD na porcie 6600, nie dotyka jego kolejki, nie wysyła dźwięku na głośniki (wyjście `null`) i nie pobiera plików audio. Resolver używa `--skip-download` oraz `--no-cache-dir`. Skrypt wymaga zwykłego użytkownika, a nie roota. Test wymaga dostępu do Internetu i może się nie powieść z powodu ograniczeń YouTube, geolokalizacji, wieku, limitów lub zmian po stronie yt-dlp.

To test integracyjny, a nie test instalacyjny. Nie uruchamia się automatycznie i nie jest wykonywany na produkcyjnym urządzeniu bez jawnego wywołania.

## Ważne ograniczenia

- Instalator **nie instaluje, nie konfiguruje, nie restartuje ani nie wysyła poleceń do MPD**.
- Nie czyści ani nie modyfikuje istniejącej kolejki MPD. Aplikacja używa protokołu MPD wyłącznie do operacji kolejki wynikających z żądania użytkownika.
- Nie konfiguruje TLS ani dostępu z sieci. Serwer API pozostaje związany z loopback. Zdalny dostęp wymaga osobno skonfigurowanego i przetestowanego TLS reverse proxy.
- Nie potwierdza, że konkretna kompilacja MPD obsługuje wymagane zachowanie HTTP/CURL. Nie potwierdza też rzeczywistego odtwarzania.
- Endpoint strumienia nie obsługuje obecnie HEAD ani Range. Zgodność z konkretnym MPD należy zweryfikować w odizolowanym teście.
- Audio jest przesyłane przez HTTP proxy i przechowywane wyłącznie w ograniczonych buforach RAM. Nie używa FIFO, potoków nazwanych, plików audio ani plikowego cache.
- Zewnętrzny klient Android nie połączy się bezpośrednio z adresem loopback Raspberry Pi. Do dostępu zdalnego potrzebny jest odrębny, zabezpieczony kanał TLS.

Instalacja usługi nie oznacza gotowości produkcyjnej. Przed użyciem zdalnym należy przeprowadzić test MPD/CURL na docelowym urządzeniu, test TLS oraz test odtwarzania end-to-end.
