# Profesjonalny audyt i plan realizacji: YouTube → MALP → Raspberry Pi → MPD

**Data audytu:** 2026-11-17  
**Repozytorium:** `bartoszruta26-droid/malp-`  
**Gałąź:** `feature/youtube-audio-bridge`  
**Katalog prac:** `work/youtube-audio-bridge/`  
**Status:** audyt dokumentacyjno-architektoniczny; implementacja usługi i odtwarzanie end-to-end pozostają do wykonania i weryfikacji.

---

## 1. Zasady audytu: prawda, sprawiedliwość, miłość i odpowiedzialność

Audyt stosuje cztery praktyczne zasady:

1. **Prawda** — oddzielamy stan potwierdzony kodem od deklaracji, planów i testów, których nie wykonano. Nie nazywamy prototypu działającym odtwarzaczem.
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

## 3. Stan faktyczny repozytorium

Audyt wykonano na plikach gałęzi `feature/youtube-audio-bridge`. Poniższe obserwacje odnoszą się do pobranych wersji plików, a nie do uruchomienia aplikacji.

### 3.1 Istniejące elementy

- `scripts/yoinks-mpd/yt-dlp-to-fifo.sh` — prototyp Bash dla jednego filmu, który kieruje wyjście binarne yt-dlp do FIFO.
- `scripts/yoinks-mpd/install.sh` — instalator APT, pobiera samodzielny plik yt-dlp dla wybranych architektur i sprawdza SHA-256 względem upstreamowego pliku sum.
- `tests/yoinks-mpd/test-yt-dlp-to-fifo.sh` — testy offline z atrapą yt-dlp.
- `docs/yoinks-mpd/README.md` i `docs/yoinks-mpd/INSTALL_RASPBERRY_PI.md` — dokumentacja prototypu FIFO i instalacji.
- `docs/YOUTUBE_AUDIO_BRIDGE_NEXT_STEPS.md` — porównanie rozwiązań upstream i późniejsza koncepcja lokalnego HTTP relay.
- `app/src/main/java/org/gateshipone/malp/application/youtube/YouTubeShareParser.java` — parser udostępnionych adresów.
- `app/src/main/java/org/gateshipone/malp/application/youtube/YouTubeAudioBridgeClient.java` — klient HTTP Androida.
- `app/src/main/java/org/gateshipone/malp/application/youtube/YouTubeBridgePreferences.java` — zapis adresu usługi i tokenu w domyślnych SharedPreferences.
- `MainActivity.java` — odbiera pojedynczy `ACTION_SEND`, pokazuje wybór enqueue/play-now i wykonuje żądanie w pojedynczym executorze.

### 3.2 Najważniejsze braki potwierdzone w kodzie

1. **Brak serwera Raspberry Pi zgodnego z API Androida.** Klient wysyła `POST /api/v1/resolve`; instalator kopiuje jedynie skrypt producenta FIFO. Nie ma dostarczonej implementacji usługi HTTP realizującej ten kontrakt.
2. **Brak wstawiania pozycji do MPD.** `MainActivity.resolveYouTubeShare()` otrzymuje listę pozycji, ale wyświetla komunikat, że wstawianie do kolejki nie jest dostępne. To nie jest działająca integracja odtwarzania.
3. **Brak potwierdzonej drogi wejściowej FIFO do MPD.** Dokumentacja MPD opisuje `pipe` i `fifo` jako wyjścia audio MPD, a nie ogólne wejścia. Skrypt producenta dowodzi jedynie możliwości wysyłania bajtów do FIFO.
4. **Rozbieżność dokumentacji.** Główny `README.md` oraz `docs/yoinks-mpd/README.md` wciąż przedstawiają FIFO jako główną ścieżkę. Nowszy dokument planu wskazuje lokalny HTTP relay przez wejście CURL MPD. Dokumentacja wymaga jednego, zatwierdzonego wariantu.
5. **Brak obsługi `ACTION_SEND_MULTIPLE`.** Manifest deklaruje wyłącznie `ACTION_SEND`; handler również obsługuje tylko pojedynczy tekst i pojedynczy link.
6. **Brak kompletnego kontraktu kolejki i anulowania.** Nie ma gotowego mechanizmu atomowego dodawania playlisty, usuwania pozycji należących do żądania, zatrzymywania producenta przy next/stop ani odzyskiwania po rozłączeniu.
7. **Bezpieczeństwo klienta wymaga poprawy.** Token jest opcjonalny, a adres usługi dopuszcza zwykłe HTTP. Odpowiedź akceptuje dowolne adresy HTTP(S) jako `stream_url`, bez sprawdzenia, czy host i port należą do skonfigurowanej usługi.
8. **Sekret w SharedPreferences.** Token jest zapisany w zwykłych preferencjach aplikacji; należy przenieść go do magazynu opartego na Android Keystore.
9. **Globalne `usesCleartextTraffic=true`.** Manifest dopuszcza nieszyfrowany ruch HTTP w całej aplikacji. Należy ograniczyć wyjątek do jawnie skonfigurowanego, zaufanego LAN albo wymagać HTTPS.
10. **Parser URL wymaga dopracowania.** Lista hostów zawiera powtórzony wpis `www.youtube.com`; kod ma warunek dla `www.youtu.be`, którego nie ma na allowliście. Parser sprawdza host i ścieżkę, ale nie wymaga poprawnego identyfikatora filmu dla `/watch`; potrzebne są testy przypadków granicznych i parametrów.
11. **Instalator nie instaluje usługi.** Pobranie yt-dlp i skopiowanie skryptu nie oznacza, że powstał daemon, serwer API ani integracja MPD.
12. **Testy prototypu nie są testami odtwarzania.** Test z atrapą yt-dlp i osobnym czytnikiem FIFO nie sprawdza MPD, rzeczywistego YouTube, API, kolejki ani zachowania dźwięku.

