# Walidacja przygotowanej wersji 0.1.0 — 2026-10-21

- Release C++20, GCC 16.2.1, Qt 6.12.0, libvips 8.18.7, LibRaw 0.22.2.
- Dekoder: TIFF 16-bit, dokładne rozszerzanie RGB8, JPEG, ICC, orientacja,
  granice kafelków, LRU, poziomy zoomu, anulowanie, wielokrotne otwarcie,
  nieistniejący/uszkodzony plik.
- Shader: rzeczywisty kontekst EGL/OpenGL 3.3, upload RGBA16, odczyt pikseli,
  sprawdzona zgodność kanałów i orientacja. Renderer: llvmpipe (LLVM 23.1.3).
  To NIE jest test RTX ani okna w Hyprland.
- Syntetyczny, niekompresowany tiled BigTIFF: 2 171 977 512 bajtów,
  30 000 × 24 000 px. Otwarcie + kafelek 1:1: 8 ms; podgląd: 4754 ms.
- Syntetyczny JPEG Q100 4:4:4: 217 008 368 bajtów, 8000 × 8000 px.
  Otwarcie + podgląd: 2799 ms. Wygenerowanie i wszystkie pomiary: 13,42 s,
  szczytowe RSS procesu generowania + dekodera: 669,7 MiB. Cache systemowy
  był ciepły; wyniki nie opisują losowych plików, zimnego dysku ani GUI.
- ASan + UBSan: scenariusze dekodera przeszły bez zgłoszonych naruszeń.
  LeakSanitizer nie działa w tej piaskownicy (błąd ptrace); nie potwierdzono
  formalnie braku wszystkich wycieków. Zwalnianie oparte na RAII, ograniczone
  cache i zakończenie workerów sprawdzono także w przeglądzie kodu.
- Test instalacji/reinstalacji/weryfikacji/eksportu/backupu na tymczasowym koncie,
  porównanie zawartości manifestów, wykrywanie uszkodzonego launchera.
- 40 istniejących testów migracji przechodzi po dodaniu zakresu Prism.

Do potwierdzenia live: nazwa RTX w stopce, realny RAW aparatu, poprawność ICC
na konkretnym monitorze, fullscreen/Esc, przewijanie i płynność, wygląd/blury,
bezczynność i pamięć po dłuższej sesji. nvidia-smi nie ma w tej sesji dostępu
 do sterownika; nie instalowano nic w aktywnym koncie i nie wykonywano finish
--validated jako rzekomego potwierdzenia live. Duże pliki testowe usunięto.

Poprawka instalatora: odpowiedzi configerrors [] oraz [" ", ""] oznaczają
brak błędów. Test integracyjny z atrapą hyprctl sprawdza instalację dla pustych
komunikatów oraz wycofanie przy rzeczywistych błędach, nieznanym schemacie
i uszkodzonym JSON (osiem wariantów odpowiedzi).

## Wersja 0.2 — rzeczywiste zdjęcia i regresja awarii

Zrzut PID 668935 potwierdził SIGABRT w libglycin/gufo-exif podczas natywnego
podglądu libqgtk3 (gdk_pixbuf_new_from_file_at_scale). Nie był to błąd RTX.
Zastąpiono dialog natywny Qt oraz dodano własny panel plików, bez miniatur GTK.
Ostrzeżenie rejestracji portalu nie jest wskazaną w stosie przyczyną SIGABRT.

Test katalogu Sample-jpg: 29 plików JPEG/TIFF, 28 poprawnie odczytanych.
Wszystkie JPEG-i przeszły. Hasselblad_0138+FIN.tif został odrzucony przez
50 MiB limit alokacji libtiff używany przez libvips: ma blok danych Photoshop
około 116 MB. To nie wyczerpanie 32 GB RAM. Nie włączano bezlimitowego czytania;
ten konkretny TIFF pozostaje ograniczeniem, komunikat nie oznacza awarii GUI.
Raport: reports/sample-benchmark.json. Oryginałów nie zmieniano.

Dla chen-ya-x2d-xcd-25v-2.jpg (11656×8742, ok. 110 MiB) stara wersja:
pierwszy kafelek po ok. 2,44 s. Nowa: pełny podgląd ok. 1,5 s, kafelki
przygotowanego poziomu 0–2 ms. Pierwszy zoom wymagający pełnego dekodowania
nadal potrzebuje ok. 2,2 s w workerze; podgląd pozostaje widoczny i skaluje się.
Nie jest to dowód pomiarowy płynności interfejsu na RTX — wymaga testu live.

