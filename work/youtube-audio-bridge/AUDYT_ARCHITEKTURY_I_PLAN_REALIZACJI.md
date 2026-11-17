# Bieżący status audytu — nadrzędna aktualizacja 2026-11-17

> **Ta sekcja ma pierwszeństwo przed starszymi fragmentami audytu.** W dalszej części dokumentu zachowały się historyczne opisy stanu sprzed implementacji C++; nie należy ich odczytywać jako opisu aktualnej gałęzi.

**Repozytorium:** `bartoszruta26-droid/malp-`  
**Gałąź:** `feature/youtube-audio-bridge`  
**HEAD sprawdzony przy aktualizacji:** `26b628b91c20f499054a9df8cf69d28360989959`

## Stan potwierdzony w kodzie

1. Serwer C++ HTTP, resolver `yt-dlp`, `LazyStreamSessionRegistry`, `YoutubeQueueService` i `MpdQueueManager` są obecne i połączone w przepływie `POST /api/v1/queue` oraz `GET /stream/{opaque-id}`.
2. Enqueue tworzy wyłącznie capability w pamięci i stabilne lokalne URI. CDN URL jest rozwiązywany dopiero przy GET strumienia. Audio jest przesyłane progresywnie przez ograniczony bufor RAM; nie ma zapisu audio do plików ani plikowego cache.
3. Reconciliation kolejki korzysta z `playlistinfo`; operacje usługi są serializowane. Sesje nieobecne w kolejce są sprzątane z ochroną sesji aktywnych.
4. `enqueue` dopisuje pozycje. `play_now` wstawia nowy blok na początek kolejki przez odwrotne `addid URI 0`, zachowuje kolejność żądania i wywołuje `playid` dla pierwszej pozycji. Nie ma komendy `clear` ani zmiany trybów MPD.
5. HTTP API jest ograniczone do loopback. Klient Android wymaga HTTPS, wymaga tokenu Bearer i zapisuje token w postaci zaszyfrowanej kluczem chronionym przez Android Keystore. Przykładowa konfiguracja Nginx TLS jest dostarczona, ale nie wdrożona.
6. Klient Android komunikuje się z `POST /api/v1/queue`; nie przekazuje ani nie otrzymuje signed CDN URL. Odpowiedź zawiera `success`, `queued`, `started`, `playlist`, `rollback_complete`; klient weryfikuje sukces i zakres liczby pozycji.
7. Test HTTP wykorzystuje atrapę resolvera oraz niedostępny port MPD i sprawdza kontrolowany błąd 503. Osobne testy obejmują kolejkę MPD, kolejność `play_now`, reconciliation, lazy activation, anulowanie, limity i cykl życia buforów.

## Wyniki automatycznych testów

- C++ build + CTest: run `44063851359` — `success`; obejmuje poprawkę rezerwacji slotu RAM do zwolnienia bufora zakończonego strumienia.
- Android `assemble-debug`: run `44064159601` — `success`; poprawka walidacji tokenu podczas odszyfrowywania ustawień.
- Android `assemble-debug`: run `44064310295` — `success`; najnowszy sprawdzony commit gałęzi.
- Niektóre wcześniejsze przebiegi zakończyły się błędem podczas zmian pośrednich; nie są podstawą do deklaracji gotowości. Powyższe sukcesy dotyczą konkretnych SHA wskazanych przez GitHub Actions, a nie testu na urządzeniu.

## Otwarte bramki — bez fałszywej deklaracji gotowości

- **Raspberry Pi / MPD:** brak nowego raportu z urządzenia po integracji. Trzeba wykonać dostarczony, odczytowy `scripts/yoinks-mpd/diagnose-mpd-http.sh` i zachować wynik wersji MPD, listy input plugins, CURL, swap/zram i mountów.
- **MPD/CURL:** brak potwierdzenia, że docelowy build MPD obsługuje lokalny HTTP stream w wymagany sposób. Obecny endpoint nie obsługuje HEAD (405) ani Range (501); należy to sprawdzić z konkretną wersją MPD, bez naruszania produkcyjnej kolejki.
- **Test pozytywny end-to-end:** nie ma jeszcze testu HTTP 200 z kontrolowanym mock MPD przechodzącego cały flow API; obecny test HTTP dochodzi do MPD i weryfikuje fail-safe 503.
- **TLS wdrożeniowy:** przykład Nginx jest tylko konfiguracją referencyjną; brak wdrożonego certyfikatu, reverse proxy i testu Android → TLS proxy → loopback API.
- **Android:** kompilacja debug przechodzi, ale nie potwierdza testu na urządzeniu, odbioru ACTION_SEND/ACTION_SEND_MULTIPLE, UX, konfiguracji certyfikatu ani działania z rzeczywistym serwerem.
- **Eksploatacja:** instalator nie tworzy jeszcze kompletnej usługi systemd ani bezpiecznego wdrożenia Nginx; nie instalować ani restartować działającego MPD bez jawnej zgody.

**Wniosek:** implementacja serwerowa i klient Android są obecne w gałęzi, a wskazane przebiegi CI przechodzą. Projekt pozostaje w stanie *integracja kodowa — walidacja docelowego urządzenia i end-to-end otwarta*. Nie scalać do `main` i nie ogłaszać gotowości produkcyjnej.

---

# Profesjonalny audyt i plan realizacji: YouTube → MALP → Raspberry Pi → MPD

**Data audytu:** 2026-11-17  
**Repozytorium:** `bartoszruta26-droid/malp-`  
**Gałąź:** `feature/youtube-audio-bridge`  
**Katalog prac:** `work/youtube-audio-bridge/`  
**Status:** audyt dokumentacyjno-architektoniczny; implementacja usługi i odtwarzanie end-to-end pozostają do wykonania i weryfikacji.

---

## 1. Zasady audytu: prawda, sprawiedliwość, miłość i odpowiedzialność

Audyt stosuje cztery praktyczne zasady:

1. **Prawda** — oddzielamy stan potwierdzony kodem od deklaracji, planów i testów, których nie wykonano. Nie nazywamy projektu działającym odtwarzaczem.
2. **Sprawiedliwość** — nowa funkcja nie może odbierać użytkownikowi kontroli nad istniejącą kolejką MPD, profilami MALP, wyjściami audio, głośnością ani lokalną biblioteką.
3. **Miłość i troska** — domyślne zachowanie ma chronić dane, urządzenie, prywatność, bezpieczeństwo sieci i zasoby Raspberry Pi. Błędy mają być komunikowane jasno, bez uszkadzania bieżącego odtwarzania.
4. **Odpowiedzialność** — każda funkcja otrzymuje kryteria akceptacji, testy i dowód wykonania. Nie deklarujemy zgodności sprzętowej bez testu na docelowym urządzeniu.

To są zasady inżynierskie projektu, a nie deklaracja, że obecny kod spełnia już wszystkie wymagania.

## 2. Cel i granice projektu

Celem jest umożliwienie użytkownikowi udostępnienia w Androidzie linku do filmu lub playlisty YouTube, a następnie odtworzenia dźwięku przez istniejący serwer MPD na Raspberry Pi. MALP nadal ma być klientem sterującym MPD, a nie drugim niezależnym odtwarzaczem.

### Wymagania bezwzględne

- **RAM-only dla mediów:** żadne kompletne audio, fragmenty audio, segmenty, pliki tymczasowe ani kopie buforów nie mogą być zapisywane na SD, SSD, HDD, USB ani innym trwałym nośniku.
- Odtwarzanie strumieniowe w czasie rzeczywistym: bajty są przekazywane do MPD w miarę ich odbierania.
- Bufor jest ograniczony i znajduje się w pamięci RAM; wolniejszy odbiorca powoduje back-pressure, a nie wzrost pamięci bez granic.
- Raspberry Pi: nowa logika serwera i integracji w Bash/C++; bez usług, skryptów i zależności uruchomieniowych Python lub Node.js.
- Android: istniejąca aplikacja MALP w Javie.
- Zwykłe odtwarzanie MPD pozostaje nienaruszone.
- Playlista to wiele osobnych pozycji kolejki, w oryginalnej kolejności, a nie jeden sklejony plik/strumień.
- MPD otrzymuje stabilne lokalne URI; podpisane, krótkotrwałe adresy CDN YouTube nie trafiają do kolejki, konfiguracji ani logów.
- Nie wolno globalnie czyścić kolejki ani samoczynnie włączać trybów `single` lub `consume`.
- Nie wolno deklarować testu jako zaliczonego, jeśli nie istnieje jego wynik.

### Poza zakresem

- Zastąpienie MPD innym serwerem muzycznym.
- Przenoszenie całej biblioteki muzycznej do YouTube.
- Pobieranie filmów lub utworów do plików.
- Omijanie ograniczeń dostępu, płatności, DRM lub zabezpieczeń platformy.
- Automatyczne nagrywanie lub archiwizowanie strumieni.

Należy używać treści, do których użytkownik ma uprawniony dostęp, oraz respektować warunki korzystania z YouTube i prawa autorskie.

## Aktualizacja stanu po korekcie HTTP-only (2026-11-17)

Ta aktualizacja zastępuje historyczne opisy prototypu innym transportem. Jedyną zaakceptowaną ścieżką danych audio jest HTTP proxy w C++. Wcześniejszy skrypt producenta i jego testy zostały usunięte. Nie wolno przywracać FIFO, named pipes, plików audio, spoolingu ani cache plikowego.

Stan potwierdzony na aktualnej gałęzi:
- HEAD przed aktualizacją audytu: `44b70c944df36e6f28e916194a4876a45ad9e996`.
- `scripts/yoinks-mpd/install.sh` instaluje wyłącznie yt-dlp jako resolver; nie instaluje proxy ani nie modyfikuje MPD.
- `docs/yoinks-mpd/API_CONTRACT_V1.md` określa Control API, semantykę enqueue/play_now, błędy i kontrakt lokalnego endpointu mediów.
- W repozytorium nie znaleziono CMake, Meson ani Makefile dla usługi Yoinks-MPD; brak źródeł C++ proxy.
- Androidowy klient nadal używa `POST /api/v1/resolve` i oczekuje `stream_url`. Jest niezgodny z nowym kontraktem `POST /api/v1/queue`; wymaga przebudowy przed integracją.
- Nie ma dowodu kompilacji C++, testów serwera, testu MPD CURL na Raspberry Pi ani odtwarzania end-to-end.

Status etapów:
- W0 — ZAKOŃCZONY: dokumentacja główna i instalacyjna wskazuje HTTP-only; historyczny prototyp usunięty.
- W1 — OTWARTY: wymagany rzeczywisty raport z docelowego Raspberry Pi.
- W2 — SPECYFIKACJA UTWORZONA: kontrakt API v1 zapisany; implementacja i testy kontraktowe pozostają otwarte.
- W3–W9 — OTWARTE: implementacja, integracja, testy i wydanie nie zostały wykonane.

Nie traktować zapisu specyfikacji API jako implementacji. Nie scalać do gałęzi głównej przed przejściem bramek wydania.

## 3. Stan faktyczny repozytorium

Audyt wykonano na plikach gałęzi `feature/youtube-audio-bridge`. Poniższe obserwacje odnoszą się do pobranych wersji plików, a nie do uruchomienia aplikacji.

### 3.1 Istniejące elementy

- `scripts/yoinks-mpd/install.sh` — instalator APT pobierający samodzielny plik yt-dlp dla obsługiwanych architektur i weryfikujący SHA-256 względem upstreamowego pliku sum. Instalator nie pobiera mediów i nie instaluje usługi proxy.
- `scripts/yoinks-mpd/diagnose-mpd-http.sh` — diagnostyka odczytowa MPD, konfiguracji systemu, swap/zram i montowań.
- `docs/yoinks-mpd/README.md`, `docs/yoinks-mpd/INSTALL_RASPBERRY_PI.md` oraz `docs/yoinks-mpd/MPD_HTTP_INPUT_DIAGNOSTIC.md` — dokumentacja docelowej architektury HTTP i bramki zgodności MPD.
- `docs/yoinks-mpd/API_CONTRACT_V1.md` — kontrakt Control API v1 (specyfikacja, jeszcze bez serwera).
- `docs/YOUTUBE_AUDIO_BRIDGE_NEXT_STEPS.md` — porównanie rozwiązań upstream i późniejsza koncepcja lokalnego HTTP relay.
- `app/src/main/java/org/gateshipone/malp/application/youtube/YouTubeShareParser.java` — parser udostępnionych adresów.
- `app/src/main/java/org/gateshipone/malp/application/youtube/YouTubeAudioBridgeClient.java` — klient HTTP Androida.
- `app/src/main/java/org/gateshipone/malp/application/youtube/YouTubeBridgePreferences.java` — zapis adresu usługi i tokenu w domyślnych SharedPreferences.
- `MainActivity.java` — odbiera pojedynczy `ACTION_SEND`, pokazuje wybór enqueue/play-now i wykonuje żądanie w pojedynczym executorze.

### 3.2 Najważniejsze braki potwierdzone w kodzie

1. **Brak serwera Raspberry Pi zgodnego z API Androida.** Klient wysyła `POST /api/v1/resolve`; nie ma dostarczonej implementacji usługi HTTP ani C++ proxy.
2. **Brak wstawiania pozycji do MPD.** `MainActivity.resolveYouTubeShare()` otrzymuje listę pozycji, ale wyświetla komunikat, że wstawianie do kolejki nie jest dostępne. To nie jest działająca integracja odtwarzania.
3. **Brak potwierdzonej zgodności HTTP z MPD.** Nie przedstawiono jeszcze wyniku odtwarzania lokalnego strumienia HTTP przez CURL input na docelowym Raspberry Pi.
4. **Aktualizacja audytu wymagana.** Dokumentacja bieżąca przyjmuje HTTP proxy jako jedyny transport mediów. Niniejszy audyt zawierał historyczne opisy wcześniejszego prototypu, które zostały zastąpione w tej aktualizacji.
5. **Brak obsługi `ACTION_SEND_MULTIPLE`.** Manifest deklaruje wyłącznie `ACTION_SEND`; handler również obsługuje tylko pojedynczy tekst i pojedynczy link.
6. **Brak kompletnego kontraktu kolejki i anulowania.** Nie ma gotowego mechanizmu atomowego dodawania playlisty, usuwania pozycji należących do żądania, zatrzymywania producenta przy next/stop ani odzyskiwania po rozłączeniu.
7. **Bezpieczeństwo klienta wymaga poprawy.** Token jest opcjonalny, a adres usługi dopuszcza zwykłe HTTP. Odpowiedź akceptuje dowolne adresy HTTP(S) jako `stream_url`, bez sprawdzenia, czy host i port należą do skonfigurowanej usługi.
8. **Sekret w SharedPreferences.** Token jest zapisany w zwykłych preferencjach aplikacji; należy przenieść go do magazynu opartego na Android Keystore.
9. **Globalne `usesCleartextTraffic=true`.** Manifest dopuszcza nieszyfrowany ruch HTTP w całej aplikacji. Należy ograniczyć wyjątek do jawnie skonfigurowanego, zaufanego LAN albo wymagać HTTPS.
10. **Parser URL — częściowo poprawiony.** Usunięto powtórzony host, dodano walidację 11-znakowego identyfikatora filmu oraz identyfikatora playlisty, a ścieżki `/watch`, `/playlist`, `/shorts`, `/live`, `/embed` i `youtu.be` są sprawdzane dokładniej. Dodano testy jednostkowe przypadków poprawnych i błędnych; wykonanie testów Gradle pozostaje niepotwierdzone.
11. **Instalator nie instaluje usługi.** Pobranie yt-dlp nie oznacza, że powstał daemon, serwer API, proxy ani integracja MPD.
12. **Brak testów usługi i odtwarzania.** W repozytorium nie ma obecnie testów C++ proxy ani dowodu odtwarzania przez MPD.

