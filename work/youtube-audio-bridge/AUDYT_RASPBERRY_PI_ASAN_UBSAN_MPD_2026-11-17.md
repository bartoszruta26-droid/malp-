# Audyt: Yoinks-MPD — diagnostyka Raspberry Pi ASan/UBSan i MPD

Data: 2026-11-17  
Baza audytu: `master` @ `072ac43316c28ecfaad7c76e44891fd60a539838`  
Zakres: `yoinks-mpd`, `yt-dlp`, lokalny HTTP proxy i testy MPD na Raspberry Pi.

## 1. Stan repozytorium i CI

- Integracja YouTube Audio Bridge została scalona do `master` commitem `072ac43316c28ecfaad7c76e44891fd60a539838`.
- GitHub Actions run [44140250406](https://github.com/bartoszruta26-droid/malp-/actions/runs/44140250406) dla tego commita zakończył się sukcesem.
- Potwierdzone kroki C++: Release configure/build, pełny CTest, Debug ASan/UBSan configure/build i pełny CTest — wszystkie zakończone powodzeniem.
- W logu z Raspberry Pi konfiguracja CMake i kompilacja wszystkich celów zakończyły się sukcesem.
- Wynik `git status --short` był pusty — przekazane logi nie wykazują lokalnych zmian.
- PR #2 został scalony. Nie należy ponownie scalać PR ani odtwarzać starej gałęzi integracyjnej bez potrzeby.

## 2. Analiza zgłoszonego przebiegu Raspberry Pi

### 2.1 Nazwa binarnego testu

Uruchomiono:
`/tmp/yoinks-mpd-sanitizers/yoinks_mpd_bounded_stream_buffer_tests`

Ta nazwa nie występuje w aktualnym CMake. Test bufora ma target `yoinks_mpd_core_tests`, a CTest rejestruje go jako `bounded_stream_buffer`.

Poprawne uruchomienie:
```bash
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 \
  /tmp/yoinks-mpd-sanitizers/yoinks_mpd_core_tests
```

### 2.2 ASan: błąd alokatora przed wykonaniem testów

Wszystkie testy uruchomione w sanitizer build kończą się tym samym komunikatem:
`AddressSanitizer: CHECK failed: sanitizer_allocator_primary64.h:131`
z nieprawidłowym zakresem adresów. Jednostkowe testy kończą się w około 0,03 s z pustym stosem.

Wniosek: log nie pokazuje raportu ASan o nieprawidłowym dostępie w kodzie Yoinks-MPD. Wskazuje na awarię inicjalizacji alokatora ASan w środowisku docelowym lub jego kombinacji runtime/toolchain/kernel/mapping. To hipoteza do zweryfikowania, a nie potwierdzona przyczyna. Nie wyłączać ASan/UBSan w CI ani nie uznawać testów docelowych za zaliczone.

Dane, które trzeba zebrać:
- wersje `gcc -v`, `g++ -print-file-name=libasan.so`, `ldd --version`, `uname -a`;
- wersja pakietu `libasan` i `libubsan`, konfiguracja PIE/ASLR i architektura;
- minimalny reproduktor ASan (program z samym `int main(){return 0;}`) kompilowany i uruchomiony tym samym kompilatorem;
- osobne, świeże kompilacje UBSan-only oraz ASan-only, bez mieszania wniosków;
- porównanie uruchomienia tego samego binarium i reproduktora na x86_64 CI oraz ARM64 Raspberry Pi.

Nie zmieniać `ASAN_OPTIONS` w celu maskowania awarii. Opcje diagnostyczne można zmieniać wyłącznie dla izolacji hipotezy i opisać wynik.

### 2.3 Testy integracyjne MPD

W testach `http_api_integration` oraz `real_mpd_http_proxy_playback` logi zawierają ten sam błąd ASan przy starcie binarium. Dodatkowo MPD zgłasza:
- brak konfiguracji dekodera WildMIDI (nie dowodzi przyczyny braku HTTP/CURL);
- brak początkowych plików `database` i `state` w katalogu tymczasowym, po czym dodaje `/audio.wav`.

Błędy `mpd_ready` oraz `bridge_ready` należy rozdzielić od problemu ASan. Po usunięciu blokera uruchom testy jeszcze raz i zbierz osobne logi procesu MPD, bridge oraz testowego TLS fixture. Nie wyciągać wniosków o obsłudze CURL przed uzyskaniem poprawnego startu fixture i bridge.

## 3. Wnioski z aktualnego kodu

- `yoinks-mpd/CMakeLists.txt` tworzy target `yoinks_mpd_core_tests` i rejestruje 11 testów CTest.
- Sanitizery są opcją kompilacji i linkowania `-fsanitize=address,undefined`; domyślny build produkcyjny pozostaje bez zmian.
- Rejestr lazy ogranicza kolejkę do 200 sesji, aktywne strumienie do dwóch, a łączny przydział buforów RAM do 8 MiB.
- Rejestr ma guard aktywacji i barierę `shutdown()`, która anuluje sesje oraz czeka na aktywacje.
- URL upstream jest walidowany jako HTTPS i host `googlevideo.com`/subdomena, bez userinfo, z portem 443; przekierowania są wyłączone, TLS peer/host verification włączone.
- Nadal istnieje wcześniej opisany problem obrony SSRF: walidowana jest nazwa hosta, ale nie wszystkie adresy IP zwrócone przez DNS ani rzeczywisty peer. Remediacja pozostaje odłożona na wcześniejsze żądanie; nie należy jej przypadkowo oznaczać jako rozwiązanej.
- Test docelowego MPD jest odrębną bramką. Dokumentacja instalatora opisuje osobny, izolowany MPD z wyjściem `null`; nie używać produkcyjnego MPD ani kolejki do testów.

## 4. Plan dalszych prac — bez zmian produkcyjnych

### Etap A — zebranie danych runtime (Raspberry Pi)
1. Potwierdzić `uname -m`, `uname -a`, wersje GCC, libasan, libubsan i glibc.
2. Uruchomić minimalny program ASan i UBSan, aby odseparować runtime od kodu projektu.
3. Uruchomić właściwy test binarny `yoinks_mpd_core_tests`.
4. Zbudować osobno ASan-only i UBSan-only w nowych katalogach tymczasowych.
5. Zapisać dokładne wyniki i kody zakończenia.

### Etap B — testy bez sanitizera na urządzeniu
1. Zbudować świeży Release w katalogu poza repozytorium.
2. Uruchomić pełny CTest z `--timeout 120`.
3. Jeżeli testy MPD nie przejdą, zebrać wszystkie logi i ustalić, czy startuje mock/daemon/bridge.
4. Nie uruchamiać usługi systemowej, nie instalować binarium i nie restartować usług.

### Etap C — odizolowany test MPD/CURL
1. Wykonać wyłącznie skrypt `scripts/yoinks-mpd/live-smoke-test.sh` z testowym MPD na osobnym porcie oraz wyjściem `null`.
2. Nie używać produkcyjnego MPD na porcie 6600.
3. Zweryfikować stan `playing`, lokalny URI proxy w kolejce oraz brak URL CDN w kolejce/logach.
4. Sprawdzić wersję MPD i obecność wejścia CURL.

### Etap D — dalszy przegląd bezpieczeństwa i Androida
1. Osobno zaplanować test-first naprawę DNS/IP SSRF, z walidacją IPv4/IPv6, zakresów specjalnych, wielu odpowiedzi i rzeczywistego połączenia do zweryfikowanego IP przy zachowaniu SNI/certyfikatu. Nie wdrażać pominięcia walidacji dla produkcji.
2. Zweryfikować wynik workflow Android na commicie integracyjnym, zwłaszcza instalację i test działania na emulatorze.
3. Nie deklarować odtwarzania end-to-end, dopóki nie przejdą testy na izolowanym MPD/CURL oraz testy aplikacji.

## 5. Komendy diagnostyczne do wykonania na Raspberry Pi

```bash
set -o pipefail

echo "=== PLATFORM ==="
uname -a
uname -m
getconf GNU_LIBC_VERSION

echo "=== TOOLCHAIN ==="
gcc --version
g++ --version
g++ -print-file-name=libasan.so
g++ -print-file-name=libubsan.so
ldd --version | head -n 1
dpkg-query -W -f='${Package} ${Version} ${Architecture}\n' \
  gcc g++ libasan8 libubsan1 libc6 2>&1 || true

echo "=== ASAN MINIMAL REPRODUCER ==="
cat >/tmp/asan-minimal.cpp <<'EOF'
int main() { return 0; }
EOF
g++ -O0 -g -fsanitize=address -fno-omit-frame-pointer \
  /tmp/asan-minimal.cpp -o /tmp/asan-minimal
ASAN_OPTIONS=verbosity=1:detect_leaks=0 /tmp/asan-minimal
echo "asan_minimal_exit=$?"

echo "=== UBSAN MINIMAL REPRODUCER ==="
g++ -O0 -g -fsanitize=undefined -fno-omit-frame-pointer \
  /tmp/asan-minimal.cpp -o /tmp/ubsan-minimal
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 /tmp/ubsan-minimal
echo "ubsan_minimal_exit=$?"

echo "=== CORRECT BUFFER TEST TARGET ==="
cmake --build /tmp/yoinks-mpd-sanitizers --target yoinks_mpd_core_tests --parallel 2
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 \
  /tmp/yoinks-mpd-sanitizers/yoinks_mpd_core_tests
```

Uwaga: przy `set -e` polecenie kończące się błędem przerwie skrypt. Dla pełnego raportu warto uruchomić każdy reproduktor w osobnej sesji lub opakować go w `if` i zapisać kod wyjścia.

## 6. Kryteria zamknięcia audytu

- [ ] ASan minimalny reproduktor działa na Raspberry Pi albo potwierdzono i udokumentowano defekt runtime/toolchain.
- [ ] UBSan-only przechodzi.
- [ ] Pełny Release CTest przechodzi na Raspberry Pi.
- [ ] Test izolowanego MPD/CURL przechodzi i potwierdza odtwarzanie WAV do wyjścia `null`.
- [ ] Android workflow zakończony sukcesem dla tego samego commita.
- [ ] Brak instalacji/restartu produkcyjnej usługi i brak zmian kolejki MPD.

**Status końcowy na 2026-11-17:** kompilacja ARM64 z logu użytkownika udana; ASan/UBSan na Raspberry Pi zablokowane przez awarię inicjalizacji ASan; wyniki testów integracyjnych MPD są niewiarygodne do czasu uruchomienia komponentów bez awarii runtime; CI x86_64 dla pełnego Release i ASan/UBSan zakończone powodzeniem. Nie wydawać zgody na wdrożenie produkcyjne na podstawie tych wyników.


## 7. Uzupełnienie po minimalnych reproduktorach ARM64 (wyniki użytkownika)

Środowisko:
- Linux `6.12.75+rpt-rpi-v8`, Debian/Raspberry Pi kernel, `aarch64`;
- glibc `2.41`;
- GCC/G++ `14.2.0-19`;
- runtime wskazany przez kompilator: `/usr/lib/gcc/aarch64-linux-gnu/14/libasan.so` oraz `libubsan.so`.

Wyniki:
1. Minimalny program `int main(){return 0;}`, skompilowany wyłącznie z `-fsanitize=address`, kończy się tym samym CHECK w `sanitizer_allocator_primary64.h:131`. Zanim nastąpi awaria, ASan zgłasza liczne `failed to intercept` dla symboli libc (m.in. `__isoc99_printf`, `__isoc99_sprintf`, `__isoc99_snprintf`, funkcji XDR i `pthread_mutexattr_getrobust_np`).
2. Minimalny program `-fsanitize=undefined` kończy się bez diagnostyki i skrypt przechodzi do kolejnej sekcji. Jest to pozytywny wynik UBSan-only dla pustego programu, ale nie jest jeszcze dowodem poprawności całej aplikacji.
3. Właściwy test `yoinks_mpd_core_tests` ponownie kończy się identycznym CHECK ASan. Nie ma podstaw, aby interpretować to jako wykrycie błędu bufora w kodzie projektu.

Wniosek po aktualizacji: problem ASan jest reprodukowalny poza Yoinks-MPD na tym samym Raspberry Pi. Można z wysoką pewnością odseparować go od logiki aplikacji; dokładna przyczyna w kombinacji runtime GCC/libasan, glibc, konfiguracji systemu i mapowania pamięci pozostaje do ustalenia. Nie przypisywać winy samemu kernelowi ani nie zalecać przypadkowego wyłączenia ASLR/PIE.

Korekta wcześniejszego statusu: UBSan-only dla minimalnego reproduktora przeszedł; ASan-only dla minimalnego reproduktora nie przeszedł. Pełny zestaw testów aplikacji pod UBSan-only nadal nie został uruchomiony.

### Następny, bezpieczny krok diagnostyczny

Zebrać dokładne wersje pakietów, zależności runtime i zmienne środowiskowe, bez aktualizacji systemu i bez modyfikacji usług:

```bash
echo "=== PACKAGE VERSIONS ==="
dpkg-query -W -f='${Package} ${Version} ${Architecture}\n' \
  gcc-14 g++-14 libasan8 libubsan1 libc6 2>&1

echo "=== RUNTIME DEPENDENCIES ==="
ldd /tmp/asan-minimal
readelf -d /tmp/asan-minimal | grep -E 'NEEDED|RPATH|RUNPATH' || true
readelf -h /tmp/asan-minimal | grep -E 'Class:|Machine:|Type:'

echo "=== ASAN LIBRARY ==="
ASAN_LIB="$(g++ -print-file-name=libasan.so)"
ls -l "$ASAN_LIB"
readlink -f "$ASAN_LIB"
dpkg -S "$(readlink -f "$ASAN_LIB")" 2>&1 || true

echo "=== ENVIRONMENT ==="
env | grep -E '^(LD_|ASAN_|UBSAN_|GLIBC_TUNABLES=)' || true
cat /proc/sys/kernel/randomize_va_space

echo "=== UBSAN-ONLY PROJECT BUILD ==="
cmake -S yoinks-mpd -B /tmp/yoinks-mpd-ubsan-only \
  -DCMAKE_BUILD_TYPE=Debug \
  -DYOINKS_MPD_ENABLE_SANITIZERS=OFF \
  -DCMAKE_CXX_FLAGS="-fsanitize=undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=undefined"
cmake --build /tmp/yoinks-mpd-ubsan-only --parallel 2
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
  ctest --test-dir /tmp/yoinks-mpd-ubsan-only --output-on-failure
```

Uwaga: ta konfiguracja UBSan-only jest tymczasowym buildem diagnostycznym. Nie zmienia konfiguracji projektu ani binariów produkcyjnych. Wynik należy raportować oddzielnie dla testów jednostkowych i integracyjnych.

Kryteria:
- [x] ASan minimalny reproduktor: awaria poza aplikacją — potwierdzona.
- [x] UBSan minimalny reproduktor: przechodzi.
- [ ] Ustalenie wersji pakietów i zależności libasan.
- [ ] Pełny CTest projektu w buildzie UBSan-only.
- [ ] Pełny Release CTest na Raspberry Pi.
- [ ] Test izolowanego MPD/CURL.
