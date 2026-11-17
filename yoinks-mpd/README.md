> **Status update 2026-11-17:** The integrated C++ bridge, resolver, lazy sessions, MPD queue adapter and Android client are present in this branch. The user has confirmed audible playback on the Raspberry Pi through the existing Yoinks-MPD API → local proxy URI → MPD path. This confirms that specific API playback flow, not direct YouTube URL insertion by MALP. A playlist watcher for MALP-originated MPD entries is not implemented yet; its safety constraints and acceptance gates are recorded in [the current architecture audit](../work/youtube-audio-bridge/AUDYT_ARCHITEKTURY_I_PLAN_REALIZACJI.md). The historical implementation-status notes below predate the current integration and must not be read as the current state. See also the [installation guide](../scripts/yoinks-mpd/INSTALL.md).

# Yoinks-MPD C++ service core

This directory is the initial C++17 core for the Raspberry Pi HTTP bridge.

## Current implementation

- CMake build for a dependency-light C++17 core.
- `BoundedStreamBuffer`: fixed-capacity ring buffer allocated in RAM.
- Producer back-pressure: writes block when the consumer has not freed capacity.
- Consumer reads available bytes progressively.
- EOF drain via `close()`; immediate discard and wake-up via `cancel()`.
- Unit tests for zero capacity, wraparound, bounded reads, producer back-pressure, close/drain and cancellation.
- GitHub Actions workflow builds with warnings-as-errors and runs CTest.

The buffer does not open files, create temporary paths, spool media, or grow dynamically after construction. It is a primitive for a future HTTP relay, not the relay itself.

## Build and test

```sh
cmake -S yoinks-mpd -B yoinks-mpd/build -DCMAKE_BUILD_TYPE=Release
cmake --build yoinks-mpd/build --parallel
ctest --test-dir yoinks-mpd/build --output-on-failure
```

## Not implemented yet

- Bounded HTTPS upstream producer to RAM, random stream-session registry and local chunked HTTP endpoint `/stream/{opaque-id}`.
- Control API routing and Bearer token verification.
- Wiring the yt-dlp resolver into the queue API and playlist expansion policy.
- MPD protocol queue manager.
- Resolver-driven source URL refresh remains pending; sessions can currently be created only by trusted internal code.
- systemd service, resource limits and Raspberry Pi installation.
- MPD CURL compatibility and real device playback.

Do not expose this core as a working bridge until these components and integration tests exist. The only permitted media transport remains the local HTTP proxy. No FIFO, named pipes, media files or disk-backed cache are part of this implementation.


## HTTP API bootstrap status

A minimal Boost.Beast HTTP server is built as `yoinks-mpd`. It exposes an unauthenticated health endpoint and a Bearer-protected queue endpoint that intentionally returns HTTP 503 until the resolver and MPD queue manager are integrated. The server enforces a 16 KiB request-body limit, an 8 KiB header limit and socket read/write deadlines. It binds to loopback by default. Control requests use a fixed four-thread pool; media streaming is dispatched to a separate two-thread pool, so long-lived streams do not occupy control workers. This remains a development component, not a deployable bridge: there is no TLS termination, no trusted queue-to-session creation path, and media is not yet enqueued.


The server also validates YouTube URLs on the Raspberry Pi: HTTPS only, exact host allowlist, no userinfo or fragments, and strict video/playlist identifier and path checks. Client-side validation is not trusted as the security boundary.

The server requires a 43–128 character base64url bearer token (minimum 256-bit entropy) and compares credentials over a fixed 128-byte loop. Generate tokens from a cryptographically secure random source; do not use human passwords.

## HTTPS upstream relay component — implementation status

Added `HttpUpstreamStream`, a C++17 producer that uses libcurl to fetch media progressively and writes callback chunks into the fixed-capacity `BoundedStreamBuffer`. The default RAM buffer is 256 KiB and the per-stream byte ceiling is 512 MiB. Back-pressure blocks the libcurl write callback until the consumer frees RAM; cancellation wakes a blocked writer and aborts the transfer.