### 3.3 Ocena stanu

| Obszar | Ocena | Uzasadnienie |
|---|---|---|
| Android share | Częściowy prototyp | Pojedynczy ACTION_SEND i dialog; brak pełnego przepływu |
| Parser YouTube | Utwardzony kodowo; testy nieuruchomione | Ścisła walidacja hosta, ścieżki i identyfikatorów; dodano przypadki testowe |
| Klient HTTP Android | Częściowy | Klient istnieje, lecz nie ma serwera po drugiej stronie |
| Ustawienia bridge | Częściowe | URL/token są zapisywane; brak bezpiecznego magazynu i walidacji |
| C++ HTTP proxy | Brak | Nie ma jeszcze implementacji serwera ani relay |
| Usługa Pi/API | Brak | Nie znaleziono implementacji zgodnej z klientem |
| Stabilny strumień HTTP | Brak | Nie ma lokalnego proxy ani odświeżania URL |
| MPD queue | Brak | Brak implementacji dodawania i zarządzania pozycjami |
| Playlista | Brak | Brak enumeracji i kolejki niezależnych pozycji |
| RAM-only assurance | Nieudowodnione | Nie ma testów śledzenia zapisów i polityki swap |
| Test Android/Gradle | Niezweryfikowany | W tym audycie nie wykonano kompilacji |
| Test sprzętowy | Niezweryfikowany | Brak niezależnego testu end-to-end |

**Wniosek:** obecny stan to częściowy prototyp Androida oraz dokumentacja architektury i kontraktu API. Nie istnieje jeszcze serwer C++, HTTP proxy ani działająca integracja z kolejką MPD.

## 4. Decyzja architektoniczna

### 4.1 Obowiązujący wariant: lokalny HTTP proxy w C++

Decyzja jest jednoznaczna: jedynym dozwolonym transportem mediów jest lokalny HTTP proxy w C++ do wejścia CURL MPD. Nie projektować ani nie wdrażać alternatywnego transportu mediów.

Powód: MPD ma udokumentowane wejście HTTP/CURL, natomiast FIFO nie jest udokumentowanym, uniwersalnym wejściem. Wariant HTTP pozwala umieścić stabilne URI w kolejce MPD, a proxy może odświeżać krótkotrwały adres CDN w pamięci.

Wcześniejszy prototyp i jego testy zostały usunięte z gałęzi. FIFO, named pipes, pliki audio i cache plikowe nie są częścią rozwiązania ani planu wdrożenia.

### 4.2 Przepływ docelowy

```text
Android MALP
  └─ Share YouTube URL
       └─ HTTPS /api/v1/queue + Bearer token
            Raspberry Pi: Yoinks-MPD C++ service
              ├─ walidacja żądania i limitów
              ├─ yt-dlp: metadane / lista pozycji (bez pobierania mediów)
              ├─ MPD protocol: addid / playid / kolejność
              └─ lokalny HTTP relay
                   ├─ stabilne URI /stream/<opaque-id>
                   ├─ RAM-only bounded buffer
                   ├─ back-pressure
                   └─ HTTPS CDN YouTube (URL tylko w RAM)
                         ↓
                    MPD CURL input
                         ↓
                    istniejące wyjście audio / DAC
```

### 4.3 Rozdzielenie płaszczyzny sterowania i mediów

Usługa powinna mieć dwa logicznie odrębne interfejsy:

- **Control API** — przyjmuje żądania z MALP, wymaga tokenu, zwraca identyfikator operacji i wynik dodania pozycji.
- **Media endpoint** — udostępnia wyłącznie strumienie dla MPD; nie udostępnia funkcji administracyjnych. Najlepiej nasłuchuje na loopback, np. `127.0.0.1`, ponieważ MPD i relay działają na tym samym Raspberry Pi.

Jeśli MPD działa w kontenerze lub innej przestrzeni sieciowej, loopback nie będzie wspólny. Wtedy należy zaprojektować ograniczoną sieć kontenerową lub konkretny interfejs LAN z regułami zapory. Nie wolno automatycznie wystawiać endpointu mediów na cały Internet.

### 4.4 Stabilne URI i pamięć

MPD otrzymuje np. `http://127.0.0.1:8787/stream/<opaque-id>`. Identyfikator jest losowy i nie ujawnia podpisanego adresu CDN.

W RAM usługa przechowuje:
- mapowanie identyfikatora na video ID i metadane;
- aktualny adres CDN i czas jego ważności;
- stan producenta/połączenia;
- ograniczony bufor danych;
- identyfikatory MPD queue item i operacji.

Adres CDN i tokeny nigdy nie trafiają do bazy MPD, plików konfiguracyjnych, historii ani logów. Po restarcie usługi mapowanie może zostać utracone; usługa powinna wtedy zwrócić kontrolowany błąd, a MALP umożliwić ponowne dodanie źródła. Nie należy utrwalać adresów CDN w celu odtworzenia mapowania.

## 5. Serwis Raspberry Pi — wymagania techniczne

### 5.1 Język i zależności

- Serwis i logika: C++17 lub nowszy, zależnie od toolchain Raspberry Pi OS.
- Bash: wyłącznie instalacja, kontrola usługi i testy pomocnicze.
- yt-dlp: samodzielny program wykonywalny jako zewnętrzny proces.
- MPD: istniejący demon, komunikacja przez protokół MPD.
- HTTP server: wybrać małą, utrzymywaną bibliotekę C/C++ po analizie licencji, dostępności pakietów i kosztu pamięci. Alternatywnie implementować minimalny serwer na sprawdzonym stosie systemowym, ale nie pisać własnej obsługi TLS.
- Nie dodawać Node.js, Pythona ani Dockera jako wymaganych usług.

Wybór biblioteki HTTP, biblioteki TLS i mechanizmu JSON musi być osobnym rekordem decyzji architektonicznej (ADR), z podaniem licencji, pakietu Debian/Raspberry Pi OS i sposobu aktualizacji.

### 5.2 Uruchamianie yt-dlp

Proces należy uruchamiać przez `fork/execve`, `posix_spawn` lub równoważne API z oddzielnymi argumentami. Zakaz `system()`, `popen()` z komendą składaną z danych użytkownika i interpolacji URL do powłoki.

Tryby:
- metadane pojedynczego filmu: JSON, bez pobierania audio;
- metadane playlisty: płaska enumeracja pozycji, bez pobierania audio;
- rozpoznanie wybranego formatu i URL strumienia: wyłącznie dane, bez zapisu pliku.

Wymagane jest jawne wyłączenie cache yt-dlp tam, gdzie ma zastosowanie. Nie używać pobierania do pliku, postprocesorów audio, zapisu segmentów, `--extract-audio` ani ścieżek wyjściowych mediów. Trzeba przetestować dokładne argumenty z aktualną wersją yt-dlp, bo interfejs i formaty YouTube mogą się zmieniać.

### 5.3 Strumieniowanie

Relay nie może najpierw pobrać całego utworu. Ma przekazywać dane progresywnie.

Wymagania:
- stały maksymalny rozmiar bufora RAM na strumień;
- limit łącznej pamięci dla wszystkich strumieni;
- back-pressure: wstrzymanie odczytu upstream, gdy bufor osiągnie limit;
- kontrolowane timeouty i limity szybkości;
- zamykanie połączenia i procesu po zakończeniu, anulowaniu lub błędzie;
- brak nieograniczonego buforowania w kolejce;
- poprawna obsługa HTTP status, EOF i przerwania klienta.

**Ważne:** samo przekazanie strumienia w kawałkach nie gwarantuje zgodności z MPD. Należy zbadać, czy MPD/CURL input wykonuje żądania HEAD, Range, ponowne połączenia lub seek. Jeśli seek ma działać, relay musi obsłużyć HTTP Range i mapowanie zakresów na źródło upstream. Jeżeli YouTube lub format danego materiału nie umożliwia wiarygodnego seek, funkcja powinna być jawnie oznaczona jako ograniczona, a nie pozorowana.

### 5.4 Odświeżanie adresu CDN

Adresy CDN są czasowe. Stabilny endpoint lokalny musi w razie wygaśnięcia:
1. wykryć błąd upstream;
2. ponownie rozwiązać źródło dla tego samego video ID;
3. sprawdzić, czy format i parametry nadal są zgodne;
4. wznowić transfer tylko wtedy, gdy można bezpiecznie ustalić pozycję bajtową i serwer upstream obsługuje odpowiedni Range;
5. w przeciwnym razie zakończyć strumień kontrolowanym błędem i przekazać MALP informację o konieczności ponownego uruchomienia utworu.

Nie wolno udawać, że można bezstratnie wznowić strumień po dowolnym błędzie sieciowym.

### 5.5 Procesy i zasoby

Każdy strumień ma własny kontekst życia:
- identyfikator żądania;
- PID procesu yt-dlp, jeśli uruchomiony;
- socket upstream i klienta MPD;
- bufor RAM;
- stan (resolving, ready, streaming, finished, failed, cancelled);
- limity czasu i bajtów;
- identyfikatory elementów MPD.

Zakończenie/skip/stop/disconnect musi zatrzymać tylko procesy i strumienie należące do odpowiedniego żądania. Należy unikać globalnego ubijania yt-dlp lub wszystkich połączeń.

## 6. Integracja z MPD i semantyka kolejki

### 6.1 Bramka kompatybilności

Przed implementacją kolejki należy na docelowym Raspberry Pi:
- zapisać `mpd --version`;
- ustalić, czy build ma wejście CURL;
- sprawdzić, czy MPD potrafi odtworzyć lokalny HTTP URI;
- zweryfikować obsługiwany kodek i kontener;
- sprawdzić, czy MPD wykonuje HEAD/Range i jak reaguje na odpowiedzi proxy;
- odnotować konfigurację sieci i uprawnienia.

Jeżeli MPD nie ma wejścia CURL, należy zatrzymać integrację i rozważyć aktualizację/rekompilację MPD. Nie wolno wracać do FIFO jako rzekomego wejścia bez dowodu.

### 6.2 Operacje kolejki

Usługa powinna łączyć się z MPD przez osobne połączenie protokołu MPD i używać poleceń z poprawnym escapowaniem argumentów. Nie należy wykonywać komend MPD przez shell.

- **enqueue:** dodaje nowe URI na końcu kolejki, nie zmienia aktualnie odtwarzanego utworu.
- **play_now:** dodaje pozycje żądania i uruchamia pierwszą pozycję jawnie wybraną przez użytkownika; nie czyści kolejki. Należy ustalić i przetestować, czy dalsze pozycje playlisty mają być umieszczone bezpośrednio za pierwszą pozycją, czy na końcu. Zalecenie: umieścić wszystkie pozycje nowego żądania w jednym spójnym bloku w kolejce, a pierwszą uruchomić przez jej ID.
- Błąd rozpoznania playlisty nie może częściowo zmodyfikować kolejki. Najpierw walidacja i limit pozycji, potem dodawanie.
- Zapamiętać MPD song IDs zwrócone przez `addid`, aby móc usunąć tylko pozycje należące do bridge.
- Nie zmieniać globalnych ustawień `single`, `consume`, `random` ani `repeat`.
- Nie usuwać istniejących lokalnych utworów.
- Obsłużyć MPD reconnect i konflikt zmian kolejki.

Najlepsza kolejność transakcyjna: rozwiązać i zweryfikować wszystkie metadane → przygotować listę lokalnych URI → dodać pozycje kontrolowaną sekwencją MPD → zwrócić potwierdzenie. Jeśli dodawanie przerwie się w połowie, usunąć wyłącznie dodane przez tę operację ID i zwrócić jednoznaczny błąd.

### 6.3 Sterowanie

MPD pozostaje właścicielem play/pause/volume/output/queue. MALP nadal steruje nim normalnym kanałem MPD. Bridge obserwuje zdarzenia MPD i sprząta własne zasoby, gdy pozycja przestaje być potrzebna.

Należy zweryfikować:
- pauza i wznowienie nie kończą niepotrzebnie źródła;
- next/stop zwalniają aktywny producent;
- seek jest obsługiwany tylko wtedy, gdy HTTP relay i upstream faktycznie go obsługują;
- odłączenie MALP nie kończy odtwarzania, jeśli MPD nadal odtwarza;
- zakończenie utworu zamyka tylko jego zasoby;
- zwykłe utwory lokalne nadal działają tak samo.

## 7. Android MALP — wymagane poprawki

### 7.1 Odbieranie udostępnienia

- Obsłużyć `ACTION_SEND` i świadomie zdecydować o obsłudze `ACTION_SEND_MULTIPLE`.
- Walidować MIME type, rozmiar tekstu, liczbę URL i host.
- Obsłużyć linki z dodatkowymi opisami tekstowymi, skrócone URL i przekierowania w sposób bezpieczny; nie wykonywać niekontrolowanego pobierania przekierowań na telefonie.
- Testować zimny start, działającą już aktywność, obrót ekranu i wielokrotne udostępnienie.
- Zachować istniejące zachowanie dla zwykłych adresów internetowych, które nie są YouTube.

### 7.2 Ustawienia i sekrety

- Pole adresu bridge powinno walidować scheme, host i port.
- Token jest wymagany; brak tokenu ma uniemożliwiać wysłanie żądania.
- Przechowywać token zaszyfrowany kluczem Android Keystore, nie w zwykłych SharedPreferences.
- Nie umieszczać tokenu w logach, komunikatach błędów ani analytics.
- Preferować HTTPS. Dla świadomie wybranego zaufanego LAN można udostępnić jawny wyjątek z ostrzeżeniem i ograniczonym zakresem; nie wyłączać globalnie kontroli TLS.
- Ograniczyć `usesCleartextTraffic=true`; zastosować Network Security Config albo usunąć globalne zezwolenie, jeśli nie jest potrzebne innym funkcjom.

