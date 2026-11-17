# Yoinks-MPD Control API — kontrakt v1

Status: **specyfikacja W2; serwer nie jest jeszcze zaimplementowany.**

## 1. Zasady

- Control API steruje bridge i kolejką MPD. Endpoint mediów jest odrębny i służy wyłącznie MPD.
- Android przekazuje wyłącznie zweryfikowany adres YouTube i wybrany tryb. Nie przekazuje adresów CDN ani URI strumienia.
- Serwer rozwiązuje wszystkie pozycje przed modyfikacją kolejki. Błąd resolve nie może pozostawić częściowej playlisty.
- Odpowiedź sukcesu oznacza potwierdzone wykonanie operacji kolejki przez MPD, a nie samo rozpoznanie metadanych.
- Żadne audio ani podpisane adresy CDN nie są utrwalane. Identyfikatory sesji i URI lokalne są przechowywane w RAM.
- Limit rozmiaru żądania: 16 KiB. Limit playlisty: 100 pozycji (konfigurowalny, nigdy większy niż 200). Limit czasu resolve: 30 s; limit całej operacji: 60 s.

## 2. Transport i uwierzytelnianie

- JSON kodowany UTF-8, Content-Type: application/json.
- Wszystkie endpointy sterujące wymagają Authorization: Bearer <token>.
- Token ma co najmniej 256 bitów entropii; porównanie stałoczasowe.
- API nasłuchuje na wybranym interfejsie prywatnym. Poza zaufanym LAN wymagane HTTPS.
- Nie logować nagłówka Authorization, treści tokenu, pełnego URL YouTube z parametrami ani URL CDN.
- Błędy nie mogą ujawniać ścieżek systemowych, argv procesów ani sekretów.

## 3. Endpointy

### GET /api/v1/health

Bez uwierzytelnienia, bez sekretów. Odpowiedź:

~~~json
{"status":"ok","service":"yoinks-mpd","api_version":1}
~~~

Health nie deklaruje gotowości MPD ani dostępności YouTube; te informacje mogą być osobnymi polami po wdrożeniu kontroli zależności.

### POST /api/v1/queue

Wymaga tokenu. Dodaje jedną pozycję lub playlistę do istniejącej kolejki.

Żądanie:

~~~json
{"url":"https://www.youtube.com/watch?v=VIDEO_ID","mode":"enqueue"}
~~~

Dozwolone wartości mode: enqueue, play_now.

Odpowiedź 200 po potwierdzeniu MPD:

~~~json
{
  "success": true,
  "request_id": "opaque-request-id",
  "title": "Playlist title",
  "playlist": true,
  "queued": 3,
  "started": false,
  "items": [
    {"id":"opaque-item-id","title":"Track title","artist":"Channel name","duration":123}
  ]
}
~~~

Pola items nie zawierają stream_url, URL CDN, bezpośrednich adresów źródła ani danych uwierzytelniających. Wewnętrzne lokalne URI są dodawane do MPD przez usługę. started=true oznacza, że MPD potwierdził rozpoczęcie pierwszej pozycji w trybie play_now.

### GET /api/v1/requests/{request_id}

Wymaga tokenu. Zwraca stan operacji: resolving, queued, playing, failed, cancelled. Wersja pierwsza może realizować POST /queue synchronicznie; endpoint statusu pozostaje zarezerwowany dla późniejszej obsługi zadań długotrwałych.

### DELETE /api/v1/requests/{request_id}

Wymaga tokenu. Anuluje wyłącznie zasoby i nieodtwarzane pozycje przypisane do danego żądania. Nie usuwa bieżącego ani obcego elementu kolejki i nie zatrzymuje całego MPD. Jeśli pozycja jest aktualnie odtwarzana, odpowiedź musi jawnie wskazać, że nie została przerwana.

## 4. Semantyka kolejki

- enqueue: dodaj wszystkie pozycje żądania na końcu kolejki, zachowując ich kolejność; nie zmieniaj bieżącego utworu.
- play_now: dodaj wszystkie pozycje żądania jako jeden blok na początku kolejki przed dotychczasowymi pozycjami (bez usuwania istniejących pozycji), następnie uruchom pierwszą pozycję tego bloku przez jej MPD song ID.
- Nie używaj clear, nie zmieniaj single, consume, random ani repeat.
- Każdy wynik addid przypisz do request ID. Przy błędzie częściowego dodawania usuń wyłącznie ID dodane w ramach tej operacji.
- Operacje kolejki wykonuj przez protokół MPD na dedykowanym połączeniu, z poprawnym cytowaniem argumentów. Nigdy przez shell.
- Nie obiecuj pełnej atomowości, której protokół MPD nie zapewnia; zastosuj sekwencję z rollbackiem i zgłoś błąd rollbacku oddzielnie.

## 5. Walidacja i błędy

