# YouTube → Yoinks-MPD → yt-dlp → MPD — remaining work

Date: 2026-11-17

## Goal

Finish the existing `feature/youtube-audio-bridge` implementation so MALP can share YouTube video and playlist URLs to a Raspberry Pi that streams audio into the user's existing MPD daemon. Preserve ordinary MALP/MPD control and avoid writing complete audio files to storage.

## Current audited state

- The Android application contains the YouTube share parser/client path and CI unit/build coverage; the latest Android emulator smoke-test state must be checked against its own current workflow run before distributing an APK.
- The Raspberry Pi side now contains the C++ HTTP bridge, authenticated control API, yt-dlp resolver, lazy local stream sessions, bounded RAM buffering, and MPD queue integration.
- The MPD queue stores stable local proxy URIs; playlist entries are expanded into separate ordered items. Integration tests use an isolated mock MPD and deterministic fixtures.
- The latest registry-lock fix is commit `2f40a5f8f3c5fc438d0462e9b44961126bbee8dd`. The user built it on the Raspberry Pi and reported **11/11 CTest tests passing**. GitHub Actions run `44104070635` for the exact commit also succeeded.
- The production service remained active and its binary hash unchanged during this verification. The test build was not installed and the production service was not restarted.
- Still unverified: a live YouTube/CDN stream on the target Pi, audible playback through the actual audio output, long-duration RAM-only behavior including swap/storage tracing, and a deterministic test of the narrow post-start/pre-publication cancellation race.

## MPD CURL compatibility gate

The sole target architecture is a local C++ HTTP proxy consumed by MPD's CURL input plugin. Named pipes and local media files are excluded from the design. Before implementing the relay, verify the exact MPD build on the target Raspberry Pi and demonstrate playback of a known-good HTTP audio URI without changing the production queue. Use scripts/yoinks-mpd/diagnose-mpd-http.sh and docs/yoinks-mpd/MPD_HTTP_INPUT_DIAGNOSTIC.md. If CURL input is absent, stop and decide explicitly whether to rebuild MPD with CURL support or implement a native C++ HTTP input module. Do not substitute named pipes.

## Requirements for the implementation

1. Pi-side integration / orchestration must use Bash or C++ only. Do not introduce Python scripts, services, build steps, or runtime dependencies.
2. Android remains the existing Java MALP application.
3. Stream source audio; do not save complete audio files.
4. Use stable local stream URIs in MPD's queue, not expiring YouTube CDN URLs.
5. Expand playlists into independent, ordered MPD queue entries. Never concatenate multiple tracks into one audio stream.
6. Support enqueue and play-now modes without clearing or interrupting the current queue unless explicitly requested.
7. Handle pause/resume, seek where supported, next, stop, disconnect, cancellation, EOF, and producer cleanup.
8. Apply strict HTTPS YouTube host validation, process/time/playlist/concurrency limits, and a separate bridge token. Never expose MPD credentials in YouTube arguments or API requests.
9. Keep the chosen source codec whenever supported. Do not label lossy-to-FLAC conversion lossless.
10. Repair MainActivity imports and implement ACTION_SEND/ACTION_SEND_MULTIPLE share handling, background network execution, safe result handling, and UI error reporting.
11. Add configuration for bridge URL/token and document local network/TLS deployment.
12. Update the audit with actual test evidence. Never mark hardware/build tests as passed unless they have actually run.

## Acceptance gates

- Android Gradle unit tests and debug assembly succeed.
- Bash syntax and offline integration tests succeed.
- Exact MPD build input compatibility is recorded.
- A real single YouTube item plays on Raspberry Pi.
- A playlist plays in original order as independent tracks.
- MALP continues to control existing local music, volume, outputs, and queues.
- Stop/skip/disconnect release producer processes and resources.
- No complete audio files are created.
- Final documentation differentiates repository-side tests, user-reported Pi tests, and independently verified target-device tests.

## GitHub repository configuration note

GitHub Issues are disabled for this repository (create-issue API returned HTTP 410), so this file records the work plan directly on the existing feature branch instead of creating an issue.


## Additional upstream comparison (2026-11-17)

### pbnoxious/yt2mpd

Inspected `README.md`, `pyproject.toml`, `yt2mpd/config.py`, `yt2mpd/yt2mpd.py`, and its sample configuration.

- GPL-3.0-or-later.
- Uses yt-dlp to extract audio and download/convert it into a temporary subdirectory inside MPD's music directory.
- Runs `mpc update`, then adds resulting local files with `mpc add`.
- Supports video URLs and playlist URLs; playlist items become separate downloaded files.
- Optional cleanup/pruning removes temporary files after they leave the queue.
- Not a streaming solution: it stores full converted audio files before adding them to MPD.
- Uses Python, explicitly conflicting with this project's Pi-side Bash/C++ constraint.
- Has unsafe shell construction in `os.system('mpc add "{}"'.format(filename))` and a shell=True query path; must not be copied as-is.
- The metadata parser depends on yt-dlp human-readable stdout and is brittle.
- Useful only as a behavioral reference for playlist expansion, queue insertion, MPD database update, and cleanup. Do not adopt its download-to-library implementation.

### Nuffic/youtube-mpd

Inspected `README.md`, `package.json`, `Dockerfile`, `docker-compose.yml`, `src/index.ts`, `src/player/MpdPlayer.ts`, `src/player/MpdStatus.ts`, and `src/eventListener.ts`.

- TypeScript/Node.js implementation based on `yt-cast-receiver` and `mpd-api`.
- Receives YouTube Cast / YouTube Music Cast sessions (the README explicitly says it is not a generic receiver for arbitrary apps).
- Extracts a video audio URL using `yt-dlp --get-url`, then adds that URL directly to MPD's queue. This is the closest of the two projects to direct, no-full-file streaming.
- The MPD instance is configured as a single renderer; its implementation clears the queue and enables single/consume behavior. This is unsuitable as-is for MALP's shared general-purpose MPD queue and could disrupt ordinary playback.
- It constructs shell commands with `child_process.exec` and interpolates the video ID. Replace with `spawn`/argument arrays and strict video-ID validation if reusing the approach.
- The direct CDN URL is time-limited and stored in MPD's queue. There is no robust local URL-refresh proxy in the inspected code; queued items can fail after URL expiry.
- Docker example uses host networking and bundles MPD plus receiver, while this project must integrate with an already configured Raspberry Pi MPD and its audio output.
- Repository metadata/package references point to `dgalli1/ytm-mpd`; confirm upstream lineage and license obligations before copying implementation code. The inspected package declares ISC; verify the exact repository license file before code reuse.