### 7.3 Kontrakt API

Obecny klient ma endpoint `POST /api/v1/resolve`, ale nazwa i zachowanie nie odpowiadają faktycznej operacji użytkownika: klient nie tylko rozpoznaje źródło, lecz chce dodać pozycje do MPD.

Zalecany kontrakt:
- `GET /api/v1/health` — stan usługi, bez sekretów.
- `POST /api/v1/queue` — walidacja URL, tryb enqueue/play_now, limity, rozpoznanie i dodanie do MPD.
- `GET /api/v1/requests/{request_id}` — status długiego żądania, jeśli rozpoznanie trwa dłużej niż limit synchroniczny.
- `DELETE /api/v1/requests/{request_id}` — anulowanie żądania i jego nieodtwarzanych pozycji, z kontrolą własności.
- Odpowiedź zawiera `request_id`, liczbę pozycji, metadane i wynik operacji MPD; nie zwraca upstream CDN URL.

Klient musi weryfikować, że zwrócone lokalne URI wskazują skonfigurowany host/port bridge. W idealnym wariancie Android nie potrzebuje otrzymywać stream URL wcale — serwer sam dodaje URI do MPD i zwraca wyłącznie wynik operacji.

### 7.4 UX

Dialog powinien jasno rozróżniać:
- dodanie na koniec kolejki;
- odtworzenie teraz;
- anulowanie.

Po wykonaniu należy pokazać prawdziwy wynik: „dodano N pozycji”, „rozpoczęto odtwarzanie”, albo konkretny błąd. Nie pokazywać komunikatu sukcesu przed potwierdzeniem z usługi i MPD. Dla playlisty pokazać liczbę pozycji i ewentualne pominięte elementy.

## 8. Bezpieczeństwo i odporność

### 8.1 Walidacja wejścia

- HTTPS wyłącznie.
- Ścisła allowlista hostów YouTube, bez dopasowań typu `endsWith("youtube.com")` bez granicy domeny.
- Odrzucać userinfo, nietypowe porty, znaki sterujące, nieprawidłowe ścieżki i nadmiernie długie URL.
- Wyciągać video ID i playlist ID do osobnych, zweryfikowanych pól.
- Nie pozwalać użytkownikowi przekazać dowolnego URL strumienia do endpointu proxy (ochrona przed SSRF).
- Redirecty yt-dlp/CDN muszą być ograniczone do oczekiwanych domen i HTTPS.

### 8.2 Uwierzytelnianie i sieć

- Token Bearer o wysokiej entropii, wymagany na każdym endpointcie sterującym.
- Porównanie tokenu w czasie stałym.
- Limit błędnych prób i możliwość rotacji tokenu.
- Control API wystawione wyłącznie na interfejs LAN, zapora ogranicza źródła do zaufanej sieci.
- HTTPS dla sieci niezaufanej; nie wystawiać API do Internetu.
- Endpoint mediów bez uprawnień administracyjnych, najlepiej loopback-only.
- Nie używać hasła MPD jako tokenu bridge. Hasło MPD przechowywać tylko w konfiguracji usługi z prawami dostępu dla dedykowanego użytkownika.

### 8.3 Limity

- limit długości URL;
- limit długości i liczby pozycji playlisty;
- limit równoległych rozpoznań i strumieni;
- timeout yt-dlp i upstream;
- limit RAM na strumień i globalnie;
- limit czasu bezczynności;
- limit żądań na klienta;
- kontrolowane zachowanie przy braku pamięci i utracie Internetu.

### 8.4 Użytkownik systemowy i uprawnienia

Uruchamiać usługę jako dedykowany użytkownik systemowy, bez roota. Nadać mu wyłącznie:
- dostęp do lokalnego portu MPD;
- dostęp do pliku tokenu/konfiguracji;
- możliwość uruchomienia yt-dlp;
- dostęp do portu lokalnego relay.

Nie wymagać uprawnień do katalogu muzycznego MPD, jeśli usługa nie zapisuje plików.

## 9. Gwarancja RAM-only i ryzyko swap

To jest najważniejszy wymóg całego przedsięwzięcia.

### 9.1 Czego nie wolno zapisywać

- kompletnego audio;
- plików częściowych i segmentów;
- cache yt-dlp;
- plików postprocessingu;
- buforów HTTP w katalogach tymczasowych;
- podpisanych adresów CDN i tokenów;
- logów zawierających treść mediów lub pełne URL.

Metadane mogą być logowane w ograniczonym zakresie, bez sekretów i pełnych adresów. Nie przechowywać historii adresów CDN.

### 9.2 Swap

Samo użycie RAM w programie nie gwarantuje braku zapisu na nośnik. Linux może przenieść strony pamięci do swap na karcie SD lub SSD. Jeżeli wymóg jest literalny i bez wyjątków:
- należy wyłączyć disk-backed swap albo wykazać, że nie może zawierać stron procesu bridge;
- zram jest dopuszczalny, ponieważ kompresuje pamięć w RAM;
- nie wolno uznawać zwykłego pliku swap na SD/SSD za bezpieczny;
- odnotować zram, swap devices i politykę systemu w raporcie instalacji.

### 9.3 Testy dowodowe

1. Uruchomić usługę i MPD z obserwacją operacji plikowych (np. `strace` lub równoważny audyt systemowy).
2. Odrzucać otwarcia i zapisy plików audio/segmentów przez bridge/yt-dlp.
3. Sprawdzić, że cache yt-dlp jest wyłączony.
4. Monitorować RSS i pamięć buforów dla długiego utworu i playlisty.
5. Zasymulować wolnego odbiorcę; bufor ma osiągnąć limit i zatrzymać odczyt upstream.
6. Zatrzymać/ominąć utwór; PID, socket i bufor mają zostać zwolnione.
7. Wykonać wyszukiwanie plików na wszystkich trwałych mountpointach przed i po teście.
8. Udokumentować konfigurację swap/zram.
9. Powtórzyć test po restarcie usługi i po restarcie Raspberry Pi.

Test „nie znaleziono pliku w katalogu tymczasowym” nie wystarcza. Wymagane jest sprawdzenie wszystkich trwałych mountpointów i operacji zapisu procesu.

## 10. Kodeki, jakość i zgodność

- Preferować audio-only format możliwie zgodny z dekoderami konkretnego MPD.
- Nie zakładać, że każdy materiał ma ten sam kodek.
- Nie transkodować do FLAC tylko po to, by uzyskać większy plik; konwersja stratnego Opus/AAC/MP3 do FLAC nie odzyskuje utraconej informacji.
- Jeśli konieczny jest FFmpeg, uruchamiać go jako proces strumieniowy z wyjściem stdout i bez plików pośrednich; jednak domyślnie unikać transkodowania.
- Sprawdzić metadane, czas trwania i EOF. Nie każdy strumień YouTube zachowuje się jak lokalny plik seekowalny.
- Sprawdzić MPD z realnym kodekiem i realnym urządzeniem audio.

## 11. Testy i kryteria akceptacji

### A. Testy jednostkowe C++

- walidacja URL, video ID i playlist ID;
- parser JSON yt-dlp;
- limity rozmiaru odpowiedzi;
- mapowanie opaque ID na strumień;
- maszyna stanów żądania;
- limity pamięci i timeoutów;
- walidacja Range/HEAD/GET;
- sanitizacja logów.

### B. Testy usługi i bezpieczeństwa

- token brakujący/błędny/poprawny;
- próba SSRF przez URL obcy i przekierowania;
- zbyt długa playlista;
- nadmiar żądań i równoległych strumieni;
- błędy yt-dlp, timeout, brak sieci;
- przerwane połączenie MPD;
- rotacja/wygaśnięcie CDN;
- restart usługi.

### C. Testy MPD

- wejście HTTP/CURL w dokładnym buildzie;
- pojedynczy utwór;
- utwór z różnymi kodekami;
- HEAD/Range/seek;
- enqueue przy trwającym utworze lokalnym;
- play-now;
- playlista i kolejność;
- brak zmian trybów globalnych;
- stop/next/pause/resume;
- ponowne połączenie MPD.

### D. Testy MALP

- ACTION_SEND na zimnym starcie;
- ACTION_SEND, gdy MALP już działa;
- kilka kolejnych udostępnień;
- błędny URL i brak tokenu;
- timeout API i błąd usługi;
- duża playlista;
- rotacja ekranu / odtworzenie aktywności;
- istniejące funkcje MALP: lokalna biblioteka, profile, kolejka, głośność, wyjścia, widget, powiadomienia.

### E. Testy RAM-only

Wszystkie testy z sekcji 9.3 muszą przejść na Raspberry Pi z docelową konfiguracją systemu.

### F. Bramki wydania

Nie scalać do gałęzi głównej, dopóki:
- Gradle debug build i testy Androida nie przejdą;
- testy C++/Bash nie przejdą;
- MPD CURL input nie zostanie potwierdzony;
- pojedynczy utwór nie zostanie odtworzony na Raspberry Pi;
- playlista nie zostanie odtworzona w prawidłowej kolejności;
- lokalna kolejka i sterowanie MALP nie zostaną zweryfikowane;
- test RAM-only nie przejdzie;
- nie będzie dowodu poprawnego sprzątania procesów.

## 12. Etapy realizacji — kolejność prac

| Etap | Zakres | Rezultat wymagany do zamknięcia |
|---|---|---|
| W0 | Ujednolicenie dokumentacji i ADR | Zakończono: HTTP proxy jest jedynym transportem; historyczny prototyp i testy usunięto |
| W1 | Dowód MPD CURL input | Wynik z dokładnej wersji MPD na Raspberry Pi |
| W2 | Specyfikacja API i bezpieczeństwa | SPECYFIKACJA UTWORZONA: `docs/yoinks-mpd/API_CONTRACT_V1.md`; testy i implementacja pozostają otwarte |
| W3 | C++ service skeleton | Uruchamianie, konfiguracja, logowanie bez sekretów, systemd |
| W4 | Resolver yt-dlp | Metadane i playlisty bez pobierania audio |
| W5 | HTTP relay RAM-only | Stabilne URI, ograniczony bufor, back-pressure, anulowanie |
| W6 | MPD queue manager | enqueue/play_now, identyfikatory pozycji, rollback i cleanup |
| W7 | Android integration | Bezpieczne ustawienia, API client, share intents, wynik operacji |
| W8 | Testy i regresja | C++, Android, security, RAM-only, MPD i Pi |
| W9 | Dokumentacja instalacji i wydania | Instrukcja, audyt końcowy, release gate |

Każdy etap należy wykonać osobno, z przeglądem zmian i aktualizacją audytu. Nie rozpoczynać kolejnego etapu, jeśli poprzedni ma nierozwiązany błąd blokujący.

## 13. Decyzje architektoniczne wymagające zapisania (ADR)

1. **ADR-001: Transport MPD** — decyzja: wyłącznie lokalny HTTP proxy + MPD CURL input; bez alternatywnego transportu mediów.
2. **ADR-002: Serwer** — C++ na Raspberry Pi, bez Pythona/Node.
3. **ADR-003: Zarządzanie kolejką** — usługa dodaje pozycje protokołem MPD; nie zwraca Androidowi CDN URL.
4. **ADR-004: Sekrety** — token API wymagany, Android Keystore, HTTPS albo jawnie zatwierdzony wyjątek LAN.
5. **ADR-005: RAM-only** — brak trwałego buforowania, bounded RAM, zram zamiast disk swap.
6. **ADR-006: Playlisty** — osobne stream URI i osobne MPD queue IDs, oryginalna kolejność.
7. **ADR-007: Odzyskiwanie** — CDN URL tylko w RAM; po restarcie mapowania wygasają i wymagają ponownego resolve.

Każdy ADR powinien zawierać: kontekst, rozważane warianty, decyzję, konsekwencje, test potwierdzający.

## 14. Najważniejsze ryzyka i środki zaradcze

| Ryzyko | Skutek | Ograniczenie |
|---|---|---|
| Brak CURL w MPD | Brak odtwarzania HTTP | Bramka W1; nie zmienia decyzji o HTTP proxy, wymaga zgodnego builda MPD |
| YouTube zmienia ekstrakcję | Przestaje działać resolve | Aktualizacje yt-dlp i testy kontraktowe |
| URL CDN wygasa | Przerwanie utworu | Re-resolve i ostrożne wznowienie Range |
| MPD nie obsługuje Range | Seek/refresh ograniczone | Test dokładnego klienta MPD |
| Zapis przez swap | Naruszenie RAM-only | Wyłączyć disk swap, użyć zram, testować |
| Zbyt wiele strumieni | Brak RAM / niestabilność | Limity globalne i per stream |
| Token wycieknie | Nieautoryzowane sterowanie | Keystore, TLS, rotacja, brak logów |
| Częściowe dodanie playlisty | Bałagan w kolejce | Walidacja przed dodaniem i rollback po ID |
| Aktualizacja MALP zepsuje funkcje | Regresja aplikacji | Testy regresji i oddzielna gałąź |
| Rozbieżna dokumentacja | Błędna instalacja | Jeden dokument architektury jako źródło prawdy |

## 15. Zalecany plan pierwszego wdrożenia

Nie należy od razu pisać całego demona. Najpierw trzeba zamknąć najdroższe ryzyko techniczne:

1. Utrzymać HTTP proxy jako jedyny transport mediów i usunąć pozostałe historyczne opisy alternatywnych transportów.
2. Uzyskać z Raspberry Pi dokładny wynik `mpd --version`, `mpc version` i test wejścia HTTP z CURL.
3. Kontrakt Control API v1 zapisano w `docs/yoinks-mpd/API_CONTRACT_V1.md`; przed implementacją należy zweryfikować semantykę kolejki z rzeczywistym MPD.
4. Rozpocząć szkielet usługi C++ i testy jednostkowe niezależne od sprzętu; bramkę MPD CURL prowadzić równolegle, bez deklarowania zgodności do czasu testu.
5. Zbudować resolver metadanych bez pobierania mediów.
6. Dodać ograniczony bufor RAM i test back-pressure zanim podłączymy prawdziwy YouTube.
7. Podłączyć MPD queue manager, a następnie MALP.
8. Przeprowadzić testy RAM-only, regresję i testy sprzętowe.
9. Zaktualizować audyt wynikami i dopiero rozważyć scalanie.