- Akceptuj wyłącznie HTTPS i ścisłą allowlistę domen YouTube: youtube.com, www.youtube.com, m.youtube.com, music.youtube.com, youtu.be. Odrzucaj userinfo, porty niestandardowe, hosty podobne i nieprawidłowe ścieżki/identyfikatory.
- Nie przyjmuj od klienta URL upstream. Resolver może zwrócić wyłącznie HTTPS źródła zatwierdzonego przez ekstraktor yt-dlp; proxy musi ponownie sprawdzić schemat i host.
- Kody: 400 nieprawidłowe żądanie; 401 brak/błędny token; 413 przekroczony rozmiar; 422 niedozwolony URL lub nierozwiązywalne źródło; 429 limit; 502 błąd upstream; 503 MPD/usługa niedostępna; 504 timeout.
- JSON błędu: {"success":false,"error":{"code":"invalid_url","message":"Unsupported URL"}}. Komunikat jest bezpieczny i nie zawiera sekretów.

## 6. Kontrakt endpointu mediów

- MPD otrzymuje stabilne lokalne URI http://127.0.0.1:8787/stream/{opaque_item_id} (adres i port konfigurowalne).
- Endpoint mediów nie akceptuje URL upstream ani funkcji sterujących.
- Obsługa GET/HEAD/Range, statusów i ponownego połączenia zostanie ustalona na podstawie rzeczywistego zachowania dokładnego builda MPD/CURL na Raspberry Pi. Nie zakłada się obsługi HEAD lub Range bez pomiaru.
- Bajty są przekazywane progresywnie, w ograniczonych buforach RAM, z back-pressure. Brak plików tymczasowych, cache i buforów dyskowych.
- Identyfikatory są losowe, nieprzewidywalne i wygasają po zakończeniu/zwolnieniu sesji.

## 7. Android — wymagane dopasowanie

Klient Android wysyła POST /api/v1/queue i nie otrzymuje stream_url ani lokalnych URI MPD. Token jest przechowywany w postaci szyfrogramu z kluczem chronionym przez Android Keystore.

## 8. Testy kontraktowe

- poprawne enqueue pojedynczego filmu i playlisty;
- play_now bez czyszczenia istniejącej kolejki;
- błędny token, brak tokenu, zły host, userinfo, niestandardowy port;
- pusta i zbyt duża playlista;
- timeout resolvera i MPD;
- błąd MPD po dodaniu części pozycji i weryfikacja rollbacku;
- odpowiedzi nie zawierają CDN URL ani stream_url;
- limity długości żądania, czasu, liczby pozycji i współbieżności;
- logi bez tokenów i podpisanych adresów.


## Implementacja serwera — status

Dodano wstępny serwer HTTP C++ oparty na Boost.Beast i nlohmann-json. Implementuje GET /api/v1/health, uwierzytelnianie Bearer z porównaniem stałoczasowym, limit body 16 KiB, limit nagłówków 8 KiB oraz timeout odczytu/zapisu. POST /api/v1/queue poprawnie waliduje kształt JSON i tryb, ale zwraca 503 backend_not_ready, dopóki nie zostaną zintegrowane resolver, proxy i manager MPD. Jest to zachowanie fail-closed; endpoint nie udaje, że dodał pozycję.

Serwer nasłuchuje domyślnie wyłącznie na 127.0.0.1:8788. Nie wystawiać go do LAN bez dodatkowej warstwy TLS i przeglądu konfiguracji sieciowej. Obsługa żądań jest synchroniczna per połączenie na stałej puli czterech wątków, z limitem 16 aktywnych/zleconych połączeń. Brak TLS i endpointu strumieniowego nadal wyklucza wdrożenie produkcyjne.

Serwer dodatkowo waliduje URL po stronie Raspberry Pi: wymaga HTTPS, dokładnego hosta z allowlisty, odrzuca userinfo i fragmenty oraz sprawdza identyfikatory filmu/playlisty i dozwolone ścieżki. Nie ufa walidacji wykonanej wyłącznie przez klienta Android.

The server requires a 43–128 character base64url bearer token (minimum 256-bit entropy) and compares credentials over a fixed 128-byte loop. Generate tokens from a cryptographically secure random source; do not use human passwords.



## Strumień upstream — częściowa implementacja

Dodano komponent C++ `HttpUpstreamStream` oparty na libcurl. Pobiera dane progresywnie do `BoundedStreamBuffer` w RAM (domyślnie 256 KiB), z limitem 512 MiB na sesję i back-pressure. Dopuszczony jest HTTPS na domenach `*.googlevideo.com`, port 443, bez userinfo. Weryfikacja certyfikatu i nazwy hosta jest włączona. Przekierowania są wyłączone — resolver musi odnowić źródło zamiast przekazywać transfer do niezweryfikowanej domeny. Anulowanie przerywa transfer i budzi producenta zablokowanego na buforze.