### Architecture decision after comparison

Use a hybrid design, not a direct copy of either project:

1. Keep the existing MALP Android share/bridge flow as the user interface.
2. On Raspberry Pi, resolve YouTube IDs and metadata with yt-dlp using machine-readable output (`--dump-single-json` / `--flat-playlist` where appropriate), never parse human-readable progress output.
3. Expand playlists into separate ordered MPD entries.
4. Use a local HTTP streaming proxy on Raspberry Pi as the stable MPD URI. The proxy resolves/refreshes expiring upstream CDN URLs and streams bytes with back-pressure; it must not persist complete audio files.
5. Configure and verify MPD's CURL input support for the local HTTP URI. This verifies the only supported bridge media path: local HTTP proxy to MPD. Keep the local endpoint restricted to loopback/LAN and protect control endpoints with a token.
6. Preserve normal MPD queue semantics. Do not clear the queue, enable global consume/single mode, or take over playback unless the user explicitly selected play-now.
7. For the required Bash/C++ Pi-side constraint, implement the proxy/service and process supervision in C++ (or a carefully constrained Bash launcher); yt-dlp remains an external executable dependency. Do not introduce a Python daemon or Node.js runtime.
8. Treat Nuffic's direct URL approach as a prototype/reference only; the local stable-URI proxy is required for long queues and URL expiry.
9. Treat pbnoxious's playlist and cleanup workflow as behavioral reference only; do not download whole audio files.

### Revised test gates

- Confirm MPD was built with CURL input enabled (for example, inspect MPD build features and test an HTTP URI on the exact target build).
- Test one YouTube item through local proxy into MPD without creating a complete audio file.
- Test a playlist with ordered, independent queue entries and verify that ordinary local tracks remain intact.
- Simulate CDN URL expiry and verify transparent re-resolution while the local MPD URI remains unchanged.
- Test pause/resume/seek/next/stop and queue behavior against the existing MPD instance.
- Test service authentication, loopback/LAN binding, process limits, cancellation, and cleanup.


## Hard requirement: RAM-only, real-time playback (confirmed 2026-11-17)

The user explicitly requires music to be played in-flight. No complete audio file may be written to persistent storage. RAM buffering is permitted; disk, SSD, SD-card, USB-storage, and persistent-cache buffering is forbidden.

This is a hard acceptance requirement, not an optimization.

### Required runtime behavior

- yt-dlp is used only for metadata / stream URL resolution (e.g. machine-readable JSON and URL extraction). Do not use its download-to-file mode.
- Stream audio bytes from YouTube through a bounded in-memory buffer to the local HTTP endpoint consumed by MPD.
- Never create temporary audio files, converted audio files, segment files, playlist downloads, or persistent cache entries.
- Disable yt-dlp cache (for example, pass `--no-cache-dir` where applicable); do not set a persistent cache directory.
- Do not use `--extract-audio`, `--output`, `-o`, or any postprocessor that writes media files.
- Avoid shell pipelines that can spill to disk. Use C++ process APIs with argv arrays / pipes, not shell interpolation.
- Keep only bounded buffers in RAM; apply back-pressure so slow MPD consumption cannot cause unbounded memory growth.
- Playlist expansion resolves each item independently and queues separate local stream URIs; it does not download playlist media.
- On stop, next, client disconnect, failure, or service shutdown, cancel the producer and release RAM buffers, sockets, and child processes.
- Do not persist upstream signed CDN URLs or authentication tokens in MPD's database, logs, or config. MPD queue entries should contain only local stable proxy URIs.
- Do not log full signed media URLs or request credentials. Logs may contain sanitized video IDs, status, and error classes.
- Review Linux swap/zram policy on supported Raspberry Pi images. zram is RAM-backed and acceptable; disk-backed swap can write pages to persistent storage and must be disabled or otherwise proven not to persist media buffers if the user's no-disk requirement is to be guaranteed.

### RAM and storage verification tests

1. Run the bridge with a dedicated temporary directory and assert that no audio/media files are created anywhere on persistent mount points.
2. Trace file opens/writes by yt-dlp, the bridge, and MPD during playback; reject writes of media payloads to persistent storage.
3. Verify yt-dlp cache is disabled and no postprocessor/download path is invoked.
4. Monitor resident memory during a long track and playlist; prove buffer usage remains under a configured bound.
5. Simulate a slow MPD reader and verify producer back-pressure rather than buffer growth or disk spill.
6. Stop playback and verify producer processes, sockets, and buffers are released.
7. Test a long playlist and ensure only metadata / small queue state is retained, not audio payloads.
8. Record swap configuration in the Raspberry Pi installation audit; distinguish RAM-backed zram from disk-backed swap.

### Architecture clarification

The local HTTP proxy is a streaming relay, not a download manager. It must stream bytes progressively from the upstream YouTube CDN to MPD with a bounded RAM buffer. A design that first downloads a complete item and then serves it over HTTP is explicitly rejected.


## Post-audit continuation — 2026-11-17 (repository review)

### Verified repository state
- Feature head reviewed: `be8f2176fc696c628567674a7b957e7204b001cb`.
- Latest Yoinks-MPD workflow run `44066634026`: success (configure, build, CTest, installer shell syntax and systemd unit validation).
- Latest Android workflow run `44064310295`: success (Gradle debug assemble and APK artifact upload).
- These are repository CI results, not Raspberry Pi playback or Android emulator evidence.
- The feature branch and `master` have diverged (feature ahead by 333 commits, master ahead by 7). Do not merge the entire branch without a dedicated reconciliation/rebase plan. The master tree contains no feature-only paths, while the feature branch carries the Android bridge, Yoinks-MPD service, CI and audit documents.

### Current technical blockers confirmed by source inspection
1. `GET /api/v1/health` currently returns the literal `"control_ready":false`, even though the queue service is constructed and wired in `main.cpp`. Health must report dependency-aware readiness, not a hard-coded value. It should separately identify process liveness, control-plane readiness and media/MPD compatibility; a healthy HTTP process does not prove MPD playback.
2. The Android manifest still sets `android:usesCleartextTraffic="true"`. The new bridge client requires HTTPS, but global cleartext remains enabled for the legacy application. Review actual legacy HTTP use and replace the global allowance with a narrowly scoped Network Security Config exception only if required; retain HTTPS-only bridge validation and add a regression test that bearer credentials are never sent over cleartext.
3. The installer documentation `docs/yoinks-mpd/INSTALL_RASPBERRY_PI.md` is stale: it says the installer provisions only yt-dlp and does not create the proxy/systemd service, while the current `scripts/yoinks-mpd/install.sh` builds, installs and enables the C++ service. Bring docs into alignment before handing the installer to an operator.
4. The current Yoinks-MPD CI workflow compiles/tests C++ and checks installer shell syntax and a transformed systemd unit, but does not execute the installer in a disposable Debian/Raspberry Pi-like root environment. Add safe shell-level tests for idempotence, token preservation, --no-start, --restart gating and failure cleanup without touching a host MPD or systemd service.
5. Android build CI creates a debug APK artifact for 14 days; emulator/instrumentation tests and a release-signed artifact are not yet evidenced.

