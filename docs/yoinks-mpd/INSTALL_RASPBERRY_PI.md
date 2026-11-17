# Instalacja Yoinks-MPD na Raspberry Pi

Status: **instalator buduje i wdraża usługę C++ HTTP proxy jako ograniczoną jednostkę systemd oraz instaluje yt-dlp. Nie modyfikuje działającego MPD. Wdrożenie nie oznacza potwierdzonego odtwarzania.**

## 1. Zakres i wymagania

Obsługiwane architektury: aarch64, armv7l i x86_64. Wymagane: Raspberry Pi OS / Debian z APT i aktywnym systemd oraz dostęp do Internetu podczas instalacji.

Instalator `scripts/yoinks-mpd/install.sh`:
- instaluje zależności kompilacji, buduje Yoinks-MPD w trybie Release i uruchamia cały zestaw CTest;
- pobiera oficjalny plik yt-dlp dla architektury i sprawdza SHA-256 względem upstreamowego `SHA2-256SUMS`;
- tworzy nieuprzywilejowane konto systemowe `yoinks-mpd`;
- generuje token Bearer o wysokiej entropii lub zachowuje poprawny istniejący token;
- zapisuje konfigurację w `/etc/yoinks-mpd/yoinks-mpd.env` z prawami 0600;
- instaluje `/usr/local/bin/yoinks-mpd`, `/usr/local/bin/yt-dlp` i `yoinks-mpd.service`;
- wykonuje `systemctl daemon-reload` oraz `systemctl enable`; uruchamia usługę, jeśli nie działa.

Instalator **nie instaluje, nie edytuje, nie restartuje i nie wysyła poleceń do MPD**. Nie zmienia kolejki ani trybów repeat/random/single/consume. Nie pobiera audio do plików. Usługa używa ograniczonych buforów RAM.

## 2. Diagnostyka przed instalacją

Zapisz stan urządzenia:

```bash
uname -m
cat /etc/os-release
mpd --version
command -v mpc || true
bash scripts/yoinks-mpd/diagnose-mpd-http.sh
```

Diagnostyka zbiera wersję/build MPD, informacje o swap/zram i montowaniach. Nie zmienia konfiguracji ani kolejki MPD.

## 3. Pobranie kodu

```bash
git clone --branch feature/youtube-audio-bridge --single-branch https://github.com/bartoszruta26-droid/malp-.git
cd malp-
```

Dla istniejącego checkoutu:

```bash
git fetch origin
git switch feature/youtube-audio-bridge
git pull --ff-only origin feature/youtube-audio-bridge
```

## 4. Instalacja

Najpierw można sprawdzić składnię i pomoc:

```bash
bash -n scripts/yoinks-mpd/install.sh
sudo bash scripts/yoinks-mpd/install.sh --help
```

Instalacja standardowa:

```bash
sudo bash scripts/yoinks-mpd/install.sh
```

Opcje:
- `--no-start` — instaluje i włącza usługę, ale jej nie uruchamia;
- `--restart` — jawnie restartuje już działającą usługę; może przerwać aktywny strumień;
- `--check-youtube` — wykonuje dodatkowe sprawdzenie samych metadanych YouTube, bez pobierania audio;
- `--help` — wyświetla pomoc.

Ponowna instalacja zachowuje istniejący token i nie restartuje działającej usługi bez opcji `--restart`. Plik tokenu pozostaje własnością root z uprawnieniami 0600.

## 5. Konfiguracja Androida

Odczytaj token na Raspberry Pi i wprowadź go w ustawieniach MALP:

```bash
sudo cat /etc/yoinks-mpd/yoinks-mpd.env
```

Android wymaga adresu HTTPS. Serwer aplikacyjny Yoinks-MPD nasłuchuje domyślnie tylko na `127.0.0.1:8788`; nie wystawiaj tego portu bezpośrednio do LAN ani Internetu.

Przykładowa konfiguracja Nginx znajduje się w `deploy/nginx/yoinks-mpd-https.conf.example`. Przed użyciem trzeba wprowadzić właściwą nazwę hosta i ścieżki certyfikatu TLS, zweryfikować konfigurację oraz zabezpieczyć dostęp zdalny. Lokalny endpoint `/stream/` jest przeznaczony dla MPD i powinien pozostać niedostępny dla zdalnych klientów. Wyłączone są buforowanie proxy i pliki tymczasowe.

## 6. Kontrola usługi

```bash
systemctl status yoinks-mpd.service
curl -fsS http://127.0.0.1:8788/api/v1/health
journalctl -u yoinks-mpd.service
```

Health API rozdziela gotowość płaszczyzny sterowania od gotowości mediów. Nawet `control_ready=true` **nie potwierdza** kompatybilności MPD/CURL ani rzeczywistego odtwarzania. Pole `mpd_playback_verified` pozostaje fałszywe do czasu wykonania kontrolowanego testu na docelowym urządzeniu.

## 7. Obowiązkowa bramka MPD/CURL i odtwarzania

Przed testem YouTube należy użyć osobnej, odizolowanej instancji MPD i potwierdzić działanie wejścia HTTP/CURL na dokładnym buildzie zainstalowanym na Raspberry Pi. Zapisz rzeczywiste zachowanie GET, HEAD, Range, seek, ponownego połączenia, timeout, EOF i dekodowania kodeka.

Instrukcja: [MPD HTTP input diagnostic](MPD_HTTP_INPUT_DIAGNOSTIC.md).

Nie wykonuj testów w sposób, który modyfikuje produkcyjną kolejkę albo przerywa działające odtwarzanie. Nie używaj nazwanych potoków ani lokalnych plików audio jako transportu.

## 8. Wymóg RAM-only

- Audio jest przesyłane progresywnie przez HTTP proxy i przechowywane wyłącznie w ograniczonych buforach RAM.
- yt-dlp działa wyłącznie w trybie rozwiązywania metadanych i adresu źródłowego; cache yt-dlp jest wyłączony.
- Nie wolno tworzyć pełnych plików audio, plików segmentów ani trwałego cache.
- Zweryfikuj konfigurację swap: zram jest pamięcią RAM; swap na SSD/SD/USB może zapisywać dane na nośnik trwały.
- Dla formalnego potwierdzenia braku zapisów audio przeprowadź kontrolę otwarć/zapisów plików podczas testu i monitoruj pamięć przez długi utwór oraz playlistę.

## 9. Granice potwierdzenia

Udany build i CTest w GitHub Actions potwierdzają kompilację i testy automatyczne w środowisku CI. Nie są dowodem poprawnego działania konkretnej wersji MPD/CURL, wdrożenia TLS, bezpieczeństwa całej sieci ani odtwarzania end-to-end na Raspberry Pi. Wyniki z urządzenia należy dopisać do audytu jako testy użytkownika lub niezależnie zweryfikowane — zgodnie z rzeczywistym źródłem dowodu.