Dodano test kontrolera (bez widocznego okna) dla 100 zmian zdjęcia, serii 100
zoomów, zamknięcia podczas odczytu, powrotu po błędzie i nazw Unicode/[];
dodano pliki puste, ucięte JPEG oraz EXIF ze złą lokalizacją IFD. Testy shaderów
obejmują aktualny shader, a testy instalacji zachowanie Defaults MIME.

## 0.3 — 2026-10-21

Pięć testów CTest: decode, shader, interaction, aperture, ui. Trzy testy pakowania
obejmują też zachowanie pozostałych widgetów, instalację bez duplikatów i MIME.
Walidator Omarchy przyjmuje manifest pluginu. Podgląd powitalny sprawdzony
zrzutem z Qt offscreen w bieżącej jasnej palecie. Testy nie potwierdzają
kompozycji blur w Hyprland ani rzeczywistej płynności animacji RTX/8K.
Kod dekodera i limity cache bez zmian. RTX env.d/20-nvidia-render.conf bez zmian.


## 0.4 — 2026-10-21

Zmiana Okno → Oryginał jest testowana w Qt offscreen: 100% fizycznej skali,
niezmieniona geometria okna oraz możliwość jego późniejszej zmiany. Interakcja
z podglądem EXIF nie może resetować późniejszego zoomu przy pełnym podglądzie.
Testy miniatur obejmują poprawne EXIF IFD1, niepoprawne dane JPEG w miniaturze
oraz pierwotne przypadki uciętych plików i uszkodzonych nagłówków EXIF.

26/26 lokalnych JPEG-ów poprawnych (reports/0.4-jpeg-corpus.json):

| Pomiar dekodera | Min | Mediana | Max |
| --- | ---: | ---: | ---: |
| Pierwszy podgląd | 15 ms | 16 ms | 1103 ms |
| Pełny podgląd | 121 ms | 509 ms | 1103 ms |
| Powrót z RAM cache | 1 ms | 4 ms | 5 ms |

Pierwszy podgląd może być oznaczoną, tymczasową miniaturą EXIF, nie pełnym
obrazem. Gdy plik jej nie zawiera, ten pomiar jest równy czasowi pełnego
podglądu. Wynik cache dotyczy tego samego procesu i nie obejmuje restartu.

Przed/po na tych samych próbkach w tej sesji (reports/0.4-before.json,
reports/0.4-after.json): 02445.jpg, kafelek poziomu 3: 567 → 24 ms, pełny
podgląd 322 → 261 ms. chen-ya 110 MiB: pierwszy podgląd 16 ms, pełny ok. 963 ms,
pierwszy głębszy kafelek nadal 1418 ms. Rendering RTX nie jest mierzony.
Wariant osobnego JPEG shrink dla poziomów 1/4, 1/2, 1/1 odrzucono: ponowne
odczyty entropii zwiększały sumaryczny czas w tym samym benchmarku z ok. 2,7
s do 4,9 s. Pozostaje jeden pełny odczyt dopiero wtedy, gdy potrzeba detalu.

Duże pliki wygenerowane i usunięte przez test: BigTIFF 2 171 977 512 B
(30000×24000) i JPEG 217 008 368 B. Oba przeszły, w tym kolor 1:1 TIFF
RGB 210/100/30. Test duży był testem poprawności, uruchomionym równolegle
z kompilacją sanitizera; jego czasy nie służą porównywaniu wydajności.

ASan + UBSan dekodera: bez raportu naruszenia pamięci/UB. LeakSanitizer
wyłączony z uwagi na ograniczenia środowiska; nie jest to dowód braku
wszystkich wycieków. Alokacje podglądów współdzielą właściciela QImage/Vips,
mają limit LRU i są zwalniane przy usuwaniu poziomów/cache.

Manifest pluginu i geometria siedmiu listków są sprawdzane automatycznie;
wektorową ikonę i jedną fazę animacji obejrzano w renderze Qt offscreen.
Animacja 720 ms i otwarcie aplikacji działają równolegle według kodu;
potwierdzenie kompozycji oraz płynności w sesji RTX pozostaje testem live.
Nie modyfikowano skryptu wyboru RTX ani oryginałów zdjęć.


Test pakowania obejmuje instalację z utworzonego archiwum oraz sprawdzenie
źródeł w zainstalowanym katalogu recovery. Naprawiono oddzielny manifest tych
źródeł: nie zawiera już niekopiowanych obiektów prebuilt/ ani installed/,
które występują wyłącznie w archiwum eksportu/kopii. Pozostają one w pełnym
manifeście archiwum, a program i źródła mają osobne poprawne rejestry.