## 16. Rejestr dowodów i ograniczeń tego audytu

### Potwierdzone przez inspekcję kodu

- Istnieje instalator yt-dlp jako resolvera; historyczny prototyp producenta i jego testy zostały usunięte.
- Istnieją klasy Android parsera, klienta HTTP i preferencji.
- MainActivity wykonuje żądanie HTTP w executorze, ale jawnie informuje, że dodawanie do kolejki nie jest jeszcze dostępne.
- Manifest obsługuje ACTION_SEND, nie ACTION_SEND_MULTIPLE.
- Dokumentacja bieżąca i kontrakt API opisują wyłącznie HTTP proxy; niniejsza sekcja jest aktualizacją stanu po korekcie architektury.
- Nie znaleziono serwera Pi implementującego endpoint Androida ani implementacji MPD queue managera w sprawdzonych plikach.

### Zgłoszone wcześniej, ale nieodtworzone niezależnie

Historyczne wyniki testów dotyczyły wyłącznie usuniętego prototypu i nie stanowią dowodu dla obecnej architektury HTTP proxy. Nie są zaliczane jako testy aktualnej gałęzi.

### Nie wykonano w ramach tego audytu

- kompilacji Gradle;
- testów Androida na emulatorze/urządzeniu;
- kompilacji C++ (serwis C++ jeszcze nie istnieje);
- testu CURL input na docelowym Raspberry Pi;
- odtwarzania pojedynczego utworu i playlisty;
- pomiarów pamięci i testu swap;
- testów śledzenia zapisów na trwałe nośniki;
- niezależnego testu bezpieczeństwa sieciowego.

**Werdykt końcowy:** architektura jest wykonalna pod warunkiem potwierdzenia MPD CURL input i poprawnej implementacji relay. Obecny kod nie spełnia jeszcze celu użytkowego. Najrozsądniejsza droga to równoległe potwierdzenie zgodności MPD i budowa szkieletu C++ z testami, następnie resolver, HTTP proxy z ograniczonym buforem RAM, manager kolejki i pełna integracja Androida. Bezpieczeństwo RAM-only musi być mierzone na systemie docelowym, włącznie ze swap, a nie wywodzone wyłącznie z braku plików w katalogu roboczym.


## Aktualizacja wykonania etapów — 2026-11-17

### W0 — ujednolicenie dokumentacji

**Status: wykonano po stronie repozytorium.** Zaktualizowano główny README, dokument Yoinks-MPD, plan kolejnych prac, instrukcję instalacji Raspberry Pi oraz indeks obszaru roboczego. Jednoznacznie wskazano lokalny HTTP proxy w C++ i wejście CURL MPD jako jedyną architekturę docelową. Usunięto historyczny prototyp i testy.

### W1 — diagnostyka zgodności MPD/CURL

**Status: przygotowano narzędzia; bramka sprzętowa pozostaje otwarta.** Dodano:
- `scripts/yoinks-mpd/diagnose-mpd-http.sh` — skrypt tylko do odczytu, zbierający wersje MPD/MPC, wskazówki o możliwościach CURL, informacje o swap/zram i montowaniach;
- `docs/yoinks-mpd/MPD_HTTP_INPUT_DIAGNOSTIC.md` — procedurę bezpiecznego testu HTTP na odizolowanej instancji MPD, bez dotykania produkcyjnej kolejki.

Weryfikacja wykonana w środowisku roboczym: `bash -n` zakończyło się powodzeniem, a uruchomienie skryptu potwierdziło jego działanie. Środowisko robocze nie ma zainstalowanego MPD ani MPC, więc nie mogło potwierdzić wejścia CURL ani odtwarzania. **Nie wykonano testu na Raspberry Pi i nie deklaruje się zaliczenia W1.**

### Następny warunek blokujący

Przed implementacją C++ relay należy otrzymać z docelowego Raspberry Pi raport diagnostyczny oraz wynik odtwarzania krótkiego, znanego strumienia HTTP na osobnej instancji MPD. Do tego czasu nie należy zmieniać produkcyjnej kolejki MPD ani implementować założeń o obsłudze HEAD/Range/seek bez obserwacji rzeczywistego klienta.


## Korekta wymagań użytkownika — 2026-11-17

Użytkownik jednoznacznie potwierdził, że wymaga **HTTP proxy**, a nie FIFO. Niniejsza decyzja zastępuje wszystkie wcześniejsze warianty i opisy prototypu. Named pipes, lokalne pliki audio i buforowanie na dysku są wykluczone z projektu. Jedyną ścieżką danych jest: MALP → API sterujące → yt-dlp (metadane/adres źródła, bez pobierania mediów) → C++ HTTP proxy z ograniczonym buforem RAM → stabilny lokalny HTTP URI → MPD CURL input.

W ramach korekty usunięto z gałęzi skrypt producenta nazwanego potoku i jego testy. Instalator został zmieniony tak, aby instalował wyłącznie yt-dlp jako zależność resolvera; nie instaluje jeszcze proxy. Dokumentacja główna i instrukcja instalacji zostały zastąpione opisem HTTP-only. Implementacja C++ HTTP proxy pozostaje następnym zasadniczym etapem i nie jest jeszcze wykonana.


## Aktualizacja prac — kontrakt API i parser Android (2026-11-17)

### W2 — kontrakt Control API v1

**Status: SPECYFIKACJA UTWORZONA.** Dodano `docs/yoinks-mpd/API_CONTRACT_V1.md`. Dokument określa uwierzytelnianie Bearer, endpointy health/queue/request, format JSON, kody błędów, limity, semantykę enqueue/play_now, rollback MPD i kontrakt lokalnego endpointu HTTP dla MPD. Serwer nie jest jeszcze zaimplementowany; nie jest to działające API.

### Android — utwardzenie parsera URL

Zmieniono `YouTubeShareParser.java`:
- usunięto zduplikowany wpis hosta;
- dopuszczone są wyłącznie dokładne hosty z allowlisty HTTPS;
- identyfikator filmu musi mieć 11 znaków dozwolonego alfabetu;
- identyfikator playlisty musi mieć od 10 do 128 znaków;
- walidowane są konkretne ścieżki watch, playlist, shorts, live, embed i youtu.be;
- odrzucono niedozwolone poddomeny i dodatkowe segmenty ścieżki.

Rozszerzono `YouTubeShareParserTest.java` o przypadki nieprawidłowego identyfikatora filmu/playlisty, ścieżek Shorts/Live i nieoczekiwanych hostów.

**Weryfikacja:** pliki zostały zapisane na gałęzi. Nie wykonano Gradle/JUnit, więc testy są dodane, ale ich wynik pozostaje niezweryfikowany. Nie deklarować PASS do czasu uruchomienia `./gradlew testDebugUnitTest`.

### Następne prace

1. W3: wybrać i udokumentować biblioteki HTTP/TLS/JSON dostępne w docelowym Raspberry Pi OS; dodać CMake i szkielet C++ z health/auth oraz testami.
2. W1: równolegle uzyskać raport z docelowego Raspberry Pi potwierdzający MPD CURL input i zachowanie żądań HTTP.
3. Dopasować Android klient do POST /api/v1/queue; usunąć założenie, że serwer zwraca stream_url.
4. Dodać bezpieczny magazyn tokenu Android Keystore i zawęzić globalne cleartext traffic po audycie pozostałych funkcji MALP.



## Aktualizacja prac — Android API, Android Keystore i C++ core (2026-11-17)

### Android — zgodność z kontraktem kolejki

- `YouTubeAudioBridgeClient` wysyła teraz uwierzytelnione `POST /api/v1/queue` z polami `url` i `mode`.
- Klient wymaga tokenu i nie przyjmuje/nie parsuje `stream_url` ani adresów CDN.
- Sukces jest akceptowany tylko po odpowiedzi `success=true`, poprawnym `request_id` i liczbie `queued` od 1 do 200.
- `MainActivity` informuje o dodaniu do kolejki lub rozpoczęciu odtwarzania na podstawie odpowiedzi serwera. Nie zgłasza sukcesu przed potwierdzeniem API.

### Android — ochrona tokenu

- Token jest szyfrowany AES-256-GCM; klucz AES jest opakowany kluczem RSA przechowywanym w Android Keystore.
- Dawny wpis tokenu w zwykłych SharedPreferences jest usuwany przy odczycie; kod nie wraca do przechowywania jawnego tekstu.
- W razie braku/utraty klucza odczyt kończy się pustym tokenem (fail closed); użytkownik musi ponownie wprowadzić dane.
- Wymagane są testy na minimalnym API 21 i aktualnych wersjach Androida; nie wykonano jeszcze kompilacji Android/Gradle w tym środowisku.

### W3 — C++17 core rozpoczęty

Dodano katalog `yoinks-mpd/`:
- CMake dla C++17 i biblioteki core bez zależności zewnętrznych;
- `BoundedStreamBuffer`: stała pojemność RAM, ring buffer, back-pressure producenta, progresywny odczyt, EOF po close i natychmiastowe cancel;
- testy: pojemność zero, zawijanie bufora, zachowanie kolejności bajtów, back-pressure, drenaż po EOF i anulowanie;
- workflow GitHub Actions budujący projekt z ostrzeżeniami traktowanymi jako błędy i uruchamiający CTest.

**Weryfikacja lokalna:** skompilowano implementację C++17 z `-Wall -Wextra -Wpedantic -Werror` i uruchomiono testy bufora; wynik PASS. Test dotyczy wyłącznie prymitywu bufora, nie serwera HTTP ani odtwarzania MPD. Workflow GitHub Actions został dodany, ale wynik zdalnego przebiegu nie został jeszcze potwierdzony.

### Otwarte zadania

1. Dodać testy jednostkowe klienta Android i wykonać `./gradlew testDebugUnitTest` oraz kompilację debug.
2. Zweryfikować implementację Keystore na API 21, API 23+ i aktualnym Androidzie.
3. Dodać serwer HTTP C++, Bearer auth, limity żądań i bezpieczne parsowanie JSON.
4. Dodać resolver yt-dlp wywoływany przez `posix_spawn/execve`, bez shell i bez zapisu mediów.
5. Zaimplementować HTTP relay z back-pressure, refresh URL, cancellation i testami mock upstream.
6. Dodać MPD protocol queue manager i testy semantyki enqueue/play_now.
7. Uzyskać raport MPD CURL z docelowego Raspberry Pi.


## Aktualizacja — bootstrap serwera HTTP C++ (2026-11-17)

Dodano Boost.Beast HTTP server, executable `yoinks-mpd`, konfigurację środowiskową `YOINKS_MPD_BIND`, `YOINKS_MPD_PORT`, `YOINKS_MPD_TOKEN`, kontrolę tokenu Bearer (porównanie stałoczasowe), limit body 16 KiB, limit nagłówków 8 KiB i timeouty odczytu/zapisu. Domyślny bind to 127.0.0.1. `GET /api/v1/health` odpowiada statusem usługi. `POST /api/v1/queue` waliduje JSON i mode, ale celowo zwraca 503 backend_not_ready, dopóki nie powstaną resolver, HTTP relay i MPD queue manager.

Dodano test integracyjny uruchamiający proces i sprawdzający health, 401, 400, fail-closed 503 oraz 413. Test jest zarejestrowany w CTest. CI instaluje Boost i nlohmann-json przed budowaniem. **Nie ma jeszcze potwierdzonego przebiegu CI** dla najnowszego commita; nie deklarować kompilacji serwera jako zaliczonej. Obsługa żądań jest synchroniczna per połączenie, ale odbywa się na stałej puli czterech wątków; brak TLS i lokalnego endpointu strumieniowego nadal wyklucza wdrożenie produkcyjne.


## Uzupełnienie — walidacja adresu po stronie serwera

Control API waliduje URL niezależnie od Androida: HTTPS, dokładną allowlistę hostów, brak userinfo i fragmentu oraz identyfikatory filmu/playlisty i ścieżki. Dodano przypadek integracyjny odrzucający host podobny do YouTube kodem 422. Testy nadal oczekują na wykonanie przez CI.



## Aktualizacja — producent strumienia HTTPS do bufora RAM (2026-11-17)

Dodano `HttpUpstreamStream` (C++17/libcurl): walidację URL źródłowego, wyłącznie HTTPS i hosty w granicy domeny `googlevideo.com`, port 443, zakaz userinfo, weryfikację TLS, brak automatycznych przekierowań, timeout połączenia i limit niskiej prędkości. Dane są zapisywane callbackiem bezpośrednio do stałego bufora RAM o domyślnej pojemności 256 KiB; back-pressure blokuje producenta, limit danych sesji wynosi 512 MiB. Anulowanie czyści bufor i odblokowuje producenta. Dodano testy allowlisty i błędnego startu bez dostępu do sieci.

To nadal wyłącznie komponent upstream → RAM. Brakuje stabilnego rejestru sesji, lokalnego endpointu HTTP z odpowiedzią strumieniowaną/chunked, kontroli zakresów/HEAD według rzeczywistego zachowania MPD oraz testów odtwarzania. Nie wolno oznaczać proxy ani całego mostu jako gotowego.


## Aktualizacja — współbieżność i ekspozycja API (2026-11-17)

Serwer ma stałą pulę czterech wątków i limit 16 zaakceptowanych/zleconych połączeń; nadmiar otrzymuje 503. Adres nasłuchu musi być loopback — konfiguracja adresu LAN jest odrzucana do czasu zaprojektowania TLS. Test integracyjny obejmuje osiem równoległych żądań health. Długie połączenia strumieniowe będą wymagały osobnego, ograniczonego wykonawcy, aby nie blokować API sterującego.


## Aktualizacja — rejestr sesji strumieniowych (2026-11-17)

Dodano `StreamSessionRegistry`: domyślnie maksymalnie cztery sesje, konfiguracyjny limit maksymalny 32. Każda sesja utrzymuje pojedynczy producent HTTPS i bufor o stałej pojemności; łączna alokacja buforów wszystkich sesji ma twardy limit 8 MiB. Identyfikatory sesji są generowane przez systemowy `getrandom(2)` (128 bitów), bez ujawniania adresów CDN. Dodano testy odrzucania nieprawidłowych źródeł, nieznanych identyfikatorów i nadmiernego limitu sesji. Rejestr nie jest jeszcze połączony z endpointem strumieniowym ani API kolejki.


## Aktualizacja wykonania — endpoint HTTP i izolacja puli strumieniowej (2026-11-17)

### Kontrola CI poprzedniego etapu

Commit `56c6133f483d9b30961d5c48670bc723b5886906` uzyskał `build-and-test: success` w GitHub Actions (job `133907788368`, zakończony 2026-11-17 08:13:28 UTC).