### Next implementation sequence
- W1: add explicit readiness model and deterministic tests for health states; health must not return `control_ready=true` until configured dependencies are constructed, and must not imply live MPD/CURL playback.
- W2: test and narrow Android cleartext network policy without breaking existing MPD profile connections.
- W3: reconcile Raspberry Pi install guide and the root bridge audit with the current installer behavior.
- W4: add isolated installer tests and expand CI.
- W5: add HTTP contract success-path tests with mock yt-dlp and mock MPD, then run MPD/CURL compatibility tests on the target Pi in a safe isolated instance.
- W6: Android share-sheet instrumentation/emulator tests, including ACTION_SEND, malformed and multiple links, saved URL/token, HTTPS rejection, redirect rejection, enqueue/play_now and activity recreation.
- Keep the hard requirement: audio payload only in bounded RAM buffers; no media file or persistent audio cache.
- Keep production MPD queue and audio configuration untouched until the user explicitly runs isolated target-device tests.


### Follow-up execution record — 2026-11-17
- Updated `yoinks-mpd/src/http_server.cpp`: health now reports `control_ready=true`, `media_ready=false`, and `mpd_playback_verified=false` as three distinct facts. This deliberately avoids claiming media/MPD playback readiness from process startup.
- Updated `yoinks-mpd/tests/http_api_test.sh` to assert all three readiness fields.
- Updated this continuation audit with CI results and remaining work.
- Rewrote `docs/yoinks-mpd/INSTALL_RASPBERRY_PI.md` to match the actual current installer: C++ build + CTest, yt-dlp checksum verification, restricted account, root-owned 0600 token environment, hardened systemd unit, loopback-only server, and optional HTTPS reverse proxy.
- CI run `44074016126` failed because the integration test still asserted the old `control_ready=false` value after the health behavior changed. The code compiled successfully; the failure was a stale test assertion.
- The subsequent integration test run `44074047254` passed all nine CTest tests. This validates the corrected test suite and new health fields in CI.
- Current branch head after these changes: `810972ee27fc9cd46877b7e54f307150faf04118`.
- No Raspberry Pi playback test, TLS deployment test, Android emulator test, or production MPD queue operation was performed in this work increment.


### Android multiple-share handling — 2026-11-17
- Added an `ACTION_SEND_MULTIPLE` text/plain intent filter to the Android manifest.
- `MainActivity` now safely extracts YouTube URLs from the shared text list, de-duplicates identical links, and accepts exactly one distinct valid YouTube URL. Multiple distinct links continue to show the existing explicit unsupported-multiple-links message; no batch queue insertion or implicit playback was added.
- Added parser tests for multiple-link extraction order, duplicate removal, ignoring invalid links, null/empty input, userinfo rejection and non-standard port rejection.
- Commits: `e3d8309c27735910607e0fc6aa96fcf5cced93de` (activity), `d26d38cab407e46fa27432808919989efbe4c30d` (manifest), `bc54a4f4e74c6306690d63a8d4cbc00085b75eb4` (tests).
- CI runs `44074441626`, `44074465965` and `44074498551` were launched for the three consecutive Android commits. Their outcomes must be inspected separately; a later queued run is the complete-tree verification.
- Legacy global cleartext HTTP remains unchanged pending a complete audit of MALP's existing MPD HTTP and artwork endpoints. The YouTube bridge itself continues to enforce HTTPS and rejects redirects.


### Android ACTION_SEND_MULTIPLE CI correction — 2026-11-17
- First Android CI runs for the change failed at `compileDebugJavaWithJavac`: `MainActivity` used `ArrayList` without importing it.
- Added `java.util.ArrayList` to `MainActivity.java` in commit `db00d37f5257711073e44f88c7e4a661d287bba1`.
- A fresh Android CI run `44074664171` is validating that corrected source. Do not mark the Android change verified until this run reaches a terminal result with successful APK assembly/artifact upload.


## Audyt kontynuacji — 17.11.2026 (stan po zmianach)

### Stan commitów i kontroli
- Gałąź robocza: `feature/youtube-audio-bridge`; nie scalano jej z `master` ze względu na rozbieżność historii (333 commitów po stronie feature i 7 po stronie master w chwili porównania).
- Test C++/HTTP po rozdzieleniu gotowości: run `44074047254` zakończony sukcesem; wszystkie 9 testów CTest przeszło.
- W aktualizacji Android ACTION_SEND_MULTIPLE pierwsze przebiegi (`44074441626`, `44074465965`, `44074498551`) wykazały brak importu `java.util.ArrayList` w `MainActivity`. Poprawiono to w commicie `db00d37f5257711073e44f88c7e4a661d287bba1`.
- Run Android `44074664171` buduje commit `db00d37f5257711073e44f88c7e4a661d287bba1`; w momencie zapisu tej aktualizacji job był nadal aktywny w kroku `Assemble debug APK`. Nie oznaczać tego przebiegu jako sukcesu, dopóki API GitHub nie poda końcowego `success` oraz kroku `Upload debug APK` zakończonego powodzeniem.
- Instrukcja Raspberry Pi została zaktualizowana tak, aby odpowiadała obecnemu instalatorowi (C++ service + yt-dlp + systemd), bez deklarowania nieprzeprowadzonych testów sprzętowych.

### Zmiany funkcjonalne Androida
- Dodano filtr manifestu `ACTION_SEND_MULTIPLE` dla `text/plain`.
- `MainActivity` odczytuje listę udostępnionych tekstów, wydobywa dozwolone HTTPS URL YouTube, usuwa duplikaty i przyjmuje dokładnie jeden unikalny link. Dwa różne linki nadal kończą się czytelnym komunikatem; brak niejawnego dodawania wielu utworów.
- Dodano testy parsera: kolejność, deduplikacja, ignorowanie złych URL-i, puste/null dane, userinfo i porty spoza domyślnego.