## 0.5 — 2026-10-21

Siedem testów CTest: decode, shader, interaction, aperture, ui, favorites,
window_control; trzy testy pakowania. Ulubione: MD5 wzorcowego pliku, atomowy
zapis i odczyt, duplikaty, równoległe operacje, blokada, anulowanie, edycja
podczas MD5, zmieniona nazwa, błędny kandydat, zachowanie UUID, uszkodzony JSON,
model i koniec animacji. UI testuje również dodanie/usunięcie przez serce,
listę, maksymalizację i stan po F11. Pakowanie przenosi mutable favorites.json
bez nadpisania istniejącej listy; rejestr instalacji nie obejmuje tego pliku.

Sterowanie maksymalizacją ma dodatkowy test Lua ze stubbem aktualnego API:
wybiera tylko swój PID i główne okno, omija dialog ulubionych i obcy proces,
używa jawnego set/unset trybu maximized. Wartości nazw plików nie wchodzą do
kodu Lua. Jest to konieczne ze względu na domyślne suppress_event=maximize.
Nie wykonano wywołania na rzeczywistym pulpicie z sandboxa.

Próba 160 otwarć z wymianą LRU: RSS 254,68 → 267,40 MiB w trakcie rozgrzewania;
po rozgrzaniu rozrzut 0,148 MiB, po clear 76,55 MiB. Raport:
reports/0.5-memory.txt. Nie stwierdzono trendu narastającego RAM. ASan/UBSan
dekodera i ulubionych bez raportu; LSAN nie działa w tym środowisku, więc był
wyłączony. Brak narzędzi Valgrind/heaptrack; niczego nie pobierano.
Nie potwierdzono braku wszystkich wycieków ani zachowania VRAM na prawdziwym RTX.

Nowy znak paska ma wektorową ramkę fotografii i krajobraz, czarny rysunek na
przezroczystym tle. Układ powitalny obejrzano w zrzucie Qt offscreen. Sublime
jest osobnym instalatorem: ./run.sh sublime install; jego testy obejmują importy
TOML, reguły alpha terminala, zachowanie preferencji i rollback configerrors.


## 0.6 — 2026-10-21 (workspace, bez oznaczania live)

8/8 CTest: dekoder, shader EGL, interakcje, przysłona, UI, ulubione,
sterowanie oknem, operacje na plikach. 4/4 testy pakowania oraz 4/4 testy
krytycznych plików archiwum. qmllint nowego TerminalWidget.qml: kod 0,
bez diagnostyki. Szczegóły: reports/0.6-validation.txt.

Regresja maksymalizacji odtwarza DOSŁOWNE porównanie klasy hl.get_windows
w Hyprland 0.56.2; poprzedni stub pomijał tę semantykę, więc nie ujawniał
błędnego regexu w query. Usunięto filtr klasy z query, zachowując ścisły wybór
PID i tytułu głównego okna. Testuje tryby 0/1/2, jawne set/unset, odmowę
wykonania i potwierdzenie stanu. Odpowiedź IPC musi być dokładnie „ok”.
Dokumentacja Omarchy opisuje Super+Alt+F jako maksymalizację z zachowaniem paska:
https://omarchy.org/manual/navigation/
Źródło porównania klasy:
https://github.com/hyprwm/Hyprland/blob/v0.56.2/src/config/lua/bindings/LuaBindingsQuery.cpp#L51

Testy plików obejmują: identyczność bajtów kopii, istniejący cel i wyścig jego
utworzenia, anulowanie przed i po odczycie, zmianę źródła podczas kopiowania,
MD5+rozmiar+nazwę, odrzucenie przemianowanej/zmienionej kopii, zakaz usuwania
ostatniej kopii, mutację przed koszem, faktyczne przeniesienie do kosza.
Wszystkie modyfikowane zdjęcia są sztucznymi plikami w QTemporaryDir.
Dialog jest testowany zdarzeniami Qt: wybór miejsca, 100 kliknięć zapisu,
listowanie duplikatów, indeksy/parent modelu, Anuluj i potwierdzenie kosza.
30 cykli otwórz/zamknij nie pozostawia obiektów dialogów, a efekt zanika po 180 ms.
UI testuje dodatkowo 100 cykli Wstecz/Dalej i rozgałęzienie historii.