Security constraints: HTTPS only, exact `*.googlevideo.com` host boundary, standard port 443 only, no URL userinfo, TLS peer/host verification enabled, redirects disabled, connect timeout and low-speed timeout enabled. Redirects are deliberately not followed because they could escape the CDN host policy; an expired or redirected source must be resolved again by the trusted yt-dlp resolver. Upstream URLs are never returned to Android or logged.

The producer is connected to a local chunked HTTP response endpoint through `StreamSessionRegistry`. The endpoint sends progressive 16 KiB chunks with bounded RAM/back-pressure and does not support Range (501) or HEAD (405). It emits the terminating HTTP chunk only after successful upstream completion; failed/incomplete streams are closed without a false successful EOF. Sessions are erased after stream completion or failure. The endpoint is not yet reachable through the normal queue flow: the resolver and trusted session-creation path are not implemented. The end-to-end bridge is not ready.


## HTTP server concurrency and exposure policy

The control server uses a fixed four-thread worker pool and caps accepted/queued connections at 16; excess connections receive 503. A separate two-thread pool handles stream responses, with a hard cap of two active streams. The listener rejects every non-loopback bind address until a separately reviewed TLS termination design exists. The stream executor is separate so a slow media client cannot consume control-plane worker capacity.


## Bounded stream-session registry

Added `StreamSessionRegistry` with a default maximum of four sessions (hard cap 32). Each session owns one `HttpUpstreamStream` and therefore one fixed-size RAM buffer; aggregate buffer allocation is hard-capped at 8 MiB across all sessions, including non-default configurations. Session IDs are 128-bit values obtained from the Linux kernel `getrandom(2)` interface and encoded as 32 lowercase hexadecimal characters. The registry does not expose source URLs and supports lookup, cancellation/removal and bulk shutdown. It is not yet wired to an MPD-facing HTTP route or the queue API; no playable URI is issued at this stage.


## Local HTTP stream endpoint — current limitations

`GET /stream/{32-char-lowercase-hex-id}` looks up an existing trusted in-memory session and streams its upstream bytes using HTTP/1.1 chunked transfer encoding. Unknown IDs return 404; unsupported methods return 405; Range returns 501 with `Accept-Ranges: none`. At most two streams run concurrently in the dedicated stream pool. The current integration tests cover route rejection and unknown sessions, not a successful end-to-end media stream. The queue API still fails closed with 503 because no resolver, queue manager or trusted session-creation path exists. Do not expose the service beyond loopback or treat it as production-ready.


## Deterministic chunked-transfer tests

The HTTP endpoint and its unit-tested `writeChunkedStream` helper share the same chunk-framing implementation. Tests cover progressive output in bounded 16 KiB blocks, successful terminal-chunk emission, omission of the terminal chunk after upstream failure, and immediate stop after a socket-write failure. These tests validate framing and producer/consumer control flow without creating a production session-injection endpoint. They do not replace a live MPD/CURL playback test.


## Server shutdown

The listener uses asynchronous accept and handles SIGINT/SIGTERM through Boost.Asio. Shutdown closes the listener, drains control workers so no additional stream tasks can be submitted, cancels all registered upstream sessions to wake blocked RAM-buffer readers, and then joins the dedicated stream workers. The HTTP integration test exercises SIGTERM shutdown. This lifecycle path still requires successful CI and device-level verification.


## yt-dlp resolver component — integration pending

A C++17 `YtDlpResolver` component now launches the absolute yt-dlp executable with `posix_spawn` and an argv vector; it never invokes a shell. Metadata mode uses `--skip-download`, `--flat-playlist`, `--playlist-end` (hard-capped at 200), `--dump-single-json`, `--ignore-config` and `--no-cache-dir`. Direct audio URL mode uses `--get-url` and `--skip-download`, then accepts only HTTPS URLs within the `googlevideo.com` host boundary. Both modes have process timeouts, process-group termination, bounded stdout/stderr and non-sensitive error codes; signed URLs and stderr are not logged.

The resolver is a tested library component, not yet connected to `POST /api/v1/queue` or the session registry. The queue endpoint must continue returning 503 until resolver/session lifecycle and MPD queue management are integrated. Unit tests use temporary fixture executables and do not contact YouTube or download media.