### Wciąż otwarte bramki
1. Zakończyć i sprawdzić build Androida po imporcie `ArrayList`; przejrzeć APK i artefakt z tego samego SHA.
2. Dodać testy emulatorowe/intentowe `ACTION_SEND` i `ACTION_SEND_MULTIPLE` (sama kompilacja nie dowodzi poprawnego zachowania Android share sheet).
3. Zweryfikować dokładnie strukturę URL Bridge w ustawieniach oraz kliencie (HTTPS, host, port domyślny/niestandardowy, userinfo, query, fragment, ścieżka reverse proxy), zachowując zgodność z rzeczywistymi wdrożeniami.
4. Audyt `usesCleartextTraffic=true`: nie usuwać globalnej zgody bez prześledzenia legacy MPD/artwork HTTP. Dążyć do jawnych wyjątków domenowych i HTTPS dla nowego API, gdy zgodność starego klienta będzie potwierdzona.
5. Wdrożenie Nginx/TLS i testy na Raspberry Pi są niezweryfikowane. C++ CI nie potwierdza kompatybilności MPD/CURL, HEAD/Range/seek, reconnect ani odtwarzania.
6. Potrzebny mock MPD testujący ścieżkę sukcesu HTTP → resolve → enqueue, obecne testy HTTP pokrywają fail-closed na niedostępnym MPD.
7. Do czasu testu na izolowanej instancji MPD nie zmieniać produkcyjnej kolejki, mpd.conf ani aktywnego strumienia.

### Zasada odbioru
Nie publikować APK jako „gotowego do instalacji” na podstawie samego CI. Wymagane: końcowy green build dla aktualnego SHA, artefakt APK z tego SHA, test instalacji/uruchomienia na emulatorze oraz test funkcji mostu na docelowym środowisku. Sekrety tokenu Bearer nie mogą trafiać do logów ani do publicznych artefaktów.


### Follow-up — ACTION_SEND_MULTIPLE payload compatibility and test coverage (17.11.2026)
- Reviewed the Android share handler against the actual Android Intent contract. The previous implementation assumed `EXTRA_TEXT` was always an `ArrayList<CharSequence>` for `ACTION_SEND_MULTIPLE`; senders may provide a single `CharSequence` or an iterable list. That assumption could silently ignore valid shares or cause a type-cast failure.
- Replaced the typed extra getter with safe inspection of `Bundle.get(Intent.EXTRA_TEXT)`. A single `CharSequence` and an `Iterable` of text payloads are now supported; unsupported payload types are ignored. Existing one-distinct-link-only behavior is preserved.
- Added an iterable overload to `YouTubeShareParser` that merges URLs in encounter order, de-duplicates them, and ignores non-text elements.
- Added unit cases for multiple text payloads, duplicate links, non-text values and null iterable input.
- Added JUnit 4.13.2 and Robolectric 4.13.2 test dependencies, enabled Android resources in local unit tests, and annotated the parser suite with `RobolectricTestRunner`.
- Extended `.github/workflows/android-build.yml` to run `:app:testDebugUnitTest` before `:app:assembleDebug`. Earlier Android CI only assembled the APK and did not execute the unit test suite.
- Commits: parser `35d4e890a8cfa02c449f72b7ec13761b51907948`; activity `9c4ff8eda34343870b02415a24c6f534abe484e0`; tests `f674aa623c0ddb9c468cad22719c71388a38d187`; Robolectric configuration `6856443764464c85db690392e84c2372490d8b18` and `d12afc0a5645a0526b508cb02f8363833968a39f`; CI test step `e55c3d4cc3dfdf74deef8b88478ffb57b5a7ef6f`.
- Verification run `44078827460` targets the workflow with the new unit-test step and the full accumulated tree. At the time this note was written, the run was still in progress before the unit-test step. Do not claim unit tests or the latest APK are green until this run completes successfully.
- Existing successful debug APK artifact `14325425019` belongs to the earlier SHA `db00d37f5257711073e44f88c7e4a661d287bba1`; it is not the latest branch build and must not be presented as the current APK.


### CI correction — remove unnecessary Robolectric dependency (17.11.2026)
- Android workflow run `44078827460` completed with failure during `:app:compileDebugUnitTestJavaWithJavac`: Gradle could not resolve `org.robolectric:robolectric:4.13.2` from the configured dependency repositories. The test task did not execute.
- Reviewed `YouTubeShareParserTest`: it tests a pure Java URL parser and does not instantiate Android framework classes or require Android resources. Robolectric was therefore unnecessary overhead.
- Removed the Robolectric runner annotation/imports and dependency, retaining JUnit 4.13.2 and the parser test cases. Removed the now-unneeded Android-resource unit-test option.
- Commits: test runner removal `2aeb106939d394c953dd4f9b0d995028664296ff`; Gradle dependency cleanup `61a872ae5cc8485e4e238ca262a68e56ad05ea4f`.
- A new CI run should be triggered by the `app/build.gradle` update. Await its terminal result; do not mark tests or APK as verified until unit tests, APK assembly and artifact upload all succeed for the same head SHA.


### Parser refactor for deterministic JVM tests — 17.11.2026
- Follow-up code review found that `YouTubeShareParser` still depended on Android `Uri` and `TextUtils`; plain JUnit execution would therefore encounter Android stub methods even after removing Robolectric.
- Refactored URL validation to use `java.net.URI`, standard Java URL decoding and null/empty checks, removing Android framework dependencies from the parser. Malformed URI syntax is rejected safely.
- Renamed the iterable overload to `extractYouTubeUrlsFromPayloads` to avoid ambiguous Java overload resolution for null inputs, updated the activity and tests, and corrected iterable test call sites.
- Commits: pure-Java parser `a362185c6117741405f2378c71e64a119984c62c`; activity call-site `f6662565610019227b9ccc89c82206d82c64e25e`; tests `2b6ea64986a73134a7f4200239e734df92623cc1`; malformed URI handling `110a5b7b50216d1e3f8ca111b06622b750f4aa1a`; iterable test correction `dfdfec97f10084a1f432448a03132cb25f624d0b`.
- Multiple intermediate CI runs were triggered by sequential GitHub contents commits. The authoritative verification must be the run for the final code SHA `dfdfec97f10084a1f432448a03132cb25f624d0b` (or a later code SHA), after all unit-test, assemble and artifact-upload steps finish successfully.