### Lokalny endpoint mediów

Zaimplementowano `GET /stream/{opaque-id}` w `yoinks-mpd/src/http_server.cpp`: sesja jest wyszukiwana w `StreamSessionRegistry`, odpowiedź używa `Transfer-Encoding: chunked`, a dane są wysyłane progresywnie w blokach do 16 KiB. Bufor upstream pozostaje ograniczony w RAM i zachowuje back-pressure. Terminujący chunk HTTP jest wysyłany wyłącznie po poprawnym zakończeniu upstream; przy błędzie/niepełnym strumieniu połączenie kończy się bez fałszywego EOF. Sesja jest usuwana po zakończeniu lub błędzie.

Endpoint odrzuca HEAD i inne metody kodem 405, Range kodem 501 (`Accept-Ranges: none`), niepoprawne i nieznane identyfikatory kodem 404. Nie dodano publicznego endpointu tworzenia sesji. Zwykły `POST /api/v1/queue` nadal zwraca 503, ponieważ resolver yt-dlp i MPD queue manager nie są gotowe.

### Izolacja współbieżności

Commit `6cbc9dd3ef80e038960b9ffbffb25c0fa244b72c` przenosi obsługę aktywnego streamu z czterowątkowej puli sterującej do osobnej puli dwóch wątków. Limit aktywnych streamów pozostaje równy 2, a limit zaakceptowanych połączeń pozostaje równy 16. Po przekazaniu połączenia do puli streamów licznik połączeń jest zwalniany przez worker streamu po zamknięciu socketu, a nie przez worker sterujący.

### Testy i status weryfikacji

- CI dla `56c6133...`: PASS.
- CI dla `6cbc9dd...`: uruchomione; wynik oczekuje na zakończenie. Nie oznaczać nowego refaktoru jako zweryfikowanego do czasu uzyskania wyniku.
- Testy integracyjne endpointu sprawdzają 404, 405 i 501 oraz równoległe żądania health. Nie ma jeszcze pozytywnego testu przesyłania bajtów przez prawdziwą sesję; normalny flow nie potrafi jeszcze tworzyć sesji.
- Nie wykonano ponownego builda na Raspberry Pi dla tych nowych commitów.

### Ograniczenia i kolejne bramki

1. Dodać kontrolowany test pozytywnej ścieżki streamu z mockowanym źródłem/readerem, bez osłabiania produkcyjnej allowlisty `googlevideo.com` i bez niezabezpieczonego endpointu tworzenia sesji.
2. Zweryfikować lifecycle i graceful shutdown puli strumieniowej; obecny serwer pracuje w pętli accept bez kompletnej obsługi sygnałów zamknięcia.
3. Dodać resolver yt-dlp przez `posix_spawn`/`execve`, z metadanymi-only, limitami stdout/stderr, timeoutem, anulowaniem procesu i bez logowania signed URL.
4. Dodać MPD protocol queue manager dopiero po testach kontraktu i potwierdzeniu zachowania docelowego MPD.
5. Zachować fail-closed 503 dla `/api/v1/queue` do czasu ukończenia resolvera i managera kolejki.
6. Uzyskać aktualny wynik build/test na Raspberry Pi; poprzedni PASS na Pi dotyczy wcześniejszego stanu przed endpointem i osobną pulą.

**Ocena:** endpoint strumieniowy i osobna pula zostały zaimplementowane w kodzie, lecz nie są jeszcze połączone z kolejką. Nie ma podstaw do stwierdzenia, że odtwarzanie YouTube działa end-to-end.

## Uzupełnienie — testowalna ścieżka chunked HTTP (2026-11-17)

Kontrola GitHub Actions dla commita `6cbc9dd3ef80e038960b9ffbffb25c0fa244b72c` zakończyła się wynikiem `build-and-test: success` (job `133909221968`, 2026-11-17 08:17:12 UTC).

Wyodrębniono wspólną dla produkcyjnego endpointu i testów funkcję `writeChunkedStream` w `yoinks-mpd/include/yoinks/chunked_stream_writer.hpp`. Endpoint `/stream/{id}` korzysta teraz z tej samej implementacji, którą testuje nowy cel CTest `chunked_stream_writer`.

Testy obejmują: porcjowanie do maksymalnie 16 KiB, poprawne ramkowanie chunków, terminujący chunk przy potwierdzonym EOF upstream, brak terminatora przy błędzie upstream oraz natychmiastowe przerwanie odczytu po błędzie zapisu do socketu. Testy nie tworzą produkcyjnej ścieżki wstrzykiwania sesji i nie osłabiają allowlisty `googlevideo.com`.

Zmiany zapisano w commitach: `3c78fed` (helper), `8020523`/`8d155eb`/`191845f` (test i korekty asercji), `16d0fd5` (CTest), `226d3fc` (użycie helpera w serwerze), `3f2d9e0` (README).

**Status weryfikacji:** nowe testy zostały dodane do CMake/CTest, ale ich wykonanie w CI dla najnowszej gałęzi jest jeszcze oczekiwane. Nie deklarować PASS przed zakończeniem workflow. Nie wykonano nowego builda na Raspberry Pi. Testy ramkowania nie zastępują testu live HTTP → MPD CURL.

### Następny etap

1. Po wyniku CI przeprowadzić przegląd lifecycle serwera i dodać kontrolowane zamykanie acceptora oraz pul workerów, wraz z anulowaniem sesji.
2. Dodać test integracyjny obciążenia: dwa długie strumienie w puli stream i równoległy health request, z kontrolowanym mock readerem.
3. Dopiero po tym rozpocząć resolver yt-dlp w trybie metadanych-only, przez `posix_spawn`/`execve`, z limitem wyjścia, timeoutem i bez zapisu audio.
4. Nie podłączać jeszcze `POST /api/v1/queue` do MPD; zachować 503 aż resolver, sesje i queue manager będą kompletne.

## Aktualizacja — kontrolowane zamykanie serwera (2026-11-17)

Po przeglądzie lifecycle dodano obsługę SIGINT/SIGTERM przez `boost::asio::signal_set` oraz asynchroniczne przyjmowanie połączeń. Zamknięcie przebiega w kolejności: zamknięcie acceptora, opróżnienie puli sterującej (aby nie mogła już zgłaszać nowych zadań strumieniowych), anulowanie wszystkich sesji upstream i obudzenie czytelników bufora RAM, a następnie dołączenie puli strumieniowej. Dodano test integracyjny SIGTERM; test rozpoznaje także proces w stanie zombie przed wywołaniem `wait`.

Zmiany kodu: `71ab4f6` (signal-driven shutdown), `f2dc5d1` (anulowanie oczekującego handlera sygnału po błędzie accept), `ef51f37`/`b3e6a44` (test zamykania i korekta wykrywania zakończenia procesu). README uzupełniono w `470fecc`.

### Stan CI

- `6cbc9dd...`: `build-and-test: success` — potwierdzono oddzielenie puli strumieniowej.
- `191845f...`: `build-and-test: success` — potwierdzono kompilację serwera z helperem chunked i poprawiony test rozmiaru pierwszego fragmentu.
- `226d3fc...`: wcześniejszy przebieg `failure` wynikał z niepoprawnej asercji testu (porównanie obejmowało dodatkowy bajt); kompilacja zakończyła się sukcesem. Asercję poprawiono, a następny przebieg `191845f...` przeszedł.
- `b3e6a44...`: CI uruchomione; wynik dla testu zamykania i ostatniej korekty testu oczekuje na zakończenie.

**Ograniczenie:** nie wykonano jeszcze builda na Raspberry Pi dla zmian endpointu, helpera chunked i graceful shutdown. Testy CI są wykonywane na runnerze x86_64 i nie dowodzą kompatybilności z docelowym MPD/CURL ani odtwarzania.

### Następny etap

Po uzyskaniu PASS dla aktualnego CI należy wykonać kontrolowany test na Raspberry Pi w osobnym katalogu tymczasowym, bez instalacji, restartu MPD i zmian kolejki. Dopiero potem kontynuować resolver yt-dlp metadanych-only z `posix_spawn`/`execve`; endpoint kolejki ma pozostać fail-closed do czasu ukończenia resolvera i MPD queue managera.

### Wynik końcowy CI — chunked writer i graceful shutdown

Commit `b3e6a442a336e4ca12774c5e5cedd27e9795ae04` uzyskał `build-and-test: success` w GitHub Actions (job `133915090222`, zakończony 2026-11-17 08:29:06 UTC). Build zakończył się powodzeniem, a CTest: **5/5 testów PASS** (`bounded_stream_buffer`, `http_upstream_validation`, `stream_session_registry`, `chunked_stream_writer`, `http_api_integration`). Test integracyjny obejmuje teraz również kontrolowane zakończenie procesu przez SIGTERM.

Poprzedni commit `226d3fc...` miał wynik `failure` wyłącznie z powodu asercji w nowym teście rozmiaru pierwszego chunku. Asercję skorygowano w `191845f...`; ten commit uzyskał PASS, a następny przebieg `b3e6a44...` potwierdził także graceful shutdown. Błąd testu nie dotyczył kompilacji serwera.

**Zakres dowodu:** CI potwierdza kompilację i testy na runnerze GitHub x86_64. Nie potwierdza jeszcze działania na Raspberry Pi, obsługi konkretnego MPD/CURL, prawdziwego źródła YouTube ani odtwarzania end-to-end.

## Aktualizacja — komponent resolvera yt-dlp (2026-11-17)

### Implementacja

Dodano `YtDlpResolver` w `yoinks-mpd/include/yoinks/yt_dlp_resolver.hpp` i `yoinks-mpd/src/yt_dlp_resolver.cpp`. Proces uruchamiany jest przez `posix_spawn` z osobnymi argumentami argv i bez powłoki. Konfiguracja wymaga bezwzględnej ścieżki wykonywalnego pliku yt-dlp. Proces otrzymuje `/dev/null` na stdin, oddzielne potoki stdout/stderr, własną grupę procesów oraz ograniczony czas działania.

Tryb metadanych używa `--skip-download`, `--flat-playlist`, `--playlist-end` (limit maksymalny 200), `--dump-single-json`, `--ignore-config` i `--no-cache-dir`. Wynik jest parsowany do listy ID, tytułów i opcjonalnego czasu trwania; kolejność wpisów playlisty zostaje zachowana. Nie przekazuje do modelu/Androida URL-i CDN.

Tryb adresu audio przyjmuje wyłącznie 11-znakowy identyfikator filmu, wywołuje `--get-url` i `--skip-download`, a wynik akceptuje wyłącznie po sprawdzeniu HTTPS i granicy hosta `googlevideo.com`. URL pozostaje w pamięci procesu; stderr nie jest zapisywany ani logowany.

Oba tryby mają limit czasu, limit stdout/stderr, zakończenie całej grupy procesów po timeout/przekroczeniu limitu oraz niesensytywne kody błędów. Wspólną walidację adresu YouTube wyodrębniono do `youtube_url.hpp` i wykorzystuje ją zarówno API HTTP, jak i resolver.

### Testy i dowód CI

Testy resolvera korzystają z tymczasowych wykonywalnych atrap i obejmują parsowanie playlisty, kolejność wpisów, czas trwania, walidację hosta YouTube, allowlistę URL CDN, odrzucanie nieprawidłowego ID, timeout procesu oraz limit stdout. Nie wykonują połączeń z YouTube i nie pobierają mediów.

Commit `3ea3d98c54220d4b71b7ec1caa9649ca6fc1bbb9` uzyskał `build-and-test: success` w GitHub Actions (job `133917734049`, zakończony 2026-11-17 08:34:22 UTC). CTest: **6/6 PASS** (`bounded_stream_buffer`, `http_upstream_validation`, `stream_session_registry`, `chunked_stream_writer`, `yt_dlp_resolver`, `http_api_integration`).

### Ograniczenia

- Resolver jest komponentem bibliotecznym, ale nie został jeszcze wstrzyknięty do `HttpServer` ani podłączony do `POST /api/v1/queue`.
- Nie ma jeszcze testu integracyjnego z rzeczywistym yt-dlp/YouTube w nowym komponencie; test instalatora nadal sprawdza metadane testowego filmu oddzielnie.
- Nie ma jeszcze anulowania procesu przez API operacji; na obecnym etapie działa timeout i zabicie grupy procesów.
- Nie ma MPD queue managera, więc `POST /api/v1/queue` ma nadal zwracać 503.
- Nie wykonano ponownego builda na Raspberry Pi po dodaniu resolvera; CI jest dowodem z runnera x86_64.

### Następny etap

1. Zintegrować resolver z kontrolowanym przepływem żądania: najpierw metadane, walidacja i limit playlisty, następnie utworzenie sesji upstream dla wybranych ID.
2. Zaprojektować anulowanie żądania i sesji tak, aby zamknięcie klienta, stop/next i shutdown kończyły proces yt-dlp oraz producenta HTTP.
3. Dopiero po utworzeniu sesji i queue managera zwracać sukces w `/api/v1/queue`; do tego czasu zachować 503.
4. W1 nadal pozostaje otwarty: wymagany jest osobny test MPD CURL na Raspberry Pi, bez zmian w produkcyjnej kolejce.

## Aktualizacja — anulowanie procesu yt-dlp i utwardzenie testów (2026-11-17)

### Zmiany

- `YtDlpResolver::resolveMetadata` oraz `resolveAudioUrl` przyjmują opcjonalny wskaźnik do współdzielonego `std::atomic_bool` należącego do wywołującego.
- Ustawienie flagi podczas pracy procesu kończy całą grupę procesów `yt-dlp` sygnałem SIGKILL, zbiera proces potomny i zwraca kod `process_cancelled` dla operacji metadanych. Flaga musi żyć co najmniej do zakończenia synchronicznego wywołania.
- Anulowanie jest sprawdzane także przed uruchomieniem procesu; istniejące limity czasu, stdout i stderr pozostają aktywne.
- Testy atrap sprawdzają przekazywanie `--ignore-config`, `--no-cache-dir`, `--skip-download`, a dla rozwiązywania URL także `--get-url`.
- Dodano test, który anuluje długo działający proces z drugiego wątku i wymaga zakończenia w czasie krótszym niż 2 sekundy przy limicie procesu 10 sekund.

### Granice wdrożenia

To jest mechanizm biblioteczny, nie pełna integracja anulowania HTTP: `HttpServer` nie zarządza jeszcze flagą żądania ani nie przekazuje jej do resolvera. Nie ma jeszcze powiązania anulowania z operacjami MPD `stop`/`next`. `POST /api/v1/queue` musi nadal zwracać 503, a MPD queue manager pozostaje niezrealizowany.