## Resolver cancellation and argument-policy checks

The resolver now accepts an optional caller-owned `std::atomic_bool` cancellation flag for metadata and direct-URL resolution. When set, the resolver terminates the complete yt-dlp process group, reaps the child and returns the non-sensitive `process_cancelled` code for metadata requests. The flag must remain alive for the duration of the synchronous call. Existing timeout and bounded-output protections remain in force. Unit fixtures also assert that both resolver modes pass `--ignore-config`, `--no-cache-dir` and `--skip-download`, and that URL mode passes `--get-url`.

This is a library-level cancellation primitive only: the HTTP request lifecycle does not yet own or propagate cancellation to resolver calls. Queue API remains fail-closed with HTTP 503.

## Resolver descriptor isolation

Before spawning yt-dlp, resolver pipe descriptors are moved above file descriptors 0–2. This prevents `posix_spawn` file actions from accidentally closing or overwriting stdin/stdout/stderr when the parent service starts with one or more standard descriptors closed. A fork-based regression test closes all three standard descriptors in the child and verifies metadata resolution still succeeds. No media file is created.

## MPD queue manager — isolated implementation

A loopback-only `MpdQueueManager` has been added as an isolated component. It accepts only exact local proxy URIs (`http://127.0.0.1:<stream-port>/stream/<32-char-opaque-id>`), connects only to a loopback MPD endpoint, and appends entries with `addid` so stable MPD song IDs are used. Enqueue mode does not issue playback commands. `play_now` issues `playid` for the first newly-added entry only when explicitly requested. The component never calls `clear` and never changes repeat, random, single or consume modes.

Known successful additions are rolled back with `deleteid` if MPD explicitly rejects a later add or the explicit `playid`. Ambiguous transport outcomes are reported as unknown and are not destructively rolled back, because the server may already have accepted the command or started playback. The component is not yet connected to the HTTP queue endpoint. It must pass mock-protocol tests and then a controlled Raspberry Pi MPD/CURL compatibility test before deployment.

## Lazy playlist sessions

`LazyStreamSessionRegistry` stores only a validated 11-character YouTube video ID and a random opaque session capability when a playlist item is registered. It does not invoke yt-dlp, resolve a CDN URL, start a media transfer, or allocate an audio buffer during queue construction. The resolver is called only when the corresponding `/stream/<opaque-id>` request activates that item.

The registry is bounded to 200 queued session records and at most two simultaneously activating/active upstreams. Aggregate active RAM buffers are capped at 8 MiB. Concurrent activation of the same item is single-flight. Erasing a session or clearing the registry signals a shared cancellation flag to the resolver and cancels any active upstream stream.

This registry is currently an isolated component with unit tests; the HTTP queue endpoint and stream handler have not yet been switched to it. The queue endpoint remains fail-closed (`503 backend_not_ready`) until the complete resolver → lazy registry → MPD queue flow is integrated and validated.
## Status integracji — aktualizacja 2026-11-17

Przebieg GitHub Actions dla commita `dc5d174d2413cf5c0dc2f635c5ed8694184ba6f3` zakończył się powodzeniem. Test resolvera sprawdza anulowanie `yt-dlp` podczas rozwiązywania bezpośredniego adresu audio: proces ma zakończyć się przed timeoutem, a wynikowy URL pozostać pusty.

Resolver, `LazyStreamSessionRegistry` i `MpdQueueManager` są zaimplementowanymi i testowanymi komponentami bibliotecznymi. Nie są jednak jeszcze połączone z serwerem HTTP. Serwer nadal używa starego `StreamSessionRegistry`, endpoint `/stream/{id}` nie aktywuje lazy sesji, a `POST /api/v1/queue` celowo zwraca `503 backend_not_ready`. `control_ready` pozostaje `false`.

Zaliczenie CI nie oznacza testu na Raspberry Pi ani potwierdzenia zgodności konkretnej wersji MPD/CURL. Usługa nie jest jeszcze gotowa do odtwarzania end-to-end.

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