### Android CI green and emulator smoke-test gate — 17.11.2026
- Android CI run `44079384696` completed successfully for code SHA `dfdfec97f10084a1f432448a03132cb25f624d0b`. Verified job steps: `:app:testDebugUnitTest`, `:app:assembleDebug`, and upload of `malp-debug-apk` all succeeded.
- Artifact: `malp-debug-apk`, artifact ID `14327070567`, 5,998,673 bytes, SHA-256 `6d8cbfffd66b6c28ef99599994c0bc066a6bf9ad71d98677d745976b865834dd`. GitHub associates it with run `44079384696` and head SHA `dfdfec97f10084a1f432448a03132cb25f624d0b`. It is a debug artifact, expires 2026-12-01; it is not a release-signed APK and was not yet emulator-verified at the time of this entry.
- Added a dedicated Android emulator smoke-test step to the workflow in commit `654b4d632b51ad290da8dec2fd923e303d305ba6`: install APK on API 34 emulator, launch MainActivity, invoke explicit ACTION_SEND and ACTION_SEND_MULTIPLE intents with a valid test URL, and fail on AndroidRuntime fatal exceptions. This is a smoke test, not a substitute for full UI/instrumentation assertions or Pi/MPD playback.
- A new workflow run should be generated for the workflow change. Verify its complete terminal result before considering emulator smoke testing passed. Do not present artifact `14327070567` as the APK built from the newer workflow commit unless a matching new run uploads a new artifact.
- The historical note above that share handling was missing reflects an earlier audit snapshot; subsequent commits added the handler and the current implementation is included in the green unit-test/build run. Emulator verification is now being added separately.


### Emulator smoke-test correction — 17.11.2026
- Workflow run `44080403049` completed with unit tests and debug APK assembly successful, but the emulator smoke-test step failed before installation.
- The failure was in the workflow harness, not an Android runtime crash: the smoke-test script could not find the APK at its relative path after the emulator action started. Emulator boot completed, but `adb install` and intent checks were never reached.
- Updated the workflow to resolve the APK using the absolute `$GITHUB_WORKSPACE` path and emit a workspace APK search diagnostic if the file is absent. Commit: `32bd484895ee80764e99a1a741f895f170c0f370`.
- Await the new run. The emulator test is only green if the step reaches install, launches MainActivity, executes both share intents, and completes the fatal-exception check. Artifact upload must also succeed for the same head SHA.


### Przegląd ścieżki HTTP i buforowania — 17.11.2026
- Przegląd `http_api_test.sh` potwierdza testy zdrowia API, autoryzacji, walidacji trybu i URL, limitu treści, współbieżnych zapytań oraz zamykania procesu. Test żądania kolejki z poprawnymi metadanymi kończy się jednak oczekiwanym `503`, ponieważ MPD jest celowo ustawiony na zamknięty port. Ten test potwierdza fail-closed, ale nie dowodzi sukcesu pełnego przepływu HTTP → yt-dlp → sesja → MPD.
- `mpd_queue_manager_test.cpp` ma osobne testy z serwerem MPD mock, w tym dopisywanie pozycji i tryb play-now. Nie zastępują one integracji warstwy HTTP z działającym mock MPD.
- Następny etap testów: dodać deterministyczny scenariusz sukcesu HTTP z mock yt-dlp i izolowanym mock MPD, sprawdzający kod 200, liczbę dodanych wpisów, kolejność playlisty i brak niezamierzonego polecenia `playid` dla trybu `enqueue`. Mock ma nasłuchiwać wyłącznie na loopback i używać osobnego portu; żadnego połączenia z produkcyjnym MPD.
- Potwierdzono w kodzie ograniczenie buforowania RAM: rejestr sesji ogranicza liczbę aktywnych strumieni do maksymalnie 2, a łączna pojemność buforów upstream jest ograniczona do 8 MiB. `yt-dlp` uruchamiany jest z `--no-cache-dir` i `--skip-download`; audio jest pobierane przez libcurl do ograniczonego bufora pamięciowego.
- Nadal wymagają osobnych testów: rzeczywisty odczyt strumienia HTTP na podstawie kontrolowanego upstream, zachowanie po zerwaniu klienta i ponownej aktywacji, wygaśnięcie URL i ponowne rozwiązanie, a także brak plików audio/cache na dysku podczas rzeczywistego odtwarzania.

### Android emulator CI correction — 2026-11-17
- Run `44083669237` completed with failure. Android unit tests and `:app:assembleDebug` both passed; emulator smoke-test did not pass, so APK artifact upload was skipped.
- Job log shows repeated `adb: device offline` while the emulator was starting. The final smoke script also failed with `Syntax error: end of file unexpected (expecting "fi")`, because the action's script execution did not preserve the multi-line shell conditional as expected.
- Corrected `.github/workflows/android-build.yml` in commit `747c442bcea207cd52b414b12598a5a476d833cc`: run the smoke sequence as one explicit Bash invocation, wait for ADB device availability and boot-complete state, enable headless software-rendered emulator options, and disable animations.
- New CI run: `44085605457`, associated with commit `747c442bcea207cd52b414b12598a5a476d833cc`. At audit update time it was still in progress. Do not mark emulator testing as passed or distribute an APK from this run until the job succeeds and the artifact is present.
- The previous successful APK artifact `44079384696` predates the emulator smoke-test correction and is not the latest verified build. Do not present it as the current installable release.
- No Raspberry Pi runtime test, production MPD modification, TLS deployment, or successful YouTube playback test was performed.

 
## HTTP → yt-dlp → proxy → MPD integration-test expansion — 2026-11-17

- Added a dedicated C++ mock MPD fixture bound only to loopback and used only by CI integration tests. It records MPD commands and returns deterministic MPD protocol replies; it is not part of the Raspberry Pi service or installer.
- Replaced the previous HTTP integration test's expected closed-port 503 with a successful control-plane path: authenticated HTTP queue request → deterministic yt-dlp metadata fixture → lazy opaque local stream URI → MPD playlistinfo reconciliation → MPD addid.
- The test asserts HTTP 200, one queued item, and that MPD receives only a local http://127.0.0.1:<port>/stream/<opaque-id> URI; YouTube/CDN URLs must never be inserted into MPD.
- The test then requests the queued local stream URI. The yt-dlp fixture is invoked in --get-url mode and deliberately returns a disallowed host; the proxy must reject activation with 502. This verifies lazy resolver invocation and the CDN host security gate without internet access or media downloads.
- This is not yet a successful media-byte relay test: no valid HTTPS googlevideo upstream is served by the fixture, and no MPD CURL playback is claimed. The next test increment must provide a deterministic, TLS-verified upstream seam and verify actual chunk bytes through the proxy, while retaining production host validation.
- No Android source or APK was changed or built. No production MPD instance, queue, configuration, or Raspberry Pi was touched.


### Verification and playlist-order extension — 2026-11-17