ASan/UBSan: aktualne testy ui i file_ops przechodzą bez raportu błędu,
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1; UBSAN_OPTIONS=halt_on_error=1.
LeakSanitizer/ptrace są niedostępne w tym środowisku. Nie jest to dowód braku
wszystkich wycieków; nie przeprowadzono pomiaru sprzętowego VRAM RTX.
Nowy model listy jest wirtualny, bez osobnego QTreeWidgetItem na każdy plik.

Sprawdzono eksport źródeł/binarki, wewnętrzne SHA256, instalację na fikcyjne
konto w /tmp, zachowanie opcji widgetów i pozostałych ikon, idempotencję,
odtworzenie ulubionych/zakładek tylko gdy nie istnieją na koncie docelowym.
Ochroniony skrypt RTX ma niezmienione SHA256
f7563a11c28347b822467708e2acaac481707d96c0082d20f1f2fad92e15f523.
Nie pobierano pakietów ani dodatkowych bibliotek.

Nie instalowano zmian do ~/.local / ~/.config: sandbox pozwala pisać tylko
w projekcie i /tmp. Rzeczywista maksymalizacja, oba monitory, uruchomienie
Alacritty z paska i wygląd nowego dialogu pod kompozytorem wymagają sprawdzenia
po ./run.sh photos install. Nie uruchomiono finish --validated, bo ta wersja
nie jest jeszcze potwierdzona w działającej instalacji. Nie zmieniano ostatniej
sprawdzonej kopii systemu.


## 0.7 — 2026-10-21, ky-prism (workspace)

9/9 CTest i 4/4 testy instalacji/pakowania, dodatkowo 4 testy krytycznych plików
oraz 10 testów prywatności eksportu. Raport: reports/0.7-validation.txt.
Testy pakowania uruchamiają rzeczywiste polecenia ky-prism i omarchy-prism
na fikcyjnym koncie z --version: oba zwracają ky-prism 0.7.0. Nowy desktop
ma poprawnie cytowany Exec, stare skojarzenia zastąpiono nowymi; stary desktop
ma NoDisplay=true i nie rejestruje drugiej listy MIME. Dane zachowują poprzednią
ścieżkę. Do selektora programów backupu dodano dokładnie ky-prism, bez szerszych globów.

Test keyboard wysyła rzeczywiste QKeyEvent przez QtTest: edycja ścieżki
z literami F, cyfrą 1 i strzałką, kopiowanie tekstu Ctrl+C, zapis/usunięcie
gwiazdki, filtr i Enter w przeglądarce, Delete zachowujący katalog, historia,
ukrywanie/pokazywanie panelu, dodawanie/usuwanie serca, lista ulubionych,
sortowanie, zoom, pan 64/256, PageUp/PageDown, EXIF i pełny łańcuch Tab.
Zapis kopii obejmuje wybór ścieżki klawiaturą, Alt+Z i rzeczywisty zapis;
w duplikatach klawiatura wybiera katalog, grupę i plik, Esc pozostawia oba,
a Tab+Enter w osobnym potwierdzeniu przenosi dokładnie wskazany plik do kosza.
Wszystkie pliki pochodzą z QTemporaryDir; nie modyfikowano zdjęć użytkownika.

Wykryto i poprawiono rejestrowanie tylko pierwszego wariantu StandardKey:
na tej platformie Close zawiera Ctrl+F4 i Ctrl+W. Fabryki zachowują teraz typ
StandardKey przy konstrukcji QShortcut, więc rejestrują pełny zestaw.
Skróty bez modyfikatorów ograniczono do WidgetShortcut na zdjęciu. Enter
obsługuje również skupione przyciski bez autoDefault. Nie występuje już kolizja
Alt+Z między zapisem kopii a zamknięciem jej dialogu; anulowanie/zamknięcie
w tym dialogu ma Alt+A. Zamknięcie głównego okna ukrywa jego dialogi i kończy animację.

About: autor Krzysztof Kylen Lenartowicz, © 2026, animacja wektorowa 640 ms,
strona skrótów, Ctrl+W/Esc, Alt+1/2 i test zatrzymania animacji po ukryciu
strony, okna oraz po zamknięciu programu. Obejrzano zrzuty Qt offscreen dla
bieżącej jasnej palety i Tokyo Night na fikcyjnym HOME w /tmp. Test keyboard
przechodzi w obu paletach. Nie jest to kontrola natywnego blur Hyprland.