### 3.3 Ocena stanu

| Obszar | Ocena | Uzasadnienie |
|---|---|---|
| Android share | Częściowy prototyp | Pojedynczy ACTION_SEND i dialog; brak pełnego przepływu |
| Parser YouTube | Częściowy | Allowlista jest dobrym początkiem, ale wymaga testów i korekt |
| Klient HTTP Android | Częściowy | Klient istnieje, lecz nie ma serwera po drugiej stronie |
| Ustawienia bridge | Częściowe | URL/token są zapisywane; brak bezpiecznego magazynu i walidacji |
| Producent FIFO | Prototyp | Jedna pozycja; brak potwierdzonego odbiorcy MPD |
| Usługa Pi/API | Brak | Nie znaleziono implementacji zgodnej z klientem |
| Stabilny strumień HTTP | Brak | Nie ma lokalnego proxy ani odświeżania URL |
| MPD queue | Brak | Brak implementacji dodawania i zarządzania pozycjami |
| Playlista | Brak | Brak enumeracji i kolejki niezależnych pozycji |
| RAM-only assurance | Nieudowodnione | Nie ma testów śledzenia zapisów i polityki swap |
| Test Android/Gradle | Niezweryfikowany | W tym audycie nie wykonano kompilacji |
| Test sprzętowy | Niezweryfikowany | Brak niezależnego testu end-to-end |

**Wniosek:** obecny stan to prototyp parsera, klienta i producenta FIFO oraz dokumentacja. Nie wolno przedstawiać go jako gotowego odtwarzacza YouTube dla MPD.

## 4. Decyzja architektoniczna

### 4.1 Zalecany wariant: lokalny HTTP relay w C++

Rekomenduję zrezygnować z FIFO jako docelowego transportu do MPD i przyjąć lokalny serwer HTTP w C++, pod warunkiem potwierdzenia obsługi wejścia CURL w dokładnym buildzie MPD na Raspberry Pi.

Powód: MPD ma udokumentowane wejście HTTP/CURL, natomiast FIFO nie jest udokumentowanym, uniwersalnym wejściem. Wariant HTTP pozwala umieścić stabilne URI w kolejce MPD, a proxy może odświeżać krótkotrwały adres CDN w pamięci.

To jest zmiana decyzji technicznej względem wczesnego prototypu FIFO. Nie należy jej ukrywać: dokumentacja i nazwy etapów muszą wyraźnie wskazywać, że FIFO pozostaje wyłącznie testem producenta, a nie docelowym interfejsem MPD.

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
| W0 | Ujednolicenie dokumentacji i ADR | Jedna zatwierdzona architektura HTTP relay; FIFO opisane jako prototyp |
| W1 | Dowód MPD CURL input | Wynik z dokładnej wersji MPD na Raspberry Pi |
| W2 | Specyfikacja API i bezpieczeństwa | Kontrakt endpointów, JSON, kody błędów, token, limity |
| W3 | C++ service skeleton | Uruchamianie, konfiguracja, logowanie bez sekretów, systemd |
| W4 | Resolver yt-dlp | Metadane i playlisty bez pobierania audio |
| W5 | HTTP relay RAM-only | Stabilne URI, ograniczony bufor, back-pressure, anulowanie |
| W6 | MPD queue manager | enqueue/play_now, identyfikatory pozycji, rollback i cleanup |
| W7 | Android integration | Bezpieczne ustawienia, API client, share intents, wynik operacji |
| W8 | Testy i regresja | C++, Android, security, RAM-only, MPD i Pi |
| W9 | Dokumentacja instalacji i wydania | Instrukcja, audyt końcowy, release gate |

