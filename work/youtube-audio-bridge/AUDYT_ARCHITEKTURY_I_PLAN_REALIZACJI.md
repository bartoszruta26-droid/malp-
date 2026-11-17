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