ASan/UBSan: ui, file_ops i keyboard 3/3 bez raportu błędu. detect_leaks=0,
ponieważ LeakSanitizer/ptrace są niedostępne w sandboxie. To nie dowód braku
wszystkich wycieków; nie wykonano sprzętowego pomiaru VRAM RTX.

Korekta wcześniejszego opisu lint: /usr/bin/qmllint zwraca 255 bez diagnostyki.
Właściwy /usr/lib/qt6/bin/qmllint (6.12.0) zwraca 0, ale ostrzega o modułach
qs.Ui/qs.Commons i typach dostarczanych przez runtime Quickshell. Nie należy
traktować tego jako pełnej kontroli typów ani deklarować braku ostrzeżeń.
QML przysłony ma osobny działający test uruchomieniowy CTest.

Nie instalowano 0.7 do ~/.local / ~/.config. Sandbox pozwala na zapis tylko
w projekcie i /tmp. Natywne okna, integracja skojarzeń, klawiatura sprzętowa,
widget i NVIDIA wymagają potwierdzenia po ./run.sh photos install w sesji.
Nie uruchomiono finish --validated i nie zastąpiono sprawdzonego checkpointu.
Ochroniony plik RTX ma nadal SHA256
f7563a11c28347b822467708e2acaac481707d96c0082d20f1f2fad92e15f523.
Nie pobierano dodatkowych pakietów ani bibliotek.


## 0.8 — zakres i sposób potwierdzania

Stan dotyczy źródeł i testów w workspace, nie instalacji live. Raport:
`reports/0.8-validation.txt`. Testy UI na offscreen nie tworzą QOpenGLWidget;
oddzielnie badany jest shader EGL oraz ręczny `./run.sh photos gpu-test` w sesji.

Wykryty i naprawiony wyścig: QFuture może przestać zgłaszać isRunning(), zanim
QFutureWatcher przekaże finished do GUI. Nadpisanie watchera w tej szczelinie
mogło zgubić wynik lub odczytać późniejszą future w wątku GUI. Stan zadania
pozostaje teraz aktywny do obsłużenia finished; dotyczy widoku, listy dat,
listy sąsiednich zdjęć i pojemności dysku. Test interakcji specjalnie nie
przetwarza zdarzeń przez 250 ms po szybkim dekodowaniu i sprawdza ten stan.

Pokrycie rozszerzone: 60 szybkich zmian kart, limit 24 kart, dwa niezależne
widoki i zwolnienie drugiego, polskie i długie nazwy, 4 sortowania, wiele plików
w kopii, kolizja w paczce, błędne ścieżki, 64 małe pliki z uszkodzonymi
nagłówkami JPEG/TIFF/RAW, JPEG z rozszerzeniem TIFF, uszkodzone EXIF i miniatury.
Kopiowanie nadal sprawdza stabilność źródła, zapis atomowy i brak nadpisywania.

Instalator testowany na fikcyjnym koncie: domyślne MIME, „Otwórz za pomocą”,
zgodność nazw ky-prism/omarchy-prism, plugin Chrome za terminalem, zachowanie
innych widgetów/opcji, eksport i backup z manifestami, odtworzenie źródeł,
zachowanie istniejących ulubionych/zakładek. Plany zależności sprawdzane osobno
dla Arch/Omarchy oraz APT. Nie jest to dowód instalacji na realnym Debianie/Ubuntu.

ASAN/UBSan oraz RSS służą kontroli stabilności; LeakSanitizer w tym sandboxie
odmawia działania z powodu ptrace. Nie deklarujemy dowodu braku wszystkich
wycieków ani odporności dekoderów C na każdy przyszły plik. Test GPU, portalu,
blur oraz montowania rzeczywistego pendrive’a pozostaje do wykonania live.
Chroniony skrypt wyboru RTX pozostaje niezmieniony.


## 0.9 — ostrość, biblioteka i pamięć

Raport i pomiary: `reports/0.9-validation.txt`, `0.9-fit-fhd.json`,
`0.9-fit-4k.json`, `0.9-memory.txt`, `0.9-thumbnails.txt`, `0.9-large.txt`.
12/12 CTest oraz 7/7 ASAN/UBSan; 33 miniatury, 31 pełnych odczytów.
Dwa wieloklatkowe FFF nie są obsługiwane przez LibRaw: ich mały podgląd nie
może uchodzić za oryginał. To jawne ograniczenie, a nie zaliczony pełny odczyt.