Każdy etap należy wykonać osobno, z przeglądem zmian i aktualizacją audytu. Nie rozpoczynać kolejnego etapu, jeśli poprzedni ma nierozwiązany błąd blokujący.

## 13. Decyzje architektoniczne wymagające zapisania (ADR)

1. **ADR-001: Transport MPD** — rekomendacja: lokalny HTTP relay + MPD CURL input; FIFO nie jest docelowym wejściem.
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
| Brak CURL w MPD | Brak odtwarzania HTTP | Bramka W1 przed budową relay |
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

1. Ujednolicić README i dokumenty: docelowy wariant HTTP relay, bez twierdzenia, że FIFO jest wejściem MPD.
2. Uzyskać z Raspberry Pi dokładny wynik `mpd --version`, `mpc version` i test wejścia HTTP z CURL.
3. Opracować kontrakt API i kolejki; ustalić semantykę play-now dla playlist.
4. Dopiero po potwierdzeniu MPD CURL rozpocząć C++ relay.
5. Zbudować resolver metadanych bez pobierania mediów.
6. Dodać ograniczony bufor RAM i test back-pressure zanim podłączymy prawdziwy YouTube.
7. Podłączyć MPD queue manager, a następnie MALP.
8. Przeprowadzić testy RAM-only, regresję i testy sprzętowe.
9. Zaktualizować audyt wynikami i dopiero rozważyć scalanie.

## 16. Rejestr dowodów i ograniczeń tego audytu

### Potwierdzone przez inspekcję kodu

- Istnieje producent FIFO Bash dla pojedynczego URL.
- Istnieje instalator yt-dlp i testy offline z atrapą programu.
- Istnieją klasy Android parsera, klienta HTTP i preferencji.
- MainActivity wykonuje żądanie HTTP w executorze, ale jawnie informuje, że dodawanie do kolejki nie jest jeszcze dostępne.
- Manifest obsługuje ACTION_SEND, nie ACTION_SEND_MULTIPLE.
- Główny README i dokument Yoinks-MPD opisują FIFO, podczas gdy nowszy plan zawiera docelowy wariant HTTP.
- Nie znaleziono serwera Pi implementującego endpoint Androida ani implementacji MPD queue managera w sprawdzonych plikach.

### Zgłoszone wcześniej, ale nieodtworzone niezależnie

Dokument audytu prototypu zapisuje, że użytkownik uruchomił na Raspberry Pi trzy polecenia testujące składnię Bash i sześć przypadków testowych FIFO, które zakończyły się powodzeniem. Jest to dowód zgłoszony przez użytkownika, nie niezależnie odtworzony wynik w tym środowisku. Test nie potwierdza kompatybilności wejścia MPD ani odtwarzania YouTube.

### Nie wykonano w ramach tego audytu

- kompilacji Gradle;
- testów Androida na emulatorze/urządzeniu;
- kompilacji C++ (serwis C++ jeszcze nie istnieje);
- testu CURL input na docelowym Raspberry Pi;
- odtwarzania pojedynczego utworu i playlisty;
- pomiarów pamięci i testu swap;
- testów śledzenia zapisów na trwałe nośniki;
- niezależnego testu bezpieczeństwa sieciowego.

**Werdykt końcowy:** architektura jest wykonalna pod warunkiem potwierdzenia MPD CURL input i poprawnej implementacji relay. Obecny kod nie spełnia jeszcze celu użytkowego. Najrozsądniejsza droga to najpierw dowód zgodności MPD, następnie C++ HTTP relay z ograniczonym buforem RAM, potem manager kolejki i na końcu pełna integracja Androida. Bezpieczeństwo RAM-only musi być mierzone na systemie docelowym, włącznie ze swap, a nie wywodzone wyłącznie z braku plików w katalogu roboczym.


## Aktualizacja wykonania etapów — 2026-11-17

### W0 — ujednolicenie dokumentacji

**Status: wykonano po stronie repozytorium.** Zaktualizowano główny README, dokument Yoinks-MPD, plan kolejnych prac, instrukcję instalacji Raspberry Pi oraz indeks obszaru roboczego. Jednoznacznie wskazano lokalny HTTP relay w C++ i wejście CURL MPD jako architekturę docelową. FIFO pozostało prototypem producenta i nie jest przedstawiane jako wejście MPD.

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