- CI run 44088216821 completed successfully for commit 3aa13095955893c49d77a20034ae8821eecfb894. C++ build succeeded and all 9 CTest cases passed, including the new HTTP-to-mock-MPD integration test.
- The follow-up commit db9d722a3f9c0ac08576c9569a27dc1dff781782 added explicit standard-library includes to the mock MPD fixture; its C++ workflow run 44088253147 also completed successfully.
- The HTTP integration fixture has now been expanded from one video to a three-item playlist. It verifies three independent local MPD stream URIs and requests each in the same order, checking that yt-dlp is invoked with the corresponding video IDs in that order.
- The playlist-order extension is committed as bad818262fc004f02231c9964a50ea9861bbccce. Its CI result is pending at the time of this audit update.
- Media-byte relay is still not proven: the stream test deliberately uses an invalid CDN host to exercise rejection. The next acceptance test must exercise a successful TLS-verified upstream response and confirm the exact bytes reach the local HTTP stream consumer. Hardware MPD playback remains unverified.


### Successful byte-stream fixture and CI test correction — 2026-11-17

- Added a narrowly scoped C++ test transport injection to HttpUpstreamStream. Production main does not configure this override; normal operation continues to use libcurl with HTTPS/TLS verification, redirects disabled, the googlevideo host allowlist and bounded RAM buffering.
- Added a separate HTTP stream fixture server that wires the real YtDlpResolver, LazyStreamSessionRegistry, YoutubeQueueService, MpdQueueManager and HttpServer together with the isolated mock MPD.
- The fixture supplies deterministic bytes in 7-byte producer chunks. Its shell integration test sends an authenticated YouTube queue request, obtains the opaque local MPD URI, requests /stream/{id}, and compares the exact returned HTTP chunked body with the expected payload.
- CI run 44089142175 built this fixture and reported http_stream_byte_integration PASS. The other HTTP playlist integration test failed at that revision because its playlist-count assertion was malformed; this is a test assertion failure, not a C++ build failure.
- The playlist assertion and resolver-fixture argument parsing were corrected in subsequent commits. The latest workflow run for the corrected revision must pass before the whole C++ suite is marked green.
- The byte fixture validates the local producer → bounded RAM buffer → HTTP chunked response path. It does not prove a real libcurl TLS session to YouTube/googlevideo or playback by MPD on the Raspberry Pi. Those remain separate acceptance gates.


### Final C++ CI verification for the current test set — 2026-11-17

- Corrected the HTTP playlist test's shell quoting/count assertion and added failure diagnostics. The test now verifies three unique MPD proxy URIs and resolves their three video IDs in playlist order.
- CI run 44089667826 completed successfully for commit efeaa1335a1ec7dca590a6d755cecedef4fed62e: installer/shell checks, CMake configure, C++ build, and all 10/10 CTest cases passed.
- The 10 tests include both `http_api_integration` (HTTP metadata/playlist → yt-dlp fixture → lazy proxy sessions → MPD mock) and `http_stream_byte_integration` (HTTP queue → MPD mock → lazy resolver → bounded RAM buffer → HTTP chunked stream, with exact payload comparison).
- The byte-stream test uses an explicit test-only transport override. Production leaves it unset and uses libcurl. Therefore the successful CI result proves the C++ proxy and chunked HTTP data path with deterministic fixture bytes, not a real TLS connection to Google's CDN.
- Still outstanding: successful real libcurl/TLS retrieval from a valid googlevideo URL, target Raspberry Pi MPD CURL-input compatibility, and audible playback verification. No production MPD configuration or queue has been changed.


### Real libcurl TLS → HTTP byte-stream integration — 2026-11-17

- Replaced the earlier fake upstream transport override with the normal libcurl path. The test-only configuration now supplies a temporary CA bundle and a CURLOPT_CONNECT_TO mapping; production configuration leaves both unset.
- The integration test creates a short-lived self-signed test certificate with the exact hostname rr1.googlevideo.com, starts a local OpenSSL HTTPS file server on an unprivileged port, and has the yt-dlp fixture return an HTTPS URL on that hostname.
- libcurl connects to the local TLS fixture while retaining the original rr1.googlevideo.com URL host for SNI and certificate hostname verification. The HTTP endpoint streams the fixture file as chunked HTTP; the test compares the received bytes exactly.
- CI run 44090166568 completed successfully for commit 2d0176dd18407ce5995c6a4defdefc0a247bc75b. C++ compilation succeeded and all 10/10 CTest tests passed, including playlist ordering, authenticated HTTP queueing, actual libcurl TLS verification, and byte-for-byte proxy output.
- This verifies the HTTP → yt-dlp fixture → HTTPS/libcurl → bounded RAM buffer → local chunked HTTP path and MPD queue insertion against a mock MPD. It does not yet verify a live YouTube/googlevideo response or playback on the Raspberry Pi's MPD daemon.
- Test-only CA and connect-to settings are internal configuration fields and are not read from environment variables by the production executable. Only the dedicated test fixture server supplies them.


### Real MPD daemon playback integration — 2026-11-17

- Added `tests/http_mpd_daemon_test.sh` and registered `real_mpd_http_proxy_playback` in CTest. CI installs the distribution MPD daemon and `mpc` client; no production MPD instance or user queue is touched.
- The test starts a private MPD daemon bound to loopback with a null audio output and temporary database/state/playlist files. It starts a temporary HTTPS fixture whose certificate SAN is `rr1.googlevideo.com`, and a deterministic yt-dlp executable that resolves a local WAV fixture.
- The normal libcurl path validates TLS using the temporary CA bundle and test-only CURLOPT_CONNECT_TO mapping. The bridge creates a local opaque HTTP proxy URI, and the real MPD daemon is asked to play it via `play_now`.
- CI run `44090982855`, commit `cd03e24c81db60e2e017ac640fc2374754ad592b`, completed successfully: build passed and all 11/11 CTest tests passed. In particular, `real_mpd_http_proxy_playback` passed, confirming MPD entered `playing` for the proxied WAV stream and its playlist contained the loopback proxy URI rather than a CDN URL.
- A subsequent hardening change in commit `177643772e25d5dd0af8708bff54df8db1d645e9` makes the real-daemon test fail early unless the installed MPD build advertises its CURL input plugin. CI run `44091039138` is validating that exact latest branch head; its result is pending at the time of this audit update.
- This is a real MPD daemon playback test, but still uses deterministic local HTTPS/WAV fixtures and a fake yt-dlp executable. Live YouTube resolution/CDN retrieval, target Raspberry Pi MPD build compatibility, real audio output hardware and audible playback remain unverified.
- No production MPD configuration, service, playlist, queue, or Android APK was modified or deployed.