Mediana CPU do ostrego dopasowania 26 JPEG: 807→764 ms dla 1600×900 oraz
2048→966 ms dla 3200×1800. Cache ostrych podglądów współdzielony i ograniczony;
metadane uwzględnione w koszcie. Po 160 otwarciach rozrzut RSS 2.21 MiB,
wzrost deskryptorów 0. LSan zablokowany przez ptrace; brak absolutnego dowodu
braku wszystkich wycieków. Test rzeczywistego GPU zawiera wzorzec pikselowy 1:1
na nieparzystych wymiarach, do uruchomienia w sesji użytkownika.

Przetestowano TIFF 2.17 GB i JPEG 217 MB. Adapter TIFF zachowuje oryginały,
ICC i obraz złożony; tylko duże prywatne dane warstw są pomijane w widoku.
Zrzuty UI jasny/ciemny i Chrome znajdują się w reports/0.9-*.png; sam blur
wymaga kompozytora. W tym środowisku nie można zainstalować zmian w ~/.local,
więc wydanie pozostaje przygotowane do instalacji, bez finish --validated.

## 0.10 — materiały, akcje bez popupów, klepsydra

Raport: `reports/0.10-validation.txt`. 13/13 testów CTest przechodzi.
26/26 rzeczywistych JPEG: mediana przygotowania ostrego dopasowania 3200×1800
wyniosła 974 ms; dekoder zachowano, nie deklarujemy przyspieszenia względem 0.9.
160 otwarć z wymuszoną rotacją cache: rozrzut końcowego RSS 0,31 MiB, bez
przyrostu deskryptorów. Nowy test `material` sprawdza stany i cykl życia animacji.

Zrzuty jasnego/ciemnego interfejsu, otwartych dialogów i klepsydry są w reports.
Sprawdzono zmianę palety w już otwartym dialogu. CUDA w izolacji nie widzi GPU
(kod 100); test NVIDIA oraz blur pulpitu trzeba wykonać w działającej sesji.
Brak zapisu do live ~/.local i ~/.config, więc brak `finish --validated` tutaj.

## 0.10.1 — przechwycenie myszy i blokada aktualizacji

Raport: `reports/0.10.1-validation.txt`. 14/14 testów CTest przechodzi.
Nowy test `pointer` obejmuje 100 kliknięć/zwolnień, próg przeciągnięcia,
syntetyczne zdarzenia, współrzędne poza oknem, kontrolki, skalowanie i ukrycie.
Intermittent freeze wymaga jeszcze potwierdzenia w natywnej sesji XWayland.
Potwierdzona blokada aktualizacji wynikała z wymuszania dawnej pozycji delfina;
36 testów ochrony przechodzi, a poprawiony plan dla bieżącego konta jest pusty.
Nie wykonano instalacji live ani aktualizacji systemu z izolacji agenta.

## 0.10.2 — resize biblioteki

Raport: `reports/0.10.2-validation.txt`. Regresja cache odtworzona testem na
0.10.1 i naprawiona. 14/14 testów przechodzi; 180 zmian szerokości splittera
nie tworzy nowych zadań dekodowania gotowych miniaturek ani nowych wpisów cache.
Nowy rozmiar paneli jest zatwierdzany po puszczeniu myszy, żeby ograniczyć
przebudowywanie bufora OpenGL. Wymagany test w działającej sesji XWayland.


## 0.11.0 — siatka miniaturek

Raport: `reports/0.11-validation.txt`, dane `0.11-gallery-*.json` i
`0.11-memory.json`. Testy wydajności rozróżniają małe syntetyczne JPEG-i,
800 wpisów opartych na 22 kopiach rzeczywistych fotografii oraz natywny GPU.
Powtórzone dane i ciepły cache systemowy nie są 800 niezależnymi zimnymi
odczytami. Testu natywnego Qt/RTX nie potwierdzono z izolacji agenta.
Źródła, zależności, testy i raporty są częścią install/export/backup;
cache miniaturek jest odtwarzalnym artefaktem i nie zastępuje fotografii.


## 0.12 — rzeczywisty backend dekodera nvJPEG

Raport: `reports/0.12-validation.txt`. Moduł CUDA jest skompilowany, ma własny
Lanczos3 i faktycznie wywołuje dekoder nvJPEG GPU Hybrid. Testy hosta oraz
AVX2 nie potwierdzają wykonania na RTX. W tym środowisku cudaGetDeviceCount
nie wykrywa GPU: raport rygorystycznej próby ma success=false/gpu_decodes=0.
Nie ma pomiaru przyspieszenia nvJPEG ani potwierdzonej instalacji live.
Installer ON wymaga udanego natywnego testu przed zapisaniem plików użytkownika.