Komponent nie jest jeszcze połączony z endpointem lokalnego proxy HTTP. Nie udostępnia MPD żadnego URI i nie zmienia kolejki. Testy walidacji URL nie wykonują połączeń internetowych.


Serwer HTTP korzysta obecnie ze stałej puli czterech wątków i ogranicza liczbę aktywnych/zleconych połączeń do 16; nadmiar otrzymuje 503. Nasłuch poza loopback jest blokowany do czasu wdrożenia i przeglądu TLS. Długie odpowiedzi mediów będą wymagały osobnej, ograniczonej puli, aby nie zagłodzić API sterującego.


## Rejestr sesji strumieniowych — komponent wewnętrzny

Dodano `StreamSessionRegistry`: domyślnie maksymalnie cztery sesje (twardy limit konfiguracji 32). Każda sesja posiada jeden producent HTTPS i stały bufor RAM; łączna alokacja buforów jest ograniczona do 8 MiB. Identyfikatory sesji mają 128 bitów losowości z systemowego `getrandom(2)`, są kodowane jako 32 znaki szesnastkowe. Rejestr obsługuje wyszukiwanie, anulowanie, usuwanie i czyszczenie. Nie jest jeszcze podłączony do endpointu MPD ani API kolejki; nie generuje obecnie URI do odtwarzania.


## Aktualny stan implementacji i korekty kontraktu (2026-11-17)

Poniższa sekcja zastępuje wcześniejsze oznaczenia „serwer nie jest jeszcze zaimplementowany” oraz historyczne opisy odpowiedzi 503 backend_not_ready.

### Implementacja

- `POST /api/v1/queue` jest podłączony do `YtDlpResolver`, `YoutubeQueueService`, `LazyStreamSessionRegistry` i `MpdQueueManager`.
- Metadane są rozwiązywane przed zmianą kolejki; audio nie jest pobierane podczas operacji enqueue.
- Rejestr tworzy lokalne URI dla maksymalnie 200 pozycji. Adresy CDN są rozwiązywane dopiero przy `GET /stream/{opaque-id}`.
- MPD dostaje wyłącznie `http://127.0.0.1:<port>/stream/<opaque-id>`.
- Odpowiedź sukcesu obecnej wersji zawiera `success`, `queued`, `started`, `playlist` i `rollback_complete`. `request_id`, `title` i `items` nie są obecnie emitowane; klient Androida nie wymaga `request_id`.
- `started=true` wyłącznie po potwierdzeniu komendy `playid` przez MPD.
- `enqueue` dopisuje nowe URI na końcu kolejki.
- `play_now` wstawia cały nowy blok przed dotychczasową kolejką, zachowując oryginalną kolejność: wykonuje `addid URI 0` w odwrotnej kolejności, a następnie `playid` dla pierwszej pozycji z żądania. Nie usuwa istniejących pozycji.
- Nie są wysyłane komendy `clear` ani zmiany trybów repeat/random/single/consume.
- Nieznany identyfikator sesji daje 404; błąd resolvera 502; limit aktywacji lub zajęta sesja 503. Błąd połączenia z MPD 503, timeout resolvera 504, błędny/nieobsługiwany wynik metadanych 422, pozostałe błędy upstream 502.
- Klient Androida odczytuje odpowiedź zgodną z aktualnym kontraktem; nie wymaga nieistniejącego pola `request_id`.

### Ograniczenia i nierozstrzygnięte kwestie

- Endpoint mediów aktualnie obsługuje GET; HEAD daje 405, Range daje 501. Zgodność z MPD/CURL nie została potwierdzona na docelowym Raspberry Pi.
- Nie ma jeszcze pozytywnego testu pełnego HTTP → mock MPD → odpowiedź 200. Warstwy `YoutubeQueueService` i `MpdQueueManager` mają osobne testy, a test HTTP przechodzi przez resolver fixture do kontrolowanego błędu niedostępnego MPD.
- `GET /api/v1/requests/{request_id}` i `DELETE /api/v1/requests/{request_id}` pozostają niezaimplementowane.
- `play_now` ma test jednostkowy potwierdzający wstawianie bloku przed starą kolejką; nie jest to jeszcze test na konkretnym buildzie MPD.
- Testy CI po poprawie semantyki sesji: run `44060542148` — build i CTest zakończone sukcesem. Nowsze zmiany kolejności play-now i mapowania statusów HTTP wymagają własnego zakończonego przebiegu CI.
- Wdrożenie produkcyjne i odtwarzanie na Raspberry Pi pozostają zablokowane do czasu testu MPD/CURL na urządzeniu oraz przeglądu bezpieczeństwa.

Nie traktować samego przejścia CTest jako potwierdzenia zgodności sprzętowej ani działającego odtwarzania end-to-end.


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