### Follow-up: MPD CURL capability guard and manual live smoke script — 2026-11-17

- The real-daemon test had already passed 11/11 in run `44090982855`, but the additional capability guard in run `44091039138` failed because `grep -q` closed the `mpd --version` pipe early under Bash `pipefail`. The MPD output itself listed `curl` among the supported URI/input features; this was a shell-check false negative, not a missing MPD plugin.
- Corrected the check by capturing the complete `mpd --version` output before searching it. Applied the same correction to the opt-in live smoke script.
- Added `scripts/yoinks-mpd/live-smoke-test.sh`: an explicit, non-root manual test that starts an isolated MPD instance on a free loopback port with a null output, starts a separate bridge process against that MPD, submits one supplied YouTube URL using the installed yt-dlp, and checks MPD playback plus the local proxy URI. Temporary MPD state/logs are removed at exit; no production MPD queue is accessed and no media file/cache is downloaded.
- Documented the manual command in `scripts/yoinks-mpd/INSTALL.md`. The live script is syntax-checked in CI but is not run automatically because it requires external YouTube access and a user-supplied URL.
- Latest commits for the guard correction and live test are pending CI verification; do not mark the latest branch head green until that workflow finishes.


### Live smoke script cleanup and latest CI gate — 2026-11-17

- Corrected the live smoke script after its first CI syntax check exposed stale duplicated fragments left by an earlier edit. The file now contains one complete execution path and one final success report.
- Corrected URL handling: the script rejects quotes, backslashes and control characters before embedding the user-supplied URL in its JSON request.
- The real-daemon test and live smoke script now capture the complete `mpd --version` output before checking for `curl`; this avoids Bash `pipefail`/SIGPIPE false negatives from `grep -q`.
- Latest branch revision at this update: `76277cea960f1fcd10ab9809ec68c95476f1def5`. CI run `44092051129` is in progress; dependency installation is still running and shell syntax/build/CTest steps have not yet completed.
- Prior confirmed real MPD run `44090982855` passed 11/11 tests, including real daemon playback from the deterministic local WAV fixture. The later capability-guard revision failed due to the pipefail check and is being revalidated; do not treat that failed run as a product playback failure.
- The opt-in live test is documented in `scripts/yoinks-mpd/INSTALL.md`. It has not been run against a live YouTube source or on the target Raspberry Pi. Production MPD and its queue remain untouched.


## Progress update — 2026-11-17: registry lock fix verified on Raspberry Pi

- Feature branch head: `2f40a5f8f3c5fc438d0462e9b44961126bbee8dd`.
- The user pulled and built the revision on the Raspberry Pi; all **11/11 CTest tests passed** in 6.70 seconds.
- GitHub Actions run `44104070635` for the same exact commit completed successfully.
- The production `yoinks-mpd` service remained active with zero restarts. The production binary hash was unchanged; no installation or restart was performed.
- The fix moves upstream cancellation and worker join outside the session-registry mutex. It is verified, but the exact post-start/pre-publication cancellation race still lacks a deterministic dedicated regression test.

### Next implementation sequence

1. Design a deterministic test seam for the post-start/pre-publication cancellation race without introducing a production-configurable network override or external network dependency. Test erase/clear/retain behavior and verify unrelated registry operations remain responsive while an upstream worker is being joined.
2. Review lifecycle interactions among `activate()`, `erase()`, `clear()`, `release()`, and `retainOnly()`; add focused tests for cancellation, slot accounting, retry, and shutdown.
3. Complete the upstream URL/DNS threat review. Current URL validation restricts HTTPS URLs to `googlevideo.com` and subdomains, rejects userinfo and non-443 ports, disables redirects, and enables TLS peer/hostname verification. Explicit DNS-resolved IP-range policy is not yet implemented; assess whether the deployment threat model requires it and choose a design that preserves TLS hostname verification.
4. Exercise slow consumers/back-pressure, client disconnect, upstream timeout/failure, byte-limit enforcement, reconnect/re-resolution, and service shutdown.
5. Keep live YouTube/CDN retrieval, target Raspberry Pi MPD CURL support, audible playback, and RAM-only/no-persistent-storage verification as separate acceptance gates. Do not install the test binary or restart the production service without explicit approval.


## In-progress lifecycle fix — release during activation (2026-11-17)

- New commits: `25030068` (implementation), `5fb5d0fa` (regression test), `36d9f70a` (deterministic cancellation wait in test).
- `release(id, true)` now cancels an in-flight resolver and does not clear the activation reservation prematurely. A subsequent retry receives a fresh cancellation token.
- Regression test covers cancellation delivery, rejection of overlapping activation while the first resolver is still blocked, and successful acquisition of a fresh token on retry.
- GitHub Actions and Raspberry Pi build/CTest verification are pending. Do not install or restart production service.


## Lifecycle race: release after upstream start, before publication

- Added a deterministic test seam and regression test for `release(id, true)` in the interval after upstream worker start but before the stream is published to the session entry.
- The discard path now joins the cancelled unpublished worker before releasing the session's activation reservation, and only clears the reservation when the same session/cancellation token still exists.
- Commits: `b854b2f2`, `06aead23`, `5c202ecd`.
- Pending gates: GitHub Actions success for the latest branch head; Raspberry Pi build; targeted lazy-session test; full CTest suite; verify production service remains untouched and binary hash unchanged.
- Next lifecycle cases after this gate: `erase()`/ `clear()` during post-start/pre-publication, and `retainOnly()`/slot accounting while a stream is being published. Then continue slow-consumer/back-pressure, shutdown, and DNS/SSRF review.

- CI run `44107862029` for `5c202ecd` completed successfully (configure/build/test). Raspberry Pi execution remains the next required verification gate.

## Lifecycle verification update — 2026-11-17

- Completed and verified post-start/pre-publication race tests for `erase()` and `clear()` in commit `7914ae676fc6f646d87853807353d6dff8cf5987`.
- CI run `44108836398` passed for that exact commit. Raspberry Pi build, targeted lazy-session test, and full CTest also passed (11/11, 5.49 s).
- Production service and binary were left unchanged; service active, zero restarts, original binary hash retained.
- Remaining lifecycle work:
  1. Add a deterministic `retainOnly()` test for the post-start/pre-publication interval, verifying that an in-flight activation is retained or pruned according to the intended live-ID/active-reservation contract.
  2. Audit slot accounting across concurrent activate/release/erase/clear/retainOnly operations, including repeated activation and retry after cancellation.
  3. Test registry destruction/shutdown while activation is paused after upstream start; verify worker cancellation/join and absence of use-after-free.
  4. Then continue back-pressure, slow consumer/client disconnect, upstream timeout/error, reconnect/re-resolution, byte limits, and DNS/SSRF review.