### Weryfikacja

Zmiany oczekują na wynik GitHub Actions. Testy nie łączą się z YouTube i nie pobierają audio. Nie wykonano nowego builda na Raspberry Pi. Poprzedni wynik 6/6 PASS dotyczy commita `3ea3d98...` i nie obejmuje niniejszej zmiany.

### Kolejne kroki

1. Po PASS CI zintegrować rejestr aktywnych żądań i bezpieczne przekazywanie flagi anulowania z `HttpServer` do resolvera, z kontrolą czasu życia flagi.
2. Dodać testy anulowania przy zamykaniu klienta i przy shutdown serwera.
3. Następnie zaprojektować transakcyjny MPD queue manager bez czyszczenia istniejącej kolejki i bez zmiany trybów odtwarzania.
4. Przed integracją odtwarzania sprawdzić rzeczywiste żądania MPD CURL (HEAD/Range/reconnect) na Raspberry Pi w osobnym, nieprodukcyjnym teście.

### Wynik CI — anulowanie resolvera

Commit testowy `cfa8a0fb882d6ae0c1dff24475331f0d4d61cc06` uzyskał `build-and-test: success` w GitHub Actions (run `44048522129`, job `133920152708`, zakończony 2026-11-17 08:39:49 UTC). Kompilacja C++ zakończyła się powodzeniem, a CTest: **6/6 PASS** (`bounded_stream_buffer`, `http_upstream_validation`, `stream_session_registry`, `chunked_stream_writer`, `yt_dlp_resolver`, `http_api_integration`). Test `yt_dlp_resolver` przeszedł w 0,21 s i obejmuje anulowanie działającego procesu.

Dowód dotyczy runnera GitHub Actions x86_64. Nie wykonano nowego builda na Raspberry Pi ani testu z MPD CURL. Commit dokumentacyjny `454b7b5...` i commit audytu są późniejsze, ale nie zmieniają kodu C++; kodowy commit objęty wynikiem CI to `cfa8a0f...`.

### Wynik CI — izolacja deskryptorów resolvera

Commit `572ee0d6820f282653a501e375ef6788c9c72d77` uzyskał `build-and-test: success` w GitHub Actions (run `44048843126`, job `133921236315`, zakończony 2026-11-17 08:41:46 UTC). Kompilacja C++ zakończyła się powodzeniem, CTest: **6/6 PASS**. Nowy test regresyjny zamyka w procesie potomnym deskryptory stdin/stdout/stderr i potwierdza, że `posix_spawn` nadal poprawnie przechwytuje wynik resolvera.

Zmiana normalizuje deskryptory czterech końców potoków do numerów >= 3 przed konfiguracją działań `posix_spawn`. Chroni to przekierowania standardowych strumieni przed kolizją, gdy usługa startuje z zamkniętym deskryptorem 0, 1 lub 2.

Dowód dotyczy runnera GitHub Actions x86_64. Nie wykonano builda na Raspberry Pi. CI nie potwierdza kompatybilności MPD CURL ani odtwarzania end-to-end.

## Aktualizacja — izolowany MPD queue manager (2026-11-17)

### Implementacja

Utworzono `MpdQueueManager` (`include/yoinks/mpd_queue_manager.hpp`, `src/mpd_queue_manager.cpp`). Komponent łączy się wyłącznie z numerycznym adresem loopback MPD. Przyjmuje wyłącznie dokładny lokalny URI proxy `http://127.0.0.1:<stream-port>/stream/<32-znakowy-opaque-id>`. Dodaje pozycje przez `addid`, dzięki czemu używa stabilnych identyfikatorów utworów MPD. Tryb enqueue wykonuje wyłącznie `addid`; `play_now` wykonuje `playid` dla pierwszej pozycji dodanej przez bieżące żądanie, wyłącznie gdy jawnie zażądano tego trybu.

Komponent nigdy nie wysyła `clear` i nie zmienia repeat/random/single/consume. Przy jednoznacznym odrzuceniu kolejnego `addid` lub `playid` usuwa wyłącznie znane ID dodane przez to żądanie (`deleteid`). Przy utracie odpowiedzi transportowej traktuje wynik jako niejednoznaczny: nie usuwa potencjalnie odtwarzanego utworu i zachowuje znane ID do późniejszego uzgodnienia. Nieznany wynik nie jest raportowany jako sukces.

### Testy i CI

Utworzono mock MPD oparty na rzeczywistym protokole tekstowym. Testy obejmują dopisywanie bez zmian sterowania odtwarzaniem, jawne play-now, rollback po jednoznacznym błędzie, zachowanie się przy utracie odpowiedzi `addid` oraz ochronę potencjalnie bieżącego utworu przy utracie odpowiedzi `playid`, a także odrzucanie obcych URI proxy.

Commit `bd68ff55ea40face9d185a9d1da1ad870e0dacf9` uzyskał `build-and-test: success` w GitHub Actions (run `44049529799`, job `133923538442`, zakończony 2026-11-17 08:47:00 UTC). Build zakończony powodzeniem, CTest: **7/7 PASS** (`bounded_stream_buffer`, `http_upstream_validation`, `stream_session_registry`, `chunked_stream_writer`, `yt_dlp_resolver`, `mpd_queue_manager`, `http_api_integration`).

Wcześniejszy przebieg dla commita `c8086b7...` nie przeszedł kompilacji z powodu błędnego literału znaku końca linii w nowym mocku MPD. Poprawiono go w `f550138...`; następne przebiegi, w tym końcowy `bd68ff5...`, przeszły. Błąd dotyczył testowego mocka, nie implementacji klienta MPD.

### Ograniczenia i bramki przed integracją

- `MpdQueueManager` jest komponentem izolowanym i nie jest jeszcze wstrzyknięty do `HttpServer`; `POST /api/v1/queue` nadal musi zwracać 503.
- Testy MPD wykonano z mockiem protokołu, a nie z działającym MPD na Raspberry Pi. Nie zmieniano produkcyjnej kolejki.
- Obecny `StreamSessionRegistry` tworzy aktywne upstreamy i ma limit czterech sesji. Nie można więc bezpiecznie tworzyć po jednej aktywnej sesji dla całej playlisty do 200 pozycji. Przed połączeniem queue managera z API trzeba wprowadzić sesje leniwe (uruchamiane przy żądaniu MPD) albo równoważny mechanizm ograniczający aktywne źródła.
- Należy również zaprojektować odświeżanie wygasłych adresów CDN bez ujawniania ich MPD/Androidowi oraz przetestować zachowanie rzeczywistego MPD CURL wobec HEAD, Range, reconnect i chunked transfer.

### Następny etap

1. Zaprojektować i przetestować leniwy rejestr sesji: zapisuje wyłącznie video ID i metadane w RAM, a URL CDN rozwiązuje dopiero przy GET `/stream/{id}`; zachować limit dwóch aktywnych strumieni i twardy limit pamięci.
2. Dodać odświeżanie URL źródłowego po wygaśnięciu/odrzuceniu źródła, bez automatycznego przekierowania poza allowlistę `googlevideo.com`.
3. Dopiero po testach sesji połączyć resolver → registry → MpdQueueManager w `POST /api/v1/queue`; do tego czasu endpoint pozostaje fail-closed 503.
4. Uzyskać kontrolowany raport MPD CURL z Raspberry Pi przed aktywacją produkcyjnego odtwarzania.

## Aktualizacja — leniwe sesje strumieniowe (2026-11-17, CI w toku)

Utworzono `LazyStreamSessionRegistry` (`include/yoinks/lazy_stream_session_registry.hpp`, `src/lazy_stream_session_registry.cpp`). Rejestr przechowuje wyłącznie 11-znakowy YouTube video ID i losowy 128-bitowy identyfikator sesji. Rejestracja pozycji nie wywołuje yt-dlp, nie rozwiązuje URL CDN, nie tworzy `HttpUpstreamStream` i nie alokuje bufora audio. Resolver jest wywoływany dopiero przy `activate()` obsługującym żądanie konkretnego strumienia.

Limity: maksymalnie 200 wpisów playlisty, maksymalnie 2 jednocześnie aktywowane/aktywne źródła, łączny limit buforów aktywnych strumieni 8 MiB. Weryfikacja video ID oraz allowlista URL `googlevideo.com` są egzekwowane przed startem upstreamu. Współbieżne żądania aktywacji tej samej sesji są single-flight; drugie żądanie nie uruchamia drugiego procesu yt-dlp.

Anulowanie: każda sesja ma współdzielony `atomic_bool`, przekazywany callbackowi resolvera. `erase()` i `clear()` ustawiają flagę, co pozwala przerwać proces yt-dlp przez już istniejący mechanizm anulowania resolvera. Po usunięciu sesji aktywacja nie może odtworzyć wpisu ani pozostawić aktywnego upstreamu.

Testy `lazy_stream_session_registry` obejmują brak eager resolution, odrzucenie błędnego video ID, odrzucenie nieautoryzowanego hosta, single-flight, anulowanie resolvera podczas `erase()` oraz walidację limitów RAM/sesji.

Commit testów `c49b9a8c9b00ca8647cd867f2e3c7926422ead38` uzyskał `build-and-test: success` w GitHub Actions (run `44052224469`, job `133932611133`, zakończony 2026-11-17 09:05:46 UTC). Build zakończony powodzeniem, CTest: **8/8 PASS**: `bounded_stream_buffer`, `http_upstream_validation`, `stream_session_registry`, `chunked_stream_writer`, `yt_dlp_resolver`, `mpd_queue_manager`, `lazy_stream_session_registry`, `http_api_integration`.

Wcześniejsze commity pośrednie `8d06a40...` i `4096c82...` nie przeszły CI, ponieważ kolejne pliki interfejsu i implementacji były zatwierdzane osobno, zanim aktualizacja testów zakończyła migrację sygnatur callbacku. Następny kompletny commit `c49b9a8...` zawierał spójny interfejs, implementację i testy oraz przeszedł cały build i CTest.

Komponent pozostaje izolowany: `HttpServer` i `POST /api/v1/queue` nie są jeszcze podłączone do leniwego rejestru. Stary aktywny `StreamSessionRegistry` nadal obsługuje istniejący testowy endpoint. Nie wykonano testu na Raspberry Pi ani nie zmieniano kolejki MPD.
## Aktualizacja po teście anulowania resolvera (2026-11-17)

Stan gałęzi `feature/youtube-audio-bridge` po commicie `dc5d174d2413cf5c0dc2f635c5ed8694184ba6f3`: GitHub Actions zakończył przebieg dla tego commita wynikiem `success`.

Nowy test `testAudioUrlCancellationTerminatesProcessGroup` sprawdza anulowanie operacji `resolveAudioUrl`: osobny wątek ustawia flagę `std::atomic_bool`, resolver ma przerwać proces `yt-dlp` przed timeoutem, zwrócić `false` i pozostawić pusty wynikowy URL. Test przechodzi w CI. Interfejs resolvera przyjmuje flagę anulowania zarówno w `resolveMetadata`, jak i `resolveAudioUrl`; flaga musi żyć przez cały czas synchronicznego wywołania.

Potwierdzony zakres CI obejmuje osiem zestawów testów C++/HTTP, w tym bufor RAM, walidację upstream, rejestry sesji, writer chunked, resolver, menedżer kolejki MPD, rejestr lazy i test integracyjny API. Wynik CI nie jest dowodem testu na Raspberry Pi ani kompatybilności wejścia CURL konkretnej kompilacji MPD.

### Aktualny stan integracji — bez zmiany oceny gotowości

- `YtDlpResolver`, `LazyStreamSessionRegistry` i `MpdQueueManager` istnieją i mają testy jednostkowe.
- Resolver i lazy registry obsługują anulowanie procesu resolvera na poziomie interfejsu.
- `HttpServer` nadal tworzy stary `StreamSessionRegistry`; nie korzysta jeszcze z lazy registry.
- `GET /stream/{id}` nadal odczytuje wyłącznie sesję ze starego rejestru.
- `POST /api/v1/queue` nadal zwraca `503 backend_not_ready`; to prawidłowe zachowanie fail-closed, dopóki nie ma kompletnego i przetestowanego przepływu.
- `MpdQueueManager` nie jest jeszcze wywoływany przez serwer HTTP.
- `GET /api/v1/health` nadal zgłasza `control_ready:false`.

### Następna bramka techniczna

Przed włączeniem kolejki należy wprowadzić kontrolowany kontekst zależności serwera (resolver, lazy registry, MPD queue manager i konfiguracja portów), przekazać flagę anulowania do resolvera metadanych, a następnie zintegrować kolejkę i obsługę strumienia. Testy muszą używać atrap resolvera i MPD oraz obejmować odrzucenia, niejednoznaczne odpowiedzi MPD, częściowy rollback, playlisty 1–200 pozycji, aktywację dopiero przy GET i shutdown. Dopiero po tych testach można rozważyć test CURL/MPD na Raspberry Pi. Nie zmieniać kolejki ani konfiguracji działającego MPD bez odrębnej zgody.


## Aktualizacja integracji HTTP → resolver → sesje lazy → MPD (2026-11-17)

Niniejsza sekcja jest nadrzędna wobec wcześniejszych fragmentów tego dokumentu, które określały integrację resolvera, sesji i menedżera MPD jako niezaimplementowaną.

**Wykonane w kodzie gałęzi `feature/youtube-audio-bridge`:**
- Dodano `YoutubeQueueService`, który pobiera wyłącznie metadane playlisty/filmu, tworzy ograniczone sesje lazy i przekazuje MPD wyłącznie lokalne URI `http://127.0.0.1:<port>/stream/<opaque-id>`.
- Tworzenie kolejki nie rozwiązuje adresu CDN i nie uruchamia pobierania audio. Adres źródłowy jest rozwiązywany dopiero przy pierwszym żądaniu `GET /stream/{opaque-id}`.
- `POST /api/v1/queue` jest podłączony do resolvera, rejestru lazy i `MpdQueueManager`. Tryb `enqueue` wyłącznie dopisuje pozycje; `play_now` przekazuje jawne żądanie odtworzenia pierwszej pozycji z bieżącego żądania.
- `GET /stream/{opaque-id}` aktywuje sesję lazy, wywołuje `yt-dlp --get-url --skip-download`, sprawdza granicę HTTPS `googlevideo.com` i przekazuje audio progresywnie przez ograniczony bufor RAM.
- Przy potwierdzonym rollbacku MPD sesje żądania są usuwane. Przy niejednoznacznym wyniku transportowym sesje pozostają, ponieważ MPD mógł już przyjąć URI. API nie deklaruje wtedy sukcesu.
- Konfiguracja uruchomieniowa: `YOINKS_MPD_TOKEN`, `YOINKS_MPD_PORT`, `YOINKS_MPD_BIND`, `YOINKS_MPD_YTDLP` oraz `YOINKS_MPD_MPD_PORT`. Domyślny plik resolvera to `/usr/local/bin/yt-dlp`; MPD nasłuchuje domyślnie na loopback `127.0.0.1:6600`.
- Nie dodano endpointu tworzenia sesji bez uwierzytelnienia. Serwer nadal odrzuca nasłuch poza loopback.
- Nie wykonano żadnych poleceń `clear`, nie zmieniono repeat/random/single/consume. Nie zmieniano działającego MPD ani kolejki na Raspberry Pi.

