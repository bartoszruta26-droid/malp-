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

Serwer nasłuchuje domyślnie wyłącznie na 127.0.0.1:8788. Nie wystawiać go do LAN bez dodatkowej warstwy TLS i przeglądu konfiguracji sieciowej. Implementacja jest synchroniczna i obsługuje jedno połączenie naraz — nie jest jeszcze gotowa do wdrożenia produkcyjnego.

Serwer dodatkowo waliduje URL po stronie Raspberry Pi: wymaga HTTPS, dokładnego hosta z allowlisty, odrzuca userinfo i fragmenty oraz sprawdza identyfikatory filmu/playlisty i dozwolone ścieżki. Nie ufa walidacji wykonanej wyłącznie przez klienta Android.

The server requires a 43–128 character base64url bearer token (minimum 256-bit entropy) and compares credentials over a fixed 128-byte loop. Generate tokens from a cryptographically secure random source; do not use human passwords.



## Strumień upstream — częściowa implementacja

Dodano komponent C++ `HttpUpstreamStream` oparty na libcurl. Pobiera dane progresywnie do `BoundedStreamBuffer` w RAM (domyślnie 256 KiB), z limitem 512 MiB na sesję i back-pressure. Dopuszczony jest HTTPS na domenach `*.googlevideo.com`, port 443, bez userinfo. Weryfikacja certyfikatu i nazwy hosta jest włączona. Przekierowania są wyłączone — resolver musi odnowić źródło zamiast przekazywać transfer do niezweryfikowanej domeny. Anulowanie przerywa transfer i budzi producenta zablokowanego na buforze.

Komponent nie jest jeszcze połączony z endpointem lokalnego proxy HTTP. Nie udostępnia MPD żadnego URI i nie zmienia kolejki. Testy walidacji URL nie wykonują połączeń internetowych.