- Keep live YouTube/CDN retrieval, target MPD CURL capability, audible hardware playback, and RAM-only/no-persistent-storage verification as separate acceptance gates. Do not deploy or restart the production service without explicit approval.

### Additional lifecycle test — verified

- Added `testRetainOnlyPreservesUnpublishedActivationAndSlotAccounting()`; its final corrected form uses `retainOnly({second_id})`. The initial empty live-ID set legitimately pruned the inactive second session, so the original expectation of zero removals was a test-design error, not an implementation defect.
- Final test commit: `6e880cb9e5a15014a5e3e290bb019548147d23ba`.
- GitHub Actions run `44118536311` passed for that exact commit.
- The user confirmed a successful Raspberry Pi build, targeted lazy-session test, and full CTest **11/11 passed** in 5.17 seconds. The checkout was clean and HEAD matched origin.
- This gate is complete. Do not infer live YouTube/CDN or audible hardware playback from fixture-based integration tests.

## Current verification state — 2026-11-17

- Current tested feature commit: `bffc9c91b15741f9282bc9ae77b7a28ae53929f8`.
- GitHub Actions run `44120703708`: success for the exact commit; configure, build, and test passed.
- Raspberry Pi: build succeeded; targeted lazy-session test passed; full CTest **11/11 passed** in 5.09 seconds; clean working tree.
- Coordinated shutdown test passed: shutdown rejects new creates, waits for an in-flight activation paused after upstream start/before publication, and returns after that activation discards and joins its unpublished upstream.
- The shutdown barrier counts in-flight `activate()` calls. The owner must stop dispatching other public registry operations before object destruction. Do not treat this API as protection against concurrent destruction through a raw object reference.
- Production service and installed binary were not changed by this test cycle.

## Next implementation sequence

1. **Review shutdown integration and operation ownership:** inspect all registry call sites and prove that no control-request worker remains when shutdown begins, and that stream-worker ownership keeps the registry alive until `stream_workers.join()` completes. Add focused tests for shutdown racing with `release()`, `erase()`, `clear()`, and `retainOnly()` only where their object-lifetime contract is valid. Consider whether a general public-operation guard is warranted or whether server-level quiescence is sufficient.
2. **Sanitizer/lifecycle stress:** run AddressSanitizer/UndefinedBehaviorSanitizer where supported, repeat deterministic lifecycle tests, and add bounded stress coverage for concurrent activate/release/erase/clear/retainOnly transitions and slot accounting.
3. **Streaming robustness:** exercise slow consumers/back-pressure, client disconnect, upstream timeout/error, byte-limit enforcement, reconnect/re-resolution, and orderly server shutdown.
4. **URL/DNS/SSRF review:** inspect DNS-resolved private, loopback, and link-local address handling while preserving TLS hostname verification; add tests before changing resolution/pinning behavior.
5. **Separate acceptance gates:** verify live YouTube/CDN retrieval on Raspberry Pi, actual MPD CURL-input compatibility, audible hardware output, and RAM-only/no-persistent-storage behavior. These are not established by the current fixture-based tests.

No production binary installation or service restart without explicit user approval.


## Lifecycle verification update — shutdown versus registry mutation (2026-11-17)

- Commit `63bf4b8aaba114a56949f4d3424ce472e9147eb3` adds deterministic races between shutdown and `release()`, `erase()`, `clear()`, and `retainOnly()` while an upstream worker is started but unpublished.
- GitHub Actions run `44122267302` passed for the exact commit; intermediate commit `5d6d7dd5` also passed run `44122205972`.
- User-reported Raspberry Pi build succeeded; targeted lifecycle test passed; full CTest **11/11 passed** in 5.36 seconds; working tree clean.
- The test keeps the registry alive until shutdown and all racing threads have joined. It does not claim safety for destroying an object while another thread calls a method through a raw reference.
- The targeted shutdown/mutation test gate is complete. No production binary or service was changed.

## Next implementation sequence

1. **Sanitizer configuration and lifecycle stress:** inspect the existing CI workflow and CMake setup; add an opt-in AddressSanitizer/UndefinedBehaviorSanitizer test build (without changing normal release flags), run the lifecycle suite under sanitizers, and add bounded repeated concurrency stress for activation, release, erase, clear, retainOnly, shutdown, and active-slot accounting. Keep test time bounded and deterministic where possible.
2. **Streaming robustness:** test slow consumers/back-pressure, client disconnect, upstream timeout/error, byte-limit enforcement, reconnect/re-resolution, and orderly server shutdown.
3. **URL/DNS/SSRF review:** inspect DNS-resolved private, loopback, and link-local address handling while preserving TLS hostname verification; add focused tests before changing resolution or connection pinning.
4. **Separate acceptance gates:** live YouTube/CDN retrieval on Raspberry Pi, actual MPD CURL-input capability on the target build, audible hardware output, and RAM-only/no-persistent-storage behavior including swap. Do not infer these from local fixture tests.
5. No production binary installation, MPD queue/playback intervention, or service restart without explicit approval.


## Sanitizer build introduced — pending CI result (2026-11-17)

- Added opt-in `YOINKS_MPD_ENABLE_SANITIZERS` CMake option (`f25b8a13`) for GNU/Clang AddressSanitizer + UndefinedBehaviorSanitizer builds. It is OFF by default, so the normal target build flags remain unchanged.
- Added a separate Debug sanitizer build and full CTest run to the C++ GitHub Actions workflow (`fd9a330f`). CI run `44123520539` is pending; inspect its individual steps before treating the sanitizer gate as complete.
- After CI passes, the next target-device step is a separate sanitizer build directory, followed by the targeted lazy-session executable and full CTest. Do not overwrite `/tmp/yoinks-mpd-audit` or use the sanitizer binary as production.
- Then add bounded lifecycle stress coverage and proceed to streaming robustness tests.


## Bounded lifecycle stress coverage added — CI pending (2026-11-17)

- Added `testConcurrentLifecycleStressMaintainsRegistryAccounting()` in commit `a2a5b046a550a6ae9fbbe9b56841188c7befb8dc`, with concurrent activation/release/reconciliation/observation workers over 16 live sessions and bounded iteration counts.
- The test checks that all workers complete, live sessions remain retained, no activation reservations remain after joining, and clear/shutdown leave an empty registry.
- CI run `44123745133` is pending for this exact commit. The sanitizer workflow is also being validated; check the completed workflow's sanitizer configure, build, and test steps before requesting Raspberry Pi sanitizer execution.