**Testy i bramki:**
- Dodano testy jednostkowe `YoutubeQueueService`: kolejność i lokalne URI, brak aktywacji upstream podczas enqueue, limit 200, sprzątanie po potwierdzonym rollbacku, zachowanie sesji przy wyniku MPD nieznanym oraz błąd metadanych.
- Test HTTP został zaktualizowany: poprawny URL bez dostępnego resolvera ma kończyć się błędem backendu HTTP 502, a nie fałszywym sukcesem lub historycznym 503.
- Commity modułu i integracji: `027ba2b`, `33dcc56`, `1c5d994`, `fd17525`, `5e688e2`, `11368bd`, `14f264c`, `364296e`.
- Wynik CI dla samego modułu i jego testów: run `44059431153`, zakończony `success` (build i CTest).
- Integracja HTTP/startup/testu 502 została zapisana po tym przebiegu; jej własny CI należy uznać za potwierdzony dopiero po zakończeniu najnowszego workflow z wynikiem `success`.

**Pozostałe bramki — nie deklarować gotowości produkcyjnej:**
1. Zweryfikować najnowszy CI i wszystkie testy CTest po integracji HTTP.
2. Dodać kontrolowane testy HTTP z atrapą resolvera i serwera MPD, w tym pozytywną odpowiedź enqueue, playlistę, play-now, rollback i niejednoznaczny wynik.
3. Sprawdzić zachowanie MPD/CURL w konkretnej wersji zainstalowanej na Raspberry Pi: HEAD, Range, ponowne połączenie, timeout i rzeczywiste odtwarzanie. Obecny endpoint świadomie nie obsługuje HEAD ani Range; zgodność MPD nie jest potwierdzona.
4. Przeprowadzić przegląd cyklu życia i anulowania aktywacji sesji przy shutdown oraz współbieżnych żądań.
5. Dopiero po jawnej zgodzie wykonać testy urządzeniowe. Dotychczasowa kolejka 629 pozycji i aktywna usługa MPD pozostają nietknięte.
6. Android nadal wymaga zgodności z kontraktem `POST /api/v1/queue` (jeżeli klient nadal wysyła `/resolve`) i bezpiecznego przechowywania tokenu. Nie ma gotowego APK ani potwierdzonego odtwarzania end-to-end.

**Ocena:** przepływ serwerowy został połączony w kodzie, lecz pełna zgodność protokołu MPD, test sukcesu HTTP end-to-end, test na Raspberry Pi i integracja Androida pozostają otwarte. Nie scalać do `main` przed przejściem tych bramek.


### Korekta kontraktu odpowiedzi API i obsługi sesji (2026-11-17)

- Odpowiedź sukcesu `POST /api/v1/queue` zwraca `success`, `queued`, `started`, `playlist` oraz `rollback_complete`. Pole `started` jest prawdziwe wyłącznie po potwierdzeniu przez MPD komendy `playid`; `request_id` nie jest wymagane ani sztucznie generowane.
- Klient Androida został dostosowany: brak opcjonalnego `request_id` nie blokuje odczytu potwierdzonej odpowiedzi. Komunikat o rozpoczęciu odtwarzania opiera się na polu `started`.
- Rejestr lazy udostępnia rozróżnienie istniejącej sesji od nieznanego identyfikatora oraz stan błędu resolvera. Nieznana sesja zwraca HTTP 404, błąd aktywacji HTTP 502, a chwilowa aktywacja/wysyczenie limitu HTTP 503.
- Drugie równoległe żądanie dla sesji, która jest już aktywowana, nie usuwa i nie anuluje pierwszego żądania.
- Test HTTP z atrapą `yt-dlp` i zamkniętym portem MPD przechodzi przez walidację, resolver i adapter kolejki, a następnie oczekuje kontrolowanego HTTP 502 z MPD. Testy jednostkowe obejmują również odróżnianie stanu sesji i semantykę `play_now`.
- Potwierdzony przebieg CI dla obsługi sesji i testów HTTP: `44060542148` — `success` (build oraz CTest). Nowszy przebieg po rozszerzeniu testu `play_now` należy sprawdzić oddzielnie.


### Semantyka `play_now` i kody błędów HTTP — aktualizacja

- Tryb `play_now` wstawia nową playlistę jako spójny blok przed istniejącą kolejką MPD: pozycje są dodawane komendą `addid "URI" 0` w odwrotnej kolejności, aby zachować kolejność źródłową, a następnie uruchamiana jest pierwsza pozycja przez jej song ID. Istniejące pozycje pozostają w kolejce.
- Tryb `enqueue` nadal wykonuje wyłącznie kolejne `addid` bez zmiany bieżącego odtwarzania.
- Test mock MPD sprawdza kolejność komend wstawiania oraz to, że `playid` wskazuje pierwszy element żądanej playlisty. Przebieg CI `44060885397` zakończył się `success` (build i CTest).
- Statusy API rozróżniają niedostępny MPD/limit sesji (503), timeout resolvera (504), pustą lub nieobsługiwaną playlistę (422) oraz pozostałe błędy upstream (502).
- Najnowsze zmiany statusów HTTP wymagają zakończenia workflow `44060955448`; do tego czasu nie deklarować ich jako potwierdzonych.


**Potwierdzenie CI po mapowaniu kodów HTTP:** run `44060955448` zakończył się `success` — konfiguracja, kompilacja, wszystkie 9 testów CTest oraz test HTTP z atrapą resolvera i niedostępnym MPD. Run `44061104082` dla dokumentacji i aktualnego drzewa również zakończył się `success`. Ostatni potwierdzony przebieg kodu obejmuje semantykę `play_now`, statusy HTTP, klienta API i poprawki cyklu życia sesji. Nie obejmuje testu na Raspberry Pi ani rzeczywistego odtwarzania.


### Reconciliation kolejki, ponowne odtwarzanie i RAM — aktualizacja 2026-11-17

- Przy każdym nowym żądaniu kolejki usługa odczytuje MPD przez read-only `playlistinfo`, wyodrębnia wyłącznie identyfikatory własnych URI proxy i usuwa nieaktywne sesje, których nie ma już w kolejce MPD.
- Aktywne lub właśnie obsługiwane strumienie są chronione przed sprzątaniem, także gdy MPD usuwa pozycję podczas odtwarzania.
- Operacje `resolve → snapshot MPD → create sessions → addid/playid` są serializowane mutexem usługi. Zapobiega to sytuacji, w której równoległe żądanie usuwa sesje utworzone przez inne, jeszcze niewstawione do MPD.
- Po zakończeniu strumienia usługa zachowuje lekką capability (YouTube video ID), ale zwalnia obiekt strumienia i bufor audio RAM. Przy ponownym GET dla tego samego URI resolver odświeża adres CDN i tworzy nowy bufor. Po przerwaniu połączenia upstream jest anulowany i następne żądanie wymusza ponowne rozwiązanie adresu.
- Limit dwóch równoległych sesji uwzględnia również przypadek, w którym upstream zakończył pobieranie, ale klient nadal odbiera dane z bufora. Bufory zakończonych/przerwanych strumieni są zwalniane po zwolnieniu czytelnika.
- Limit czasu: yt-dlp metadata do 30 s; każdy z dwóch etapów MPD (snapshot i enqueue) do 15 s. Łączny budżet nominalny wynosi maksymalnie 60 s, aby zmieścić się w 65-sekundowym limicie odczytu klienta Androida.
- Testy obejmują odczyt playlisty MPD, filtrowanie lokalnych URI, usuwanie nieaktualnych capability, zachowanie sesji aktywnych, fail-closed przy błędzie snapshotu i ponowienie nieudanej aktywacji.
- Potwierdzony przebieg CI przed ostatnimi zmianami limitu aktywnych czytelników i limitów czasu: run `44062367873` — build i CTest `success`. Przebiegi dla kolejnych zmian należy sprawdzić po ich zakończeniu.
- Sesje usunięte z MPD są sprzątane przy kolejnym żądaniu kolejki; nie ma jeszcze osobnego demona monitorującego zmiany MPD. Pełna weryfikacja replay/reconnect na rzeczywistym MPD/CURL pozostaje wymagana.


### Dostęp Androida przez HTTPS

- Klient Androida i ustawienia Bridge wymagają obecnie adresu `https://`; zwykły HTTP jest odrzucany, ponieważ żądania zawierają token Bearer.
- Serwer Yoinks-MPD nadal nasłuchuje wyłącznie na `127.0.0.1:8788`. Nie wolno wystawiać tego portu bezpośrednio do sieci LAN/Internetu.
- Dodano przykład `deploy/nginx/yoinks-mpd-https.conf.example`: TLS 1.2/1.3, przekazanie API do loopback, wyłączenie buforowania i plików tymczasowych Nginx oraz blokada zdalnego dostępu do `/stream/` (ten endpoint jest przeznaczony wyłącznie dla lokalnego MPD).
- Przykład wymaga podstawienia rzeczywistej nazwy hosta i certyfikatu TLS. Nie został wdrożony ani przetestowany na Raspberry Pi. Instalator nadal nie konfiguruje Nginx, certyfikatu ani usługi systemd.


## Instalator systemd — 2026-11-17

Dodano kompletny instalator `scripts/yoinks-mpd/install.sh` oraz jednostkę `scripts/yoinks-mpd/yoinks-mpd.service`.

- Instalator buduje C++17 Release, uruchamia pełny CTest i dopiero po sukcesie instaluje binarium.
- Pobiera oficjalny yt-dlp i sprawdza SHA-256 z upstreamowego `SHA2-256SUMS`.
- Tworzy systemowego użytkownika `yoinks-mpd`, token o wysokiej entropii i plik `/etc/yoinks-mpd/yoinks-mpd.env` z uprawnieniami 0600.
- Instaluje i domyślnie uruchamia ograniczoną usługę systemd: loopback only, bez roota, bez capabilities, z limitami zadań/pamięci, read-only systemem plików i prywatnym /tmp.
- `--no-start` pozwala zainstalować i włączyć usługę bez uruchamiania jej; `--check-youtube` dodaje opcjonalny test samych metadanych.
- Instalator nie instaluje, nie konfiguruje, nie restartuje ani nie modyfikuje MPD i nie dotyka kolejki.
- Nie konfiguruje TLS ani dostępu zdalnego; serwer pozostaje związany z 127.0.0.1.
- Dodano `scripts/yoinks-mpd/INSTALL.md` z procedurą instalacji i ograniczeniami oraz rozszerzono CI o składnię Bash, pomoc instalatora i walidację jednostki systemd.
- Wynik CI dla zmian instalatora musi zostać oceniony po zakończeniu workflow; do tego czasu nie uznawać walidacji za zaliczoną.



### Potwierdzenie CI instalatora — 2026-11-17

- GitHub Actions run `44066415238`, commit `6922f684b361283760c33679a0572e4a1ec6dff3`: **success**.
- Zaliczone kroki: składnia skryptów Bash, uruchomienie pomocy instalatora, walidacja struktury jednostki systemd, konfiguracja CMake, kompilacja C++ i pełny CTest.
- Walidacja jednostki w CI używa testowego `ExecStart=/bin/true` i `EnvironmentFile=/dev/null`, aby sprawdzić składnię i dyrektywy bez wymagania zainstalowanego programu na runnerze.
- Ponowna instalacja nie restartuje aktywnej usługi domyślnie, aby nie przerwać odtwarzania. Jawne `--restart` może przerwać bieżący strumień; `--no-start` pomija uruchomienie.
- CI potwierdza składnię, kompilację, testy i strukturę jednostki, ale nie wykonuje instalacji na Raspberry Pi ani nie testuje realnego MPD/CURL.


---

# Aktualizacja: potwierdzony odsłuch i wariant obserwatora playlisty MALP (2026-11-17)

## Potwierdzony test na Raspberry Pi

Użytkownik wykonał rzeczywiste żądanie do działającego Yoinks-MPD dla adresu YouTube w trybie `enqueue`. API zwróciło:

```json
{"playlist":false,"queued":1,"rollback_complete":true,"started":false,"success":true}
```

Użytkownik następnie potwierdził: **„Jest dźwięk”**.

To jest potwierdzenie rzeczywistego odtwarzania przez istniejący MPD i tor audio HiFi dla ścieżki API → lokalne URI proxy → MPD. Nie jest to jednak potwierdzenie nowego wariantu, w którym MALP samodzielnie dodaje surowy URL YouTube do playlisty MPD. Odpowiedź `started:false` jest zgodna z semantyką `enqueue`: wpis został dodany, ale żądanie nie uruchamia odtwarzania.

## Audyt istniejącego kodu pod kątem obserwatora

Na HEAD `be327358425f912a169eff4da2fde3cc28c0177c` potwierdzono:

- `main.cpp` uruchamia jeden serwer HTTP i tworzy jeden `MpdQueueManager` oraz jeden `YoutubeQueueService`.
- `MpdQueueManager` potrafi dopisywać wyłącznie zaufane lokalne URI proxy przez `addid`; ma odczytowy `playlistinfo` używany do sprzątania sesji.
- `YoutubeQueueService` przyjmuje URL z API, rozwiązuje metadane, tworzy lazy session i przekazuje lokalne URI do MPD.
- W kodzie nie ma jeszcze połączenia MPD `idle`, obserwatora playlisty ani mechanizmu wykrywania URL YouTube dodanego bezpośrednio przez MALP.
- Aktualna usługa nie przechwytuje komend MPD wysyłanych przez MALP. Samo uruchomienie Yoinks-MPD obok MPD nie powoduje automatycznej konwersji wpisów MALP.

## Decyzja projektowa: obserwator jako rozszerzenie istniejącego procesu

Docelowo obserwator ma działać wewnątrz istniejącego procesu `yoinks-mpd`, bez drugiej usługi i bez modyfikowania oficjalnego MALP:

1. Osobne połączenie MPD utrzymuje `idle playlist`; nie wolno współdzielić połączenia będącego w stanie `idle` z poleceniami `playlistinfo`, `status`, `addid` lub `deleteid`.
2. Po zdarzeniu `playlist` obserwator pobiera spójny snapshot `playlistinfo` oraz `status`, zapisując `songid`, `Pos`, URI i bieżący stan odtwarzania.
3. Rozpoznawane są wyłącznie URL przechodzące istniejącą ścisłą walidację `isAllowedYouTubeUrl`. Lokalne URI proxy oraz wszystkie zwykłe wpisy pozostają bez zmian.
4. Rozwiązywanie metadanych i tworzenie sesji korzysta z istniejącego `YtDlpResolver` i `LazyStreamSessionRegistry`; nie wolno tworzyć drugiego resolvera ani osobnego serwera strumieni.
5. Przetwarzanie musi być idempotentne. Własne URI proxy nie mogą ponownie trafić do resolvera. Zmiany playlisty wykonane przez obserwator muszą być odróżnialne od zmian MALP.
6. Zmiana pozycji odbywa się po `songid`, nie po samym indeksie `Pos`, ponieważ MALP może równolegle zmieniać kolejkę.
7. Nie wolno używać `clear`, przebudowywać całej playlisty ani zmieniać trybów repeat/random/single/consume.

## Krytyczne ograniczenie bezpieczeństwa odtwarzania

Nie wolno bezwarunkowo wykonać `deleteid` dla aktualnie odtwarzanego wpisu YouTube. Usunięcie bieżącego song ID może zatrzymać lub zmienić odtwarzanie. Z tego powodu pierwsza wersja obserwatora powinna:

- konwertować wyłącznie przyszłe pozycje, które nie są aktualnym `songid`;
- dla aktualnie odtwarzanego YouTube zapisać stan `pending_current` i nie wykonywać destrukcyjnej podmiany;
- przewidzieć osobną, przetestowaną procedurę przełączenia aktualnego wpisu dopiero wtedy, gdy da się zachować intencję użytkownika i nie przerwać aktualnie grającego zwykłego utworu;
- po każdej mutacji ponownie pobrać snapshot i zweryfikować, czy pozycja docelowa nadal istnieje i czy nie zmienił się bieżący utwór.

Obserwator oparty na `idle` jest asynchroniczny. Jeżeli MALP doda URL i natychmiast wyda `play`/ `playid`, istnieje wyścig między odtwarzaniem a konwersją. Nie wolno deklarować, że obserwator gwarantuje konwersję przed startem. Jeśli testy wykażą, że MALP może natychmiast uruchomić surowy URL, pełna gwarancja wymagałaby pośrednika protokołu MPD lub zmiany sposobu dodawania pozycji — nie należy ukrywać tego ograniczenia.

## Bramki przed włączeniem automatycznej konwersji

1. **Akceptacja URI:** na docelowym MPD sprawdzić, czy `addid "https://www.youtube.com/watch?..."` jest akceptowane i czy MPD zachowuje wpis jako URI, zamiast odrzucać go przy walidacji wejścia. Test musi używać odrębnej, kontrolowanej kolejki testowej albo pustego środowiska testowego; nie dotykać produkcyjnej kolejki.
2. **Protokół idle:** dodać testy zdarzeń playlisty, wielu zmian w krótkim czasie, rozłączenia i ponownego połączenia oraz zmian zachodzących w trakcie pobierania snapshotu.
3. **Współbieżność:** testować równoległe dodawanie/usuwanie/przesuwanie pozycji przez MALP. Każda mutacja ma być warunkowana ponowną weryfikacją `songid` i URI.
4. **Bezpieczna konwersja:** testować pozycję przyszłą, aktualną odtwarzaną, zatrzymaną, usuniętą równolegle oraz duplikaty URL.
5. **Zgodność sesji:** sprawdzić rollback, limity 200 wpisów, sprzątanie sesji oraz brak ponownego rozpoznawania lokalnego URI.
6. **Uruchamianie:** watcher ma być opcjonalnie sterowany konfiguracją i domyślnie wyłączony do czasu ukończenia testów. Nie restartować działającej usługi automatycznie.

## Status prac

- [x] Audyt rzeczywistych ścieżek źródeł i punktu uruchomienia procesu.
- [x] Potwierdzenie, że istniejący kod nie ma jeszcze obserwatora MPD.
- [x] Zapisanie decyzji architektonicznej i warunków bezpieczeństwa.
- [x] Zapisanie potwierdzonego przez użytkownika testu dźwięku ścieżki API.
- [x] Implementacja transportu `idle` oraz odczytowego snapshotu playlisty (integracja cyklu życia nadal oczekuje).
- [ ] Testy wyścigów i idempotencji obserwatora.
- [ ] Test potwierdzający, że docelowy MPD przyjmuje surowy URL YouTube jako wpis kolejki.
- [ ] Implementacja bezpiecznej konwersji przyszłych wpisów.
- [ ] Procedura dla aktualnie odtwarzanego wpisu.
- [ ] Włączenie obserwatora po przejściu bramek.

**Wniosek:** potwierdzony jest już rzeczywisty dźwięk z obecnej ścieżki API. Nowy wariant MALP → MPD → obserwator jest wykonalny jako rozszerzenie procesu Yoinks-MPD, ale nie został jeszcze zaimplementowany. Nie wolno aktywować destrukcyjnej podmiany bieżącego utworu ani zakładać, że MPD przyjmie surowy URL YouTube bez testu.


## Implementacja etapu 1 — transport MPD idle (2026-11-17)

Dodano izolowany, jednorazowy komponent `MpdPlaylistWatcher`:

- otwiera własne połączenie TCP do MPD i subskrybuje wyłącznie `idle playlist`;
- rozpoznaje `changed: playlist` i końcowe `OK`;
- ignoruje zdarzenia innych podsystemów, np. `player`;
- ma ograniczenie czasu operacji i rozmiaru odpowiedzi;
- odrzuca konfiguracje inne niż loopback;
- nie wysyła poleceń mutujących, nie usuwa utworów, nie uruchamia odtwarzania i nie zmienia trybów MPD;
- jest jednorazowym prymitywem transportowym: właściciel pętli obserwatora odpowiada za ponawianie subskrypcji oraz pobranie świeżego snapshotu na osobnym połączeniu.

Dodano testy protokołu dla zdarzenia playlisty, zdarzenia niezwiązanego z playlistą oraz odrzucenia zdalnego hosta.

**Ograniczenie etapu:** komponent nie jest jeszcze podłączony do `main.cpp` i nie konwertuje URL. To celowe: najpierw weryfikujemy zachowanie protokołu bez aktywowania automatycznej ingerencji w produkcyjną kolejkę. Następny etap obejmuje pełny snapshot `playlistinfo` + `status`, cykl życia subskrypcji oraz testy wyścigów przed jakąkolwiek podmianą wpisu.


## Aktualizacja 2026-11-17 — pełny, odczytowy snapshot playlisty MPD

- CI dla d4f25d70b04e2e706af09fbaae49507ee115f26d zakończyło się sukcesem; wszystkie kroki build, CTest oraz kompilacja i testy ASan/UBSan w GitHub Actions mają status success.
- Dodano do MpdQueueManager metodę listPlaylistState(). Korzysta z osobnego, zwykłego połączenia MPD (nigdy z połączenia idle) i wykonuje wyłącznie playlistinfo oraz status.
- Wynik zawiera osobne wpisy playlisty: song_id, position, uri, a także state i opcjonalny current_song_id.
- Parser nie zakłada kolejności pól Id: i Pos: w obrębie wpisu. Odrzuca niekompletne wpisy, niepoprawne liczby, nieznany stan MPD oraz identyfikator bieżącego utworu, którego nie ma w odczytanej playliście.
- Stan play i pause wymaga songid; stan stop nie może zawierać bieżącego songid. Pusta playlista ze stanem stop jest poprawnym wynikiem.
- Dotychczasowa metoda listStreamSessionIds() pozostaje bez zmian. Nowa metoda nie mutuje playlisty, nie wywołuje playback i nie zmienia trybów MPD.
- Dodano test integracyjny z atrapą MPD sprawdzający identyfikator, pozycję, URI, kolejność pól oraz status odtwarzacza. Wynik testu będzie potwierdzony przez kolejny przebieg CI.
- Nadal nie ma integracji obserwatora z main.cpp, automatycznej konwersji URL YouTube ani wdrożenia na Raspberry Pi. Nie wykonano testu addid dla surowego URL YouTube na docelowym MPD; do czasu rozstrzygnięcia tej bramki watcher pozostaje nieaktywny.


## Korekta CI i snapshotu — 2026-11-17

- Przebieg CI dla commita `e5b0f8ae` wykrył błąd kompilacji: w `listPlaylistState()` pozostawiono drugi odczyt `status` i deklarację o tej samej nazwie. Nie był to błąd protokołu MPD ani testów, ponieważ kompilacja zatrzymała się przed uruchomieniem testów.
- Usunięto redundantne polecenie `status`. Snapshot używa teraz wyłącznie jednej listy odczytowej: `command_list_ok_begin`, `playlistinfo`, `status`, `command_list_end`.
- Parser rozdziela odpowiedź playlisty i statusu na separatorze `list_OK`, a następnie weryfikuje końcowe `OK`. Odpowiedzi ACK, przekroczenie limitu i niepełny protokół są zgłaszane jako błędy.
- Poprawka zapisana w commicie `73155e5a61bbf0ac4510f5f4d870ffac3f001897`. CI dla tego commita jest uruchomione; do czasu zakończenia nie oznaczamy kompilacji ani testów jako zaliczonych.
- Test atrapy MPD weryfikuje, że oba polecenia są wysyłane w jednej liście oraz że wynik zawiera `songid`, `Pos`, URI i stan odtwarzacza.
- Nadal nie uruchomiono watchera w procesie usługi. Nie wykonano żadnej operacji na docelowej kolejce ani testu surowego URL YouTube przez `addid`.


## Rozszerzenie testów snapshotu MPD — 2026-11-17

- Rozbudowano istniejącą atrapę MPD o konfigurowalną odpowiedź command-list; zachowano jeden wspólny mechanizm testowy bez kopiowania serwera testowego.
- Dodano przypadki: pusta playlista w stanie stop, pauza z bieżącym songid, stop z niedozwolonym songid, niekompletne wpisy (brak Id/Pos/file), nieznany lub brakujący stan, ACK z command list oraz brak separatora list_OK.
- Testy pozostają odczytowe: nie wykonują mutacji kolejki ani komend playback.
- Oczekują na wynik CI dla commita zawierającego tę zmianę. Testy limitu odpowiedzi i awarii połączenia pozostają do dalszego rozszerzenia; nie uznajemy ich za pokryte.
- Obserwator nadal nie jest zintegrowany z main.cpp, a automatyczna konwersja YouTube pozostaje wyłączona.


## Testy limitu odpowiedzi i błędu połączenia — 2026-11-17

- Dodano test przekroczenia limitu liczby linii odpowiedzi command-list (8320 linii), oczekujący na bezpieczne odrzucenie snapshotu.
- Dodano test zamkniętego lokalnego portu MPD, sprawdzający jawny błąd `mpd_connect_failed` bez ingerencji w usługę produkcyjną.
- Testy korzystają wyłącznie z lokalnych atrap/gniazda testowego; nie łączą się z Raspberry Pi ani z zewnętrznym MPD.
- CI musi potwierdzić kompilację i zachowanie tych przypadków przed dalszą integracją watchera.
- Integracja watchera z `main.cpp` oraz automatyczna konwersja YouTube pozostają nieaktywne.


## Korekta testu limitu odpowiedzi snapshotu — 2026-11-17

- Przebieg CI `44150764418` zakończył się niepowodzeniem wyłącznie na teście `mpd_queue_manager`; kompilacja wszystkich celów zakończyła się sukcesem, a pozostałe testy przeszły.
- Przyczyną był błędny zapis separatorów w fixture `testSnapshotResponseLineLimitIsEnforced()`: test generował literalne znaki backslash+n zamiast rzeczywistych znaków końca linii. Serwer testowy wysyłał więc jeden niekończący się wiersz protokołu i pozostawał w oczekiwaniu na kolejne polecenie, przez co `mock.join()` blokował test aż do timeoutu CTest.
- Poprawiono literały na standardowe sekwencje C++ `\\n`, tak aby odpowiedź zawierała 8320 osobnych linii i końcowy separator command-list. Nie zmieniano limitów produkcyjnego parsera ani zachowania MPD.
- Poprawka testu: commit `edc32ae56de8efe4769188d3d994f232d729e733`. Treść pliku ponownie pobrano z GitHub i sprawdzono.
- Wynik nowego CI dla poprawki wymaga potwierdzenia; do czasu zakończenia nie uznawać testów za zaliczone.
- Integracja watchera, automatyczna konwersja URL YouTube i wdrożenie na Raspberry Pi pozostają nieaktywne.


## Integracja obserwatora playlisty — etap odczytowy (2026-11-17)

- CI po korekcie fixture snapshotu: run `44151739744` — **success**, w tym kompilacja, CTest oraz build i test ASan/UBSan.
- Zintegrowano opcjonalny worker obserwatora z istniejącym `HttpServer::run()`; nie powstaje osobny demon, proces ani port.
- Włączenie jest jawne przez `YOINKS_MPD_PLAYLIST_WATCH=true` (domyślnie `false`). Przy wyłączonej opcji ścieżka startowa zachowuje dotychczasowe działanie.
- Worker utrzymuje osobne, jednorazowe połączenie MPD `idle playlist`, po zdarzeniu pobiera świeży snapshot przez istniejący `MpdQueueManager::listPlaylistState()`. Połączenie `idle` nie jest współdzielone z poleceniami snapshotu.
- Timeout subskrypcji wynosi 5 s, co ogranicza czas zatrzymania. Przy błędzie transportu worker ponawia połączenie z wykładniczym opóźnieniem do 10 s. SIGINT/SIGTERM ustawia flagę zatrzymania; worker jest dołączany przed zamykaniem pozostałych pul.
- Logi zawierają wyłącznie liczbę pozycji, stan MPD i kod błędu. Nie logują URI, URL YouTube, tokenów ani metadanych utworów.
- **Worker jest wyłącznie obserwacyjny:** nie rozwiązuje URL, nie tworzy sesji, nie dodaje/usuwa/przesuwa pozycji i nie wywołuje playback. Brak automatycznej konwersji pozostaje zamierzony.
- Nowy etap wymaga odrębnego CI. Testy cyklu życia i reconnect watchera oraz bezpieczna akceptacja surowego URL przez docelowy MPD nadal pozostają otwarte. Nie uruchamiać opcji na Raspberry Pi przed potwierdzeniem CI i kontrolowanym teście.
