# ky-prism — historia zmian (dziennik deweloperski)

Raporty `reports/…` wymienione niżej są lokalnymi logami z testów i nie są publikowane w repozytorium.


## 0.13.0 — interfejs GTK 4 i regulowane krycie oprawy

Okno programu jest teraz aplikacją **GTK 4** (Ubuntu 26.04: GTK 4.22; wymagane
GTK ≥ 4.12). Uruchamia się natywnie na Waylandzie (GNOME/Mutter, Hyprland w
Omarchy) oraz na X11. Silnik — dekodowanie libvips/LibRaw, ICC, piramida
kafelków, miniatury, nvJPEG, kopiowanie, duplikaty, ulubione i zakładki — to
ten sam, przetestowany kod; korzysta wyłącznie z niewizualnych modułów Qt
(QtCore/QtGui/QtConcurrent), bez okien Qt. Pliki danych i ich formaty są bez
zmian: `favorites.json`, `bookmarks.json`, `language.json`, cache miniaturek
i `appearance.json` działają dalej.

**Krycie interfejsu.** Przycisk z półwypełnionym kółkiem w nagłówku (albo
`Ctrl+Alt+O`) otwiera suwak 35–100 %. Zmiana działa od razu i jest zapisywana
w `~/.config/ky-prism/appearance.json` (`{"ui_opacity_percent":94}`).
Dotyczy pasków, paneli i okien programu. **Zdjęcie jest zawsze w pełni
kryjące**: płótno fotografii ma alfa 1, a test `gtk_ui` porównuje jego piksele
przy niskim i pełnym kryciu oprawy. Wartość domyślna: 94 % tam, gdzie
kompozytor nie rozmywa tła (GNOME), 69 % — dotychczasowy materiał — w
Hyprland, gdzie rozmycie jest dostępne. `PRISM_UI_OPACITY=0…100` nadal
wymusza wartość dla jednego uruchomienia. Okna dialogowe, menu i podpowiedzi
nie schodzą poniżej 97 % (od poprawki okna duplikatów; wcześniej 90 %), żeby tekst okna pod spodem nie prześwitywał.

**Wygląd.** Pasek narzędzi jest podzielony na grupy (pliki, nawigacja, praca,
zoom, znaczniki, metadane); ikony wektorowe, karty, lista i siatka miniatur,
EXIF oraz dialogi mają wspólny szklany materiał. Kolory: paleta Omarchy
(`colors.toml`, obserwowana zdarzeniowo), a poza Omarchy — jasny/ciemny
schemat i kolor akcentu systemu (GNOME/Yaru), także zmieniane w locie.

**Renderer zdjęć.** `GtkGLArea` z tym samym shaderem i kafelkami RGBA16.
GTK wybiera rodzinę kontekstu dla całego ekranu: desktopowy OpenGL ≥ 3.3 albo
OpenGL ES ≥ 3.0 (domyślne w GTK na Waylandzie). Oba warianty używają tego
samego kodu; w ES tekstury 16-bit wymagają `GL_EXT_texture_norm16`, a bez niego
kafelki są przesyłane jako 8 bit/kanał (głębia samego płótna). Bez działającego
OpenGL program pokazuje podgląd CPU zamiast się nie uruchomić. Stopka nadal
pokazuje rzeczywisty `GL_RENDERER`. Dodano zoom gestem szczypania.

**Launcher.** `PRISM_GPU=auto|nvidia|system` jak dotąd (zmienne PRIME tylko dla
procesu). Nowe: `PRISM_BACKEND=auto|wayland|x11` — `x11` przywraca
wcześniejszą drogę przez XWayland. Program nie wymusza już XWayland.

**Wybór plików.** „Otwórz” / „Załaduj zdjęcie” i wybór drugiego zdjęcia do
porównania to własne okno z miniaturami i podglądem. Wybór katalogu i nazwy
kopii używa systemowego okna (portal XDG), poza procesem programu.

**Poprzednia wersja Qt** pozostaje w źródłach jako `ky-prism-qt`: buduje się
(gdy dostępne są Qt Widgets) razem ze swoimi testami zdarzeń, ale nie jest
instalowana. `-DPRISM_BUILD_QT_LEGACY=OFF` ją pomija.

**Testy.** `./run.sh test` uruchamia testy silnika, testy poprzedniego okna Qt
oraz nowe: `gtk_settings` (zapis krycia) i `gtk_ui` — prawdziwe okno GTK na
ekranie sesji: lista katalogu, otwarcie, zoom 1:1, karty, porównanie,
zakładki, ulubione, język, dialogi, krycie, zamknięcie w trakcie odczytu.
Bez ekranu `gtk_ui` zgłasza pominięcie. `./run.sh gtk-test --screenshots
KATALOG` zapisuje dodatkowo zrzuty PNG do oceny wyglądu. Test okna zależy od
działającego kompozytora (obszar roboczy, kafelkowanie, fokus), dlatego ma
etykietę `display` i **nie blokuje instalacji**: `./run.sh install` oraz
instalator paczki (`--test`) uruchamiają `ctest -LE display`.
Test steruje oknem przez nazwane akcje programu; rzeczywistego wejścia
(klawiatura, mysz, przeciąganie plików, gest szczypania) nie symuluje.

**Zakres weryfikacji.** Zbudowano i przetestowano na Ubuntu 26.04.1
(GNOME 50, Wayland, GTK 4.22.4, RTX 4070). **Na Omarchy/Hyprland tej wersji nie
uruchamiano** — kod ścieżek Hyprland (maksymalizacja przez `hyprctl`, reguła
`prism.lua`, paleta `colors.toml`) jest zachowany, a identyfikator okna
pozostaje `ky-prism`, ale wymaga to potwierdzenia na żywo, w tym pracy PRIME
bez XWayland (`PRISM_BACKEND=x11` jest drogą zapasową).

Zależności budowania: dodatkowo `libgtk-4-dev libepoxy-dev` (Ubuntu) lub
`gtk4 libepoxy` (Arch/Omarchy); `./run.sh dependencies plan` pokazuje polecenie.

---

Poniżej opis wersji 0.12 i wcześniejszych (silnik bez zmian; fragmenty o
oknach Qt dotyczą `ky-prism-qt`).

0.12 dodaje rzeczywisty opcjonalny dekoder nvJPEG/CUDA oraz konwersję RGB→RGBA
z wyborem AVX2 podczas działania. Nowy backend opisano na końcu dokumentu.

© 2026 Krzysztof Kylen Lenartowicz.

C++20 / Qt 6 Widgets / OpenGL 3.3 / libvips / LibRaw / Little CMS.
Jest to nowa aplikacja przygotowana do testu w sesji użytkownika. Testy dekodera
nie zastępują testu rzeczywistego okna, monitora, sterownika RTX i plików RAW aparatu.

## Instalacja i uruchomienie

Z projektu remote-desktop:

```sh
./run.sh photos install
ky-prism /ścieżka/zdjęcie.jpg
```

Program instaluje się na koncie użytkownika, bez autostartu. Brakujące zależności instalator dobiera z repozytoriów dystrybucji; wtedy wymaga sudo i połączenia z repozytoriami. Ustawia ky-prism jako domyślną przeglądarkę obsługiwanych zdjęć.
Wymagane istniejące pakiety: gcc, make, cmake, pkgconf, qt6-base,
libvips, libraw, lcms2, libjpeg-turbo, libtiff, libglvnd i sterownik OpenGL.
`./run.sh photos dependencies check` sprawdza zależności, `plan` pokazuje polecenie, a `install` instaluje brakujące. W nowym systemie zapewnia je pełny eksport
Omarchy z dokładnymi pakietami offline. Zależności wynikają także z pkg-config.

Launcher stosuje PRIME tylko do procesu ky-prism: NVIDIA + GLX na XWayland.
Nie zmienia `~/.config/uwsm/env.d/20-nvidia-render.conf`.
Stopka pokazuje rzeczywisty GL_RENDERER; nazwa NVIDIA/RTX oznacza używany renderer.
Domyślny `PRISM_GPU=auto` wybiera NVIDIA, gdy sterownik jest dostępny i działa XWayland. `PRISM_GPU=system ky-prism zdjęcie.jpg` dziedziczy GPU sesji; `PRISM_GPU=nvidia` wymusza PRIME tylko dla tego procesu.

Kółko/trackpad: zoom pod kursorem. Przeciągnięcie: przesuwanie. `1`: dokładnie
jeden piksel fotografii na fizyczny piksel framebufferu. `F`: dopasowanie.
Strzałki / przyciski ‹ ›: poprzednie/następne zdjęcie w katalogu. F11: fullscreen,
Esc: wyjście z fullscreen. `×` lub Ctrl+W: zamknięcie. EXIF: panel metadanych.
Można przeciągnąć lokalny plik na okno. Lista katalogu ma limit 50 000 wpisów.

## Obraz i kolor

* JPEG/TIFF/PNG: oryginalna orientacja EXIF jest uwzględniana.
* Profile osadzone są przeliczane przez libvips/Little CMS, relative colorimetric.
  Domyślne wyjście to sRGB. Bez profilu wejściowego przyjmowane jest sRGB;
  CMYK bez ICC oraz float/HDR są odrzucane zamiast zgadywania barw.
* Konwersja oraz kafelki i tekstury są 16-bitowe. Okno jest obecnie SDR;
  nie gwarantuje 10-bitowego wyjścia monitora ani HDR.
* `ky-prism --display-profile /ścieżka/monitor.icc zdjęcie.tif` pozwala
  ustawić profil wyświetlacza. Nie ma automatycznej obsługi profili wielu
  monitorów. Nie stosuj ręcznego profilu drugi raz, jeśli kompozytor wykonuje
  już tę samą transformację. Bez kalibracji nie da się obiecać fizycznej wierności.
* Przy 1:1 nie ma wyostrzania, rozmycia ani efektu kolorystycznego. Redukcja
  wykorzystuje Lanczos3, GPU interpolację biliniową przy skalowaniu.
* RAW jest wywoływany przez LibRaw: 16-bit, sRGB, balans aparatu, AHD, bez
  automatycznego rozjaśniania. Nie odtwarza własnego wyglądu JPEG producenta.
  Limit RAW: 160 Mpx, limit pamięci surowego dekodera 2 GiB. Cały proces może
  potrzebować więcej RAM ze względu na wynik i bufory robocze.
* TIFF wielostronicowy: pierwsza strona. Maks. geometria: 2,5 Gpx / 1 mln px
  na wymiar. Rozmiar pliku 2 GB nie jest rozmiarem rozkodowanego obrazu.

## Wydajność i czas życia

GUI nie dekoduje ani nie czyta EXIF. Jedna kolejka robocza wykonuje odczyt,
libvips używa maks. 4 swoich wątków. Skan katalogu działa osobno. Przy zmianie
pliku stare wyniki nie trafiają do nowego zdjęcia; anulowanie jest kooperacyjne
(w dekoderze może zaczekać do kolejnego punktu kontrolnego).

Kafelki 512 px z marginesem eliminującym szwy; wyświetlany jest potrzebny poziom
rozdzielczości. RAM LRU 256 MiB (także ostatnie zdjęcia), VRAM LRU maks. 768 MiB na widok (do 1536 MiB dla dwóch zdjęć),
alokowana stopniowo, zwykle znacznie mniej. Jest to limit tekstur zdjęć, nie
całego zużycia procesu/sterownika. Duże widoki do 8K mogą potrzebować setek MiB.
Wewnętrzny cache operacji vips jest wyłączony, żeby nie przetrzymywał całych RAW.

Dekoder JPEG to libjpeg-turbo przez libvips; CPU dekoduje, GPU renderuje.
Kod nie udaje sprzętowego dekodera CUDA. JPEG/sekwencyjny TIFF mogą wymagać
materializacji na dysku: prywatny katalog sesji w ~/.cache/omarchy-prism,
usuwany przy normalnym zamknięciu. Wymagane wolne miejsce na rozpakowany obraz.
Po awarii procesu katalog sesji może zostać do ręcznego usunięcia. Program
nie ma sieci, telemetrii ani daemonów. Po bezczynności nie animuje obrazu; pojemność dysku odświeża co 30 sekund tylko przy widocznym oknie.
Zamknięcie chowa okno, anuluje pracę i czeka na bezpieczne zwolnienie dekodera.

Paleta pochodzi z ~/.local/state/omarchy/current/theme/colors.toml i jest
obserwowana zdarzeniowo. Fotografia pozostaje na neutralnym, kryjącym tle;
przezroczyste są tylko elementy oprawy. Instalator dodaje osobny prism.lua
z wyjątkiem od globalnej przezroczystości oraz włącza istniejący blur kompozytora.
Nie zwiększa liczby przebiegów blur. Jednorazowa animacja dotyczy oprawy.
Przy XWayland i skalowaniu ułamkowym kompozytor może dodatkowo skalować okno;
dla krytycznego 1:1 sprawdź tryb skalowania XWayland/monitora.

## Backup, eksport, odtworzenie

```sh
./run.sh photos verify
./run.sh photos export /ścieżka/do/paczek
./run.sh photos backup /ścieżka/do/kopii
```

Samodzielna paczka zawiera źródła, CMake, testy, instalator, manifest SHA-256,
ikonę, launcher i pomocniczą binarkę, zależności dla APT/pacman oraz wszystkie trzy pluginy paska. Po sprawdzeniu `.sha256` rozpakuj ją
w wybranym katalogu, wejdź do `ky-prism` i wykonaj `./run.sh install`.
Instalator przebudowuje źródła dla bieżącego systemu; nie zakłada zgodności ABI
starej binarki z przyszłym Qt. Paczka backup zawiera też zainstalowane pliki.
Nie zawiera zdjęć, cache ani zależności systemowych.

Główne `./run.sh backup` / `export` / `finish --validated` uwzględniają
~/.local/share/omarchy-prism, launcher, desktop, ikonę, reguły Hyprlanda i źródła.
Audyt krytycznych plików pilnuje obecności instalacji, jeśli jest zainstalowana.
Po potwierdzeniu działania live wykonaj `./run.sh finish --validated` w
remote-desktop. Nie oznaczaj samej kompilacji jako potwierdzenia live.

Instalator zapisuje poprzednie pliki w ~/.local/state/omarchy-prism/install-backups.
Przy błędzie instalacji cofa własne zapisy. Żadne pliki /usr/share/omarchy nie są
modyfikowane. Aktualizacja ABI Qt/libvips może wymagać ponownej kompilacji;
nie deklarujemy odporności binarki na każdą przyszłą aktualizację.

## Testy

`./run.sh photos test`: kompilacja z ostrzeżeniami jako błędami, dekoder i shader.
Test shaderowy używa EGL surfaceless; może używać llvmpipe i nie jest dowodem RTX.
`photo-viewer/build/prism-large-test photo-viewer/build`: dodatkowy test tworzący
>2 GB tiled BigTIFF i >200 MB JPEG. Usuwa dane po zakończeniu, wymaga miejsca.

Dokumentacja podstawowa:
- https://omarchy.org/manual/
- https://www.libvips.org/API/current/how-it-works.html
- https://doc.qt.io/qt-6/qopenglwidget.html
- https://www.libraw.org/docs/API-datastruct.html
- https://download.nvidia.com/XFree86/Linux-x86_64/595.71.05/README/primerenderoffload.html
- https://wiki.hypr.land/Configuring/Basics/Window-Rules/

## Zmiany 0.2

- Panel katalogów z asynchronicznym QFileSystemModel, nazwami i rozmiarami,
  przejściem w górę, paskiem ścieżki oraz regulowaną szerokością. Folder można
  podać jako argument uruchomienia. Dwuklik otwiera katalog lub zdjęcie.
- Dialogi Qt bez natywnego podglądu GTK (obejście potwierdzonej awarii glycin).
- Płynna animacja zoomu 110 ms, podgląd pozostający pod kafelkami, zachowanie
  już dostępnych niższych poziomów, doczytywanie detalu po zakończeniu zoomu.
- JPEG: szybki podgląd przez shrink-on-load; przygotowany poziom trafia również
  do cache detalu. Cache podglądów 128 MiB, kafelków 256 MiB, zmaterializowanych
  poziomów do 384 MiB. Dekoder JPEG może używać do 512 MiB RAM przed przejściem
  na plik tymczasowy. Są to limity różnych warstw, nie całego procesu.
- „Otwórz za pomocą”: Added Associations oraz absolutny Exec w desktop entry;
  zachowanie istniejących Defaults. Dodano rozpoznawanie rozszerzenia RAW .fff.
- Nie przepisano warstwy sterującej na C: ciężki dekoder libvips/libjpeg już
  korzysta z natywnego kodu; problem był w strategii ładowania i renderowaniu.

Scenariusze automatyczne i plan prób na prawdziwym ekranie: TEST_PLAN.md.

Historyczne ograniczenie 0.2 (usunięte w 0.9): Hasselblad_0138+FIN.tif ma blok Photoshop
przekraczający limit 50 MiB dekodera libtiff/libvips i w tej wersji pozostaje
nieobsługiwany. Nie wyłączamy globalnie limitów parsera. Pozostałe 28 próbek
JPEG/TIFF przeszło odczyt; szczegóły w VALIDATION.md.

## Zmiany 0.3

Pojedyncze kliknięcie otwiera zdjęcie; aktywacja katalogu pozostaje dwuklikiem / Enter.
Przycisk Pliki pokazuje i ukrywa panel. 1:1 ustawia skalę natychmiast i centruje;
↺ Widok przywraca dopasowanie, ▣ Okno ustawia rozmiar według zdjęcia do granic monitora.
Prism jest domyślnie oknem pływającym, żeby menedżer kafelkowy nie blokował rozmiaru.
Po ręcznym włączeniu kafelkowania o rozmiarze okna decyduje Hyprland.

Oprawa korzysta z kolorów aktualnego motywu, lokalnych fontów Inter / Adwaita Sans /
Noto Sans, powierzchni alpha 175/255 i istniejącego blur Hyprland. Samo zdjęcie
pozostaje nieprzezroczyste. Brak globalnego włączania blur, jeśli użytkownik go wyłączył.
Ekran powitalny ma statyczny, wektorowy aparat z uśmiechem.

Plugin kylen.prism dodawany jest po delfinie, bez przestawiania reszty paska.
Animacja przysłony trwa 1,1 s; Loader usuwa nakładkę po zakończeniu, bez cyklicznych
odświeżeń. W shell.json można ustawić animate:false. Wektory Qt Quick Shapes
skalują się bez bitmap: https://doc.qt.io/qt-6/qml-qtquick-shapes-shape.html .
Instalator tworzy kopię poprzedniego shell.json i mimeapps.list, scala układ idempotentnie,
zachowuje skojarzenia innych typów. Plugin i helper należą do install/export/backup.

## Zmiany 0.4

„▣ Oryginał” ustawia fizyczną skalę 1:1 i centruje zdjęcie wewnątrz bieżącego
okna. Nie zmienia rozmiaru okna, fullscreen ani maksymalizacji. Zdjęcie można
przeciągać lewym przyciskiem; kółko nadal powiększa. „↺ Widok” przywraca
wycentrowane dopasowanie. Dodano przechwytywanie krawędzi okna również nad
widżetami potomnymi, żeby obszar zdjęcia nie blokował skalowania ramki.

Ikona to czarne, wektorowe koło z siedmioma trójkątami, z przezroczystym tłem.
Siedem odnosi się do popularnego układu siedmiu głównych czakr, a nie do jednej
uniwersalnej liczby we wszystkich tradycjach:
https://himalayaninstitute.org/online/series/chakras-back-basics/ .
Animacja trwa 720 ms, ma siedem listków, skalę ostrości i śruby montażowe.
Proces aplikacji uruchamia się od razu, równolegle z animacją; Loader znika na końcu.

JPEG z miniaturą EXIF pokazuje najpierw oznaczony, tymczasowy podgląd. Pełny
podgląd zastępuje go po dekodowaniu i nie resetuje zoomu ani przesunięcia
wybranego w międzyczasie. Miniatura może mieć inne opracowanie lub barwy niż
oryginał; nie zastępuje pełnego obrazu. Limit miniatury: 2 MiB danych i 2048 px
na wymiar. Uszkodzona miniatura nie uniemożliwia odczytu poprawnego zdjęcia.
Źródło API: https://www.libvips.org/API/current/ctor.Image.jpegload.html .

Cache 128 MiB przechowuje gotowe piksele podglądu wraz z pasującym poziomem
piramidy, współdzieląc jedną alokację QImage. Ponowny wybór zdjęcia nie wymaga
ponownego dekodowania tego podglądu. Podglądy dyadyczne do 2048 px zachowują
więcej szczegółów; mniejsze poziomy korzystają z już gotowych pikseli zamiast
ponownie czytać pełny plik. TIFF także zachowuje przygotowany poziom. Dokładny
widok 1:1 nadal odczytuje oryginalne dane i przechodzi ten sam tor ICC/16 bit.
Głębszy zoom może wymagać pełnego rozkodowania JPEG-a. Nie uruchamiamy zbędnego
dekodowania wysokiej rozdzielczości w tle, jeśli użytkownik tylko ogląda całość.

Pomiary dekodera na 26 lokalnych JPEG-ach: 26/26 poprawnych. Mediana pierwszego
podglądu 16 ms, pełnego 509 ms, ciepłego otwarcia 4 ms. Nie są to pomiary
renderowania okna RTX. Pliki bez miniatury czekają na pełny podgląd (do 1103 ms
w tym zbiorze). Szczegóły: reports/0.4-jpeg-corpus.json i VALIDATION.md.


## Zmiany 0.5 — maksymalizacja i ulubione

Przycisk □ maksymalizuje i przywraca okno. W Hyprland używa krótkiego,
asynchronicznego polecenia natywnego dla własnego PID i głównego okna, ponieważ
Omarchy domyślnie tłumi żądania maksymalizacji aplikacji. Bez Hyprland działa API Qt. F11 z maksymalizacji wraca do tego
samego stanu po wyjściu z pełnego ekranu. Nowa czarna ikona paska to minimalistyczna
ramka fotografii z linią krajobrazu; tło jest przezroczyste, geometria wektorowa.

Serduszko dodaje aktualny plik do ulubionych po obliczeniu MD5 w tle i udanym
zapisie. Staje się czerwone i przez 550 ms rysuje promienie. Ponowne kliknięcie
usuwa wpis. Przycisk ♥≡ otwiera listę wszystkich ulubionych z filtrem nazwy,
ścieżki i MD5. Otwórz / dwuklik wyświetla zdjęcie; Usuń nie kasuje oryginału.

Dane: `$XDG_DATA_HOME/omarchy-prism/favorites.json`, domyślnie
`~/.local/share/omarchy-prism/favorites.json`. Zapisuje UUID, nazwę, pełną ścieżkę,
rozmiar i MD5. Nie kopiuje zdjęć. Zapis atomowy QSaveFile, blokada QLockFile,
limit 10 000 wpisów / 8 MiB; uszkodzony JSON nie jest nadpisywany. Równoległe
okna odczytują aktualną listę przed zapisem i obserwują zmiany bez odpytywania.

„Odnajdź zdjęcie” szuka w wybranym katalogu i podkatalogach, uwzględnia także
zmienione nazwy. Najpierw porównuje rozmiar, następnie strumieniowe MD5. Pomija
nieczytelne pliki i dowiązania podkatalogów. Limit 500 000 wpisów i 20 zgodnych
kopii jest jawnie sygnalizowany; można wskazać węższy katalog. Kilka kopii wymaga
wyboru ścieżki. Aktualizacja ponownie sprawdza MD5, więc błędny plik nie zastąpi
wpisu. Operacje da się anulować; blok pamięci haszowania to 1 MiB. MD5 służy tu
identyfikowaniu zawartości, nie weryfikacji autentyczności podpisu cyfrowego.

Install zachowuje istniejące ulubione. Export/backup dołącza listę jako
`user-data/favorites.json`; nowa instalacja odtwarza ją tylko, gdy nie ma własnej
listy. Po migracji ścieżki można zaktualizować funkcją odnajdywania. Pełny backup
konta także obejmuje ten katalog. Nie oznaczono przygotowanych zmian jako live.

Stabilność: 160 otwarć z wymianą cache, plateau RSS ok. 267 MiB, rozrzut 0,15 MiB
po rozgrzaniu i ok. 77 MiB po clear; raport `reports/0.5-memory.txt`. ASan/UBSan
dekodera i ulubionych bez błędów. Nie jest to dowód braku wszelkich wycieków;
VRAM i rzeczywiste okno RTX trzeba dodatkowo obserwować live.

## Nawigacja i operacje na plikach (0.6)

Strzałki ← / → nad listą katalogów i Alt+Lewo / Alt+Prawo cofają/przywracają
katalog. Historia mieści 100 pozycji; wejście do nowego katalogu po cofnięciu
usuwa dalszą gałąź historii. Przyciski ‹ / › na górze nadal zmieniają zdjęcia.
Gwiazdka obok serca zapisuje do 128 katalogów. Osobna przeglądarka zakładek
otwiera je, filtruje i pozwala usunąć samą zakładkę. Lista zapisuje się atomowo w bookmarks.json obok favorites.json;
uszkodzony JSON nie jest automatycznie nadpisywany.

Menu „⋯” zawiera „Kopiuj zdjęcie…” i „Znajdź duplikaty plików…”. Kopiowanie
otwiera animowany dialog z wyborem miejsca, nazwą, przyciskiem zapisu i anulowania.
Przesyła oryginalne bajty (łącznie z EXIF/ICC), bez dekodowania i rekompresji.
Istniejącego celu nie nadpisuje, także gdy inny program utworzy go podczas
kopiowania. Plik tymczasowy jest w katalogu docelowym; renameat2(RENAME_NOREPLACE) publikuje go
atomowo po fsync. Dla starszych systemów plików stosuje bezpieczny link(2);
jeżeli obie metody są niedostępne, odmawia zapisu bez usuwania źródła.
Zmiana źródła lub anulowanie przed publikacją usuwa plik tymczasowy.
Anulowanie po opublikowaniu kompletnej kopii nie cofa już zapisanego pliku.

Duplikaty: wybierz katalog, skanowanie obejmie zwykłe pliki, także ukryte,
rekurencyjnie, z pominięciem dowiązań. Najpierw nazwa i rozmiar, potem MD5
wyłącznie dla potencjalnych kopii. Nazwa musi być identyczna, z uwzględnieniem
wielkości liter. Limit 200 000 wpisów: po jego przekroczeniu wybierz mniejszy
katalog; nie przedstawiamy częściowej listy jako kompletnej. Błąd odczytu lub
zmiana pliku przerywa porównywanie. Lista pokazuje grupy i pełne ścieżki;
wybierz jedną ścieżkę, „Przenieś zaznaczony do kosza”, następnie potwierdź
w osobnym oknie (domyślnie Anuluj). Program ponownie sprawdza MD5/rozmiar/nazwę,
fingerprint stat i drugą dostępną kopię, dodatkowo porównując bajty. Odrzuca
usunięcie ostatniej potwierdzonej kopii. Nie ma trwałego usuwania ani zastępowania
nieudanego przeniesienia do kosza trwałym kasowaniem. MD5 nie jest dowodem
bezpieczeństwa ani ochroną przed złośliwym równoczesnym podmienianiem plików
przez inny proces; przy celowych zmianach zewnętrznych nie ma transakcji między
kilkoma plikami. Zachowuj kopie ważnych zdjęć.

Jeden dialog operacji i jeden jego wątek roboczy, bufor kopiowania 1 MiB,
postęp najwyżej co 100 ms, anulowanie między blokami. Brak skanowania/haszowania
w bezczynności. Animacja wejścia trwa 180 ms, a pasek postępu kończy animowanie
z operacją. Zamknięcie pracującego dialogu anuluje zadanie; po zakończeniu można
zamknąć okno. Zamknięcie aplikacji anuluje i dołącza wątek.

Przycisk maksymalizacji używa natywnego trybu Hyprland „maximized” (z paskiem),
wybierając własny PID i tytuł głównego okna. hl.get_windows w Hyprland 0.56.2
ma filtry dosłowne, a nie regex: naprawiono błędny filtr klasy z 0.5.
Żądanie omija handler układu i sprawdza stan wewnętrzny po wykonaniu.
Omarchy: https://omarchy.org/manual/navigation/
Źródło API: https://github.com/hyprwm/Hyprland/blob/v0.56.2/src/config/lua/bindings/LuaBindingsQuery.cpp

Instalator dodaje „kylen.prism-terminal” bezpośrednio za „kylen.prism”,
zachowując pozostałe elementy paska i ich ustawienia. Przycisk uruchamia
Alacritty w oddzielnej usłudze uwsm; nie ustawia środowiska GPU i nie odpytuje
niczego w tle. Oba lokalne pluginy, helpery i źródła obejmuje rejestr instalacji,
backup/export. Listy ulubionych i zakładek są danymi użytkownika: przy przenoszeniu
są odtwarzane tylko wtedy, gdy konto docelowe jeszcze ich nie ma.


## 0.7 — nazwa, zakładki, autor i klawiatura

Program i polecenie nazywają się `ky-prism`. Nowa pozycja w menu i „Otwórz
za pomocą” używa ky-prism.desktop i tego samego programu. Poprzednie polecenie
omarchy-prism uruchamia aktualną wersję dla zgodności z zapisanymi skrótami;
stary wpis desktop jest ukryty i nie reklamuje osobnych skojarzeń MIME.
Dane w ~/.local/share/omarchy-prism (ulubione, zakładki, rejestr i źródła)
zachowują swoje położenie, dzięki czemu zmiana nazwy nie wymaga ich przenoszenia.
`./run.sh photos` nadal obsługuje instalację, testy, eksport i backup.

Gwiazdka ☆ / ★ znajduje się bezpośrednio obok serca. Zapisuje / usuwa zakładkę
bieżącego katalogu; ☆≡ otwiera przeglądarkę z nazwami, pełnymi ścieżkami,
filtrem i przyciskami otwierania, dodawania oraz usuwania odwołań. Przycisku
zakładek nie ma już w lewym panelu. Usunięcie zakładki nie usuwa katalogu.

About otwiera wektorową animację obiektywu (640 ms), nazwę programu i autora
Krzysztof Kylen Lenartowicz oraz informację © 2026. Osobna strona zawiera skróty
klawiatury. Animacja zatrzymuje się po jej zakończeniu, przejściu do skrótów,
ukryciu dialogu i zamknięciu programu. Kolory pochodzą z bieżącej palety Omarchy.
Informacja o prawach: COPYRIGHT.md; pełna instrukcja klawiatury: KEYBOARD.md.

Sprawdzane są rzeczywiste zdarzenia klawiatury Qt, nie tylko wywołania click():
nawigacja, edycja ścieżki, gwiazdka i przeglądarka, serce i ulubione, sortowanie,
Enter w listach, kopiowanie pliku, wybór ścieżki, wyszukiwanie duplikatów,
anulowanie oraz potwierdzenie kosza, EXIF, zoom, pan, okno i pełny ekran.
Zwykłe F / 1 / strzałki są ograniczone do fokusu na zdjęciu. Tab/Shift+Tab
obejmują kontrolki, również serce; Enter uruchamia skupiony przycisk.
Ctrl+C zachowuje standardowe kopiowanie tekstu, kopiowanie zdjęcia ma Ctrl+Shift+C.
Nie dodano odpytywania ani animacji w tle.


## Zmiany 0.8 — biblioteka, pliki i porównanie

- Biblioteka: pojedynczy klik otwiera zdjęcie; Ctrl/Shift zaznacza wiele plików.
  Sortowanie: nazwa (naturalna kolejność 2 przed 10), wielkość, data zmiany,
  czas utworzenia; przycisk ↑/↓ odwraca kierunek. Brak czasu utworzenia
  w systemie plików oznacza „—”, a nie zgadywanie na podstawie mtime/ctime.
  Daty utworzenia są pobierane w jednym anulowalnym wątku, do 50 000 wpisów.
- Kopiowanie ma własną ikonę na pasku. Jeden plik: wybór nazwy; wiele plików:
  wybór katalogu, przegląd źródeł, łączny postęp. Do 4096 plików, po 1 MiB
  bufora, bez dekodowania zdjęć. Wcześniej wykryty konflikt blokuje całą paczkę.
  Kolizja podczas kopiowania też nie nadpisuje pliku. Kopie ukończone przed
  anulowaniem lub późniejszym błędem pozostają na dysku; komunikat podaje liczbę.
- Karty: Ctrl+T nowa, Ctrl+Tab / Ctrl+Shift+Tab przełączanie,
  Ctrl+Shift+W zamyka kartę. Maksymalnie 24. Zapamiętują plik, katalog,
  historię i zdjęcie porównania, bez osobnych dekoderów i tekstur dla każdej karty.
  Przełączenie przywraca dopasowanie obrazu (nie poprzedni zoom).
- Porównanie: zaznacz dwa zdjęcia i kliknij ikonę dwóch obrazów, albo otwórz
  pierwsze i wybierz drugie. Ctrl+Shift+P włącza/wyłącza porównanie.
  Widoki mają niezależny zoom/pan. Zamknięcie porównania zwalnia jego cache.
- Pendrive: Ctrl+U / ikona USB. Pokazuje tylko nośniki USB/removable,
  przy kilku pozwala wybrać. Niezamontowany dysk montuje przez `udisksctl`
  z autoryzacją systemową, bez sudo i bez zmian w fstab. Szyfrowane LUKS
  należy wcześniej odblokować w programie Pliki. Nie ma skanowania USB w tle.
- Stopka podaje dostępną dla użytkownika pojemność aktualnego dysku.
- Pasek ma oddzielny tytuł i kontrolki okna; narzędzia są wyrównane i mają
  nazwy dostępności. Pole ścieżki i EXIF mają alpha; blur zapewnia kompozytor.
- Chrome za ikoną Alacritty: znak monochromatyczny według palety, podczas
  uruchamiania 660 ms animacji wektorowego logo w kolorach Chrome.
- Na OpenGL >=4.2 / ARB_texture_storage tekstury używają immutable storage,
  a na starszym GL pozostaje bezpieczna ścieżka 3.3. Dane obrazu nadal RGBA16.
- Testy UI działają na zwykłym QWidget z podglądem CPU w trybie offscreen,
  bez tworzenia nieobsługiwanego QOpenGLWidget. Test shadera EGL jest osobny.
  `./run.sh photos gpu-test` sprawdza prawdziwe okno OpenGL, kolory i zmianę
  rozmiaru w sesji pulpitu. Nie zastępuje go test offscreen ani llvmpipe.
- Identyfikator desktop jest ustawiony przed QApplication. Prywatna oprawa
  Fusion nie ładuje platformowego motywu GTK, który może konkurować o rejestrację
  tej samej sesji DBus. Nie wyłączamy portalu ani nie filtrujemy ostrzeżeń Qt.

Obsługiwane platformy źródłowe: Omarchy/Arch, Ubuntu >=24.04, Debian >=12,
Qt >=6.4, OpenGL >=3.3. Na Debianie/Ubuntu instalator przebudowuje program
z lokalnymi bibliotekami i nie instaluje pluginów Omarchy bez samego Omarchy.
Te ścieżki pakietów są przygotowane; testy bieżące wykonano na Omarchy, nie
w osobnych instalacjach wszystkich dystrybucji. Binarka z Arch nie jest przenośnym
pakietem ABI dla Ubuntu/Debiana — używaj źródeł i instalatora.

Walidacja odrzuca NUL, nieodwracalne kodowanie i nadmierną długość ścieżek;
przyjmuje polskie litery, spacje, nawiasy i metaznaki bez wykonywania powłoki.
JPEG/TIFF/PNG mają jawnie ograniczone dekodery, RAW limit geometrii/pamięci,
metadane limit wyświetlania. To ogranicza ryzyko, ale nie dowodzi braku wszystkich
błędów bibliotek C/C++; aktualne poprawki libvips/LibRaw/Qt pozostają konieczne.


## Zmiany 0.9 — ostre podglądy, biblioteka i oprawa

Podgląd uwzględnia proporcje zdjęcia, obszar fotografii i fizyczną skalę ekranu.
Dekoder JPEG dobiera najszybszy poziom DCT, który ma dość szczegółów do tego
widoku. Nie rozciąga małej miniatury EXIF na duży obszar. Powiększenie do 1:1
nadal czyta oryginalne piksele przez dotychczasowy tor ICC/16 bit, bez wyostrzania.
Ostre podglądy są współdzielone w RAM: 1/32 pamięci fizycznej, od 128 MiB do
1 GiB (na laptopie 32 GiB: około 1 GiB). `PRISM_PREVIEW_CACHE_MIB=256 ky-prism`
pozwala zmniejszyć ten budżet; zakres ustawienia 64–1024 MiB, bez przekraczania
budżetu wynikającego z RAM. Jest to limit tego cache, nie całego procesu.
Dwa sąsiednie JPEG-i mogą zostać przygotowane po zakończeniu bieżącego odczytu.
Jedna kolejka o niskim priorytecie; nowy odczyt, ukrycie/minimalizacja okna
lub zamknięcie przerywają ją. Nie ma ciągłego odpytywania w bezczynności.

Biblioteka ma tryby Lista/Miniatury, wspólne zaznaczenie i cztery sortowania.
Menu sortowania otwiera się pod przyciskiem. Miniatury są dekodowane poza GUI,
tylko na żądanie widocznych pozycji, z kolejką do 24 oczekujących i cache 48 MiB.
Podczas głównego odczytu są wstrzymane, a po ukryciu panelu/okna anulowane.
Duże TIFF-y i RAW-y również mają miniatury; brak osadzonego podglądu może
wymagać wolniejszego odczytu. Oryginały pozostają niezmienione.

Pliki FFF najpierw sprawdza LibRaw. Tylko jeśli zgłosi nieobsługiwany RAW,
przeglądarka dopuszcza rozpoznany pełny TIFF RGB/CMYK/Lab. Pomniejszony IFD
wewnątrz RAW nie jest podawany za pełny oryginał; przy braku wsparcia LibRaw
pozostaje miniatura w bibliotece oraz czytelny błąd po otwarciu. TIFF z dużymi
prywatnymi warstwami Photoshop jest czytany przez ograniczony adapter tylko do
odczytu. Adapter pomija duże prywatne tagi warstw 37724/34377, zachowuje offsety
obrazu, profil ICC i EXIF. Nie zmienia pliku i nie wyłącza limitów libtiff;
przeglądarka pokazuje złożony obraz pierwszej strony, nie edytowalne warstwy.

Pasek kart znika przy jednej karcie; ⊞ / Ctrl+T tworzy kolejną. W porównaniu
każde zdjęcie ma własne ×; drugie pozostaje otwarte. Otwarcie nowej pary
przywraca równe szerokości. Nagłówek biblioteki i przyciski mają paletę motywu,
lekki bevel/gradient oraz stany hover, fokus, wciśnięcie, wybór i wyłączenie.
Są malowane zdarzeniowo, bez stałej pętli animacji. Kontur ikony Chrome jest
cieńszy; kolorowa animacja uruchomienia pozostaje bez zmian.

## 0.10 — szkło, bezpośrednie akcje i sygnalizacja ładowania

Pasek nie zawiera pionowych separatorów ani menu „…”. Duplikaty mają własny
przycisk obok kopiowania. Cztery przyciski A–Z / MB / Utw. / Zmi. zmieniają
sortowanie bez okna wyskakującego; strzałka odwraca kolejność. Każdy przycisk
jest osiągalny Tabem, a Enter/Spacja go aktywuje. Wskaźnik zoomu pozostaje pusty,
gdy nie ma zdjęcia; lupy służą do przybliżania i oddalania.

Wspólny materiał kontrolek ma półprzezroczyste wypełnienie, cienką krawędź,
miękki refleks oraz osobne stany fokusu, wciśnięcia, wyboru i niedostępności.
Ikony są wektorowe, inspirowane dostarczonymi przykładami; ich paleta zmienia
się wraz z Omarchy. Okna kopiowania i duplikatów mają spójny nagłówek, przycisk
zamknięcia, kartę celu i skalowalny rozmiar. Zmiana motywu obejmuje także
już otwarte okna. Alt+W nadal wybiera miejsce/katalog, Alt+Z zapisuje kopię.

Klepsydra 30 fps jest osobnym małym obszarem nad zdjęciem. Pojawia się od
rozpoczęcia odczytu, znika przy pierwszym podglądzie lub błędzie i nie wymusza
odświeżania całego obrazu. Ukrycie/minimalizacja zatrzymują timer; przywrócenie
wznawia go tylko podczas odczytu. Dekoder wybranego zdjęcia dostaje normalny
priorytet wątku, a miniatury i prefetch pozostają na niskim priorytecie.

Materiał jest cieniowany analitycznie przez QPainter na żądanie. Nie dodano
ciągłego shadera tła ani osobnego kontekstu OpenGL do każdego przycisku.
Istniejący renderer zdjęć OpenGL, ostre podglądy, ICC i limity cache zachowano.
Szczegóły pomiarów, analizy NVIDIA i ograniczeń: `reports/0.10-validation.txt`.

## 0.10.1 — obsługa myszy i przeciągania okien

Kliknięcie w nagłówek lub ramkę nie rozpoczyna już natychmiast systemowego
przesuwania/skalowania. Operacja zaczyna się po przekroczeniu progu
przeciągnięcia z nadal przytrzymanym lewym przyciskiem, w widocznym aktywnym
oknie. Zdarzenia syntetyczne i punkty spoza okna są odrzucane. Przyciski,
pola tekstowe, listy i zdjęcia zachowują własną obsługę myszy.

Zwolnienie przycisku, utrata aktywności, ukrycie i zamknięcie anulują
oczekujące przeciąganie. Zwalniane jest wyłącznie przechwycenie myszy należące
do danego okna. Ta sama logika obejmuje szklane okna operacji plikowych.


## 0.10.2 — stabilne miniatury przy zmianie szerokości biblioteki

Wczytana miniatura ma jeden stały, ograniczony do 512 px wpis cache dla pliku,
nie osobny wpis dla każdej chwilowej szerokości komórki. Przeciąganie separatora
skaluje gotowy obraz bez ponownego dekodowania i pustych mignięć. Limit cache
pozostaje 48 MiB. Jakość zdjęcia w głównym widoku i jego cache pozostają takie same.

Przeliczanie komórek jest łączone w jeden przebieg co najwyżej co 16 ms podczas
zmian rozmiaru; po zakończeniu nie ma aktywnego timera. Stałe miejsce paska
przewijania zapobiega zmianom szerokości viewportu podczas układania kolumn.
Uchwyt splittera i QSizeGrip mają własną obsługę myszy i są wyłączone z gestów
przesuwania/skalowania całego okna.

Separator biblioteki pokazuje podczas przeciągania linię położenia. Rozmiar
paneli zostaje zatwierdzony po puszczeniu przycisku, aby nie przebudowywać
bufora OpenGL zdjęcia przy każdym ruchu o piksel. To ustawienie dotyczy
separatora biblioteki, nie jakości renderowania fotografii.


## 0.11.0 — duża biblioteka miniaturek i GPU

Miniatury powstają poza wątkiem interfejsu, w 1–4 równoległych zadaniach
(zależnie od liczby dostępnych CPU), z niskim priorytetem i ograniczoną kolejką.
Widoczne komórki zamawiają obrazy; program nie otwiera od razu 800 oryginałów.
Przewijanie usuwa nieaktualną kolejkę, a kilka już rozpoczętych zadań może
zakończyć się i zasilić cache. Ukrycie/minimalizacja/lista zatrzymują pracę;
odczyt głównego zdjęcia ma pierwszeństwo. Po zakończeniu nie działa timer animacji.

Każdy plik ma jeden docelowy podgląd do 512 px. Najpierw odczytywane są
osadzone podglądy aparatu widocznych JPEG-ów, a potem dekodowane szczegółowe
wersje. Mała osadzona miniatura jest tylko przejściowym podglądem: zastępuje ją
wersja 512 px, bez pustej klatki i bez ponownego układania komórek. Nie jest
zapisywana jako końcowy podgląd w cache dyskowym. libvips/libjpeg-turbo wykonują zmniejszanie
przy dekodowaniu, zachowując orientację i konwersję ICC do sRGB. Oryginały i
renderowanie głównego zdjęcia pozostają niezmienione.

Cache RAM przydziela się na żądanie: domyślny budżet to 1/32 RAM, ograniczony
do 128–1024 MiB. `PRISM_THUMB_RAM_MIB=256 ky-prism` ustawia inny budżet w tych
granicach. Osobny, bezstratny cache dyskowy jest w
`~/.cache/ky-prism/thumbnails-v3` (lub XDG_CACHE_HOME). Ma około 1 GiB limitu;
porządkowanie następuje co 32 zapisy, więc limit może być chwilowo przekroczony.
Usuwane są najstarsze zapisane pliki. Cache można bezpiecznie usunąć; nie jest
kopiowany jako źródło fotografii i nie jest potrzebny przy odtwarzaniu systemu.
`PRISM_THUMB_DISK_CACHE=0` wyłącza go. Pliki mają prywatne uprawnienia 0600;
nowy katalog 0700. Czytnik sprawdza geometrię, rozmiar i SHA-256 przed użyciem.

Siatka używa jednego viewportu OpenGL, tekstur RGBA8 i mipmap z filtrowaniem
trójliniowym. Gotowa tekstura pozostaje na GPU podczas przewijania i resize;
nie powstaje osobny kontekst dla każdego pliku. Limit własnych tekstur siatki
wynosi 512 MiB (osobno od buforów Qt i głównego zdjęcia). Przesyłane jest
maksymalnie osiem nowych tekstur na klatkę. Nie ma ciągłego odmalowywania po
zapełnieniu cache. Przepełnienie bardzo gęstej siatki korzysta z malowania Qt,
zamiast stale usuwać i ponownie przesyłać widoczne tekstury. Błąd inicjalizacji
OpenGL przełącza całą siatkę na Qt; ręcznie: `PRISM_THUMB_GPU=0 ky-prism`.
Ustawienia PRIME są tylko dla procesu; wspólny skrypt wyboru RTX jest nietknięty.

Nie jest wymagany CUDA/nvJPEG. Dekodowanie JPEG i ICC pozostaje na CPU,
wyświetlanie/skala/mipmapy korzystają z GPU. Nową jawną zależnością jest zlib,
dostępny już na badanym systemie; instalator uwzględnia też pakiet deweloperski
Debiana/Ubuntu. Nie pobierano nowych bibliotek z internetu.

Testy z katalogu głównego projektu:

```sh
./run.sh photos install
./run.sh photos gallery-bench --gpu
./run.sh photos gallery-bench /katalog/z/co/najmniej/80/jpg --gpu
./run.sh photos gallery-bench --stress
```

Test GPU otwiera na krótko okno z docelowym rozmiarem 3840×2160 i co najmniej
80 miniaturami, sprawdza tekstury, piksele, ponowne odmalowania i resize.
Wynik podaje faktyczny rozmiar framebufferu oraz renderer; menedżer okien może
ograniczyć żądany rozmiar. Z launcherem testu PRIME jest ustawiany lokalnie dla
procesu, tak jak w przeglądarce. Bez katalogu powstaje 800 małych syntetycznych
JPEG-ów; to test kolejki/cache/GL, nie pomiar 800 niezależnych plików po 20 MB.
Benchmark ma własny pusty cache dyskowy, więc rozdziela pierwsze utworzenie
miniaturek i kolejne wczytanie z dysku. Nie opróżnia cache plików systemu.

Zmierzone wyniki i ograniczenia: `reports/0.11-validation.txt`. Samo wyliczenie
800 plików i ponowny odczyt 80 podglądów są szybkie, jednak pierwsze utworzenie
szczegółowych miniaturek ogromnych JPEG-ów wymaga odczytu i dekodowania danych.
Nie deklarujemy natychmiastowego pierwszego odczytu 16 GB fotografii.


## 0.12.0 — nvJPEG GPU Hybrid, CUDA Lanczos3, AVX2

Pełne JPEG-i mogą teraz przejść przez `nvjpegJpegStreamParse`, dekoder
`NVJPEG_BACKEND_GPU_HYBRID`, `nvjpegDecodeJpegHost`, transfer i
`nvjpegDecodeJpegDevice`. To osobny dekoder GPU, obok renderowania OpenGL.
Po dekodowaniu własne dwa przebiegi CUDA zmniejszają RGB filtrem Lanczos3,
z preliczonymi wagami i pośrednim buforem float. Do RAM wraca tylko gotowa
miniatura do 512 px. ICC jest przeliczany do sRGB, a EXIF stosowany dokładnie
raz. Główne zdjęcie, RAW/TIFF i skrypt wyboru RTX pozostają bez zmian.

Na RTX 2060 używany jest backend GPU Hybrid. Parametr nvJPEG skalujący już
podczas dekodowania jest przeznaczony dla backendu HARDWARE; dlatego tutaj
pełny obraz RGB pozostaje w VRAM i jest zmniejszany własnym kernelem CUDA.
Nie zakładamy, że nvJPEG zawsze wygrywa ze zmniejszaniem JPEG na CPU — wynik
trzeba zmierzyć na rzeczywistym GPU i konkretnych plikach.
Dokumentacja: https://docs.nvidia.com/cuda/nvjpeg/index.html

Zasoby mają ograniczenia: dwa równoległe konteksty dekodera ze strumieniami o
niskim priorytecie, współdzielony budżet alokacji device do 3 GiB i najwyżej
2/3 wolnego VRAM przy inicjalizacji; pinned host do 512 MiB. Bufory są
wielokrotnie używane i zwalniane po zakończeniu pracy GPU. Budżet nie obejmuje
pamięci sterownika/OpenGL ani pozostałych cache aplikacji. Oryginalny JPEG
jest odczytywany w wątku roboczym, do 512 MiB, z kontrolą anulowania;
nie używamy mmap narażającego proces na SIGBUS przy skróceniu pliku.
Granica dekodera GPU to 250 MP i 65535 px na bok. CMYK, ICC szarości oraz
nieobsługiwane kodowania korzystają z dotychczasowej ścieżki CPU w trybie auto.

CPU wykorzystuje własną funkcję AVX2 konwersji RGB do RGBA (8 pikseli na
przebieg), z wykrywaniem dostępności podczas działania i wersją skalarną.
Działa w ścieżce CPU oraz po odczycie miniatury z GPU. Nie wymusza instrukcji
AVX2 na całej binarce ani na komputerze, który ich nie obsługuje. Samo
rozpakowanie JPEG na CPU nadal realizuje libvips/libjpeg-turbo. Nowy klucz
cache `512-v4` oddziela wyniki od wcześniejszej implementacji; format zapisu
cache pozostaje v3, wspólny limit dyskowy i porządkowanie nadal działają.

CUDA Toolkit >=12, nvcc i nvJPEG są opcjonalnymi zależnościami dla kompilacji
modułu. Na Omarchy/Arch zapewnia je pakiet `cuda`; w badanym środowisku już
był zainstalowany, niczego nie pobierano. Moduł `libprism_nvjpeg.so` jest
ładowany dynamicznie, więc aplikacja CPU działa też bez bibliotek NVIDIA.
Instalacja i archiwa obejmują własny moduł oraz metadane jego kompilacji,
źródła `.cu`, ABI, testy i niniejsze instrukcje. Biblioteki/headers NVIDIA
nie są redystrybuowane; ich powiadomienia są w `src/NVIDIA-NVJPEG-NOTICE.txt`.
Na innym systemie należy zbudować program z lokalnymi zależnościami.

```sh
# Wymaga skompilowania modułu I udanego testu na prawdziwym GPU przed instalacją:
PRISM_WITH_NVJPEG=ON ./run.sh photos install

# Kolory, EXIF, porównanie z CPU, równoległa praca i anulowanie na GPU:
./run.sh photos nvjpeg-test

# Cały podany katalog JPEG, bez CPU rozgrzewającego wcześniej każdy oryginał:
./run.sh photos nvjpeg-bench ~/Pictures/Sample-jpg

# Opcjonalne porównanie pikseli i czasów z CPU (CPU czyta każdy plik pierwszy):
./run.sh photos nvjpeg-bench ~/Pictures/Sample-jpg --reference
./run.sh photos cpu-bench ~/Pictures/Sample-jpg

# Sprawdzenie pamięci kerneli CUDA na rzeczywistym GPU:
./run.sh photos nvjpeg-sanitize
```

Benchmark nvJPEG jest rygorystyczny: brak urządzenia/sterownika lub brak
rzeczywistego dekodowania GPU daje kod błędu. Pokazuje `gpu_decodes`, błędy
poszczególnych plików i `diagnostic`; nie zalicza awaryjnego CPU jako pracy
GPU. Tryb porównania ma jawnie oznaczone `cpu_reference_before_gpu=true`.
Pusty cache miniaturek nie oznacza zimnego cache plików systemu operacyjnego.
Nie deklarujemy natychmiastowego pierwszego odczytu dowolnej liczby GB.

Zmienne tylko dla danego procesu:
`PRISM_JPEG_BACKEND=auto|cpu|nvjpeg` (ostatni jest rygorystyczny),
`PRISM_CUDA_DEVICE=0` (indeks urządzenia CUDA, Intel nie jest urządzeniem CUDA).
Budowanie CPU: `PRISM_WITH_NVJPEG=OFF ./run.sh photos install`.
Tooltip renderera pokazuje liczbę dekodowań nvJPEG, fallback CPU i SIMD;
odczyt diagnostyki nie czeka na inicjalizację CUDA w wątku interfejsu.

Status bieżącej weryfikacji: `reports/0.12-validation.txt`. W środowisku agenta
brak urządzenia CUDA, więc wykonania kerneli na RTX i przyspieszenia nie
potwierdzono. Testy z kontrolowanym modułem zastępczym dotyczą ABI/ICC/EXIF,
nie stanowią testu NVIDIA. Dopiero po testach na pulpicie wykonać
`./run.sh finish --validated`.

## 0.12.1 — menu i otwarcie terminala

Menu kontekstowe korzystają z własnych powierzchni alpha i wspólnej palety.
Fotografie, silnik nvJPEG i cache pozostają bez zmian. Widget Alacritty
ma jednorazową animację Matrix (1450 ms, bez rysowanej konsoli), bez przechwytywania wejścia.
Grupa startowa: menu, Pliki, ky-prism, Alacritty, Chrome.
Globalne style GTK/Qt: `../run.sh desktop-effects install`.

Terminal 0.14.1: aktywne komponenty `WarpTerminalWidget.qml` i `WarpFlight.qml`
mają nowe adresy, aby powłoka nie używała poprzednich komponentów QML z pamięci.
Instalator jawnie odświeża rejestr pluginów po zapisaniu kompletu plików.
Scena zawiera wyłącznie radialny kod warp; bez dekoracyjnego terminala i napisu CONNECTED.

## Terminal 0.15.5 — zielona podróż warp

Animacja trwa 1450 ms. Stała droga głębokości 2,4 jednostki daje około 64%
niższą prędkość niż wariant 0.15.4 (4,2 jednostki przez 925 ms). Nie ma
przyspieszania ani etapu slow motion. Perspektywa nadal naturalnie zwiększa
pozorną prędkość bliższych znaków. Stała pula 324 torów jest rozłożona na
całej głębokości od początku: widz przemieszcza się przez gotowe pole kodu,
zamiast oglądać gwałtowny wybuch liter ze środka.

Drobniejsze znaki 12–14 px, cieńsze smugi, ciemniejsze dalsze warstwy i dwa
subtelne echa budują głębię. Kierunkowa poświata jest słabsza, a fade dotyczy
wyłącznie początku i końca widoczności. Katakana pozostaje odbita jak w Matrix,
cyfry i symbole terminala czytelne. Fonty wybierane są z lokalnych rodzin,
bez pobierania i zmieniania liter co klatkę.

Zgodnie z nową decyzją efekt jest zawsze zielony. Ciemne i jasne tło motywu
wybierają odpowiednio jaśniejsze lub ciemniejsze odcienie zieleni; korekta
kontrastu miesza wyłącznie zielone kolory. Loader usuwa scenę po zakończeniu,
a prawdziwy Alacritty uruchamia się raz. Nakładka przepuszcza wejście i nie
pracuje w tle po zakończeniu; watchdog 2100 ms służy tylko odzyskaniu po błędzie.

ky-prism 0.12.2: okno Qt6 do otwierania zdjęć ma pełną paletę aplikacji (również nieaktywne
okno i zaznaczenie), własną powierzchnię alpha ~89% i ostre napisy. Tło jest
rysowane jawnie, aby `WA_TranslucentBackground` nie pozostawiało pustego
dialogu mimo CSS. Ikony nawigacji przyjmują kolor tekstu zamiast pozostawać
czarne w ciemnym motywie. Aktualizacja następuje przy otwarciu/zmianie palety,
bez timera. Blur zapewnia istniejąca reguła kompozytora dla ky-prism; fotografie
zachowują własny, nieprzezroczysty renderer. Test sprawdza rzeczywistą alpha
tła i kontrast tekstu/zaznaczenia, nie tylko deklaracje CSS.

Instalator utrzymuje Media Glass za pulpitami. Poprawka samego pomiaru jest
osobno w `./run.sh media install`; opis w
[media-stability-update](../media-stability-update/README.md).

Pełne odtworzenie aplikacji z CUDA i pakietami systemowymi opisuje
[OFFLINE-RECOVERY.md](../migration/OFFLINE-RECOVERY.md). Główne backup/export
obejmują wszystkie zainstalowane paczki, nie tylko wykryte zależności pluginów.

Cały rysunek jest przycięty do środkowej połowy szerokości i połowy wysokości
ekranu (25% jego powierzchni). Limit obejmuje także shader i rozmycie.
Shader kierunkowego blur pracuje na teksturze maksymalnie 768×432, a ostre
glify renderowane są osobno. Backend programowy pomija shader. Kolory
pozostają zielone. Odcień dopasowuje się do jasności tła motywu, z celem
kontrastu 3:1 względem bazowego tła. Nie jest to gwarancja kontrastu na dowolnej
tapecie. Wybór odcienia przelicza się przy zmianie tła, bez odczytów plików
co klatkę.

Gotowe `WarpGlow.frag.qsb` jest częścią instalacji i eksportu; instalacja nie
wymaga Shader Bakera ani pobierania biblioteki efektów. Po zmianie GLSL
opiekun źródeł odtwarza plik za pomocą:

```bash
/usr/lib/qt6/bin/qsb --qt6 --glsl '100 es,120,150,330' --qsbversion 64 \
  -o terminal-plugin/WarpGlow.frag.qsb terminal-plugin/WarpGlow.frag
```

Następnie aktualizuje sumy w `WarpGlow.meta.json` i głównym manifeście.
Test OpenGL renderuje kod wyciągnięty z gotowego `.qsb`; sprawdza blur,
przezroczystość i ograniczenie alpha. Test klatek sprawdza rzeczywiste
piksele w 15 kombinacjach czasu i rozdzielczości, w tym 8K, ultrawide i pion,
a następnie wszystkie lokalne palety stockowe z nakładkami użytkownika.
To testy poza sesją pulpitu; nie potwierdzają pomiarów na fizycznym RTX.

Wgranie samej aktualizacji animacji wraz z bieżącym programem:

```bash
./run.sh photos install  # z głównego katalogu remote-desktop
omarchy restart shell
```

Restart powłoki zapewnia odrzucenie starej sceny QML z pamięci. Po kontroli
wyglądu na pulpicie wykonaj `./run.sh finish --validated`.

## Osobna paczka do GitHub — Ubuntu i RHEL

```bash
./run.sh photos publish # z głównego katalogu remote-desktop
```

Generator przygotowuje `photo-viewer/exports/github/ky-prism` i archiwum źródeł
z sumą SHA-256. Na GitHub trafia wyłącznie ten osobny katalog aplikacji.
Źródła publikacji obejmują instalator Ubuntu 24.04/26.04 i RHEL 9/10,
instrukcje dodatkowych repozytoriów RHEL, testy, workflow budowania DEB/RPM
oraz analizę licencji. nvJPEG domyślnie używa AUTO: kompiluje się przy obecnym
lokalnym SDK i próbuje dekodować JPEG-y przed fallbackiem CPU. Brak SDK nie
blokuje instalacji ani renderowania OpenGL. Sterowniki i biblioteki NVIDIA
nie są dołączane do archiwum źródeł.

Paczka zawiera konkretną propozycję MIT do przeglądu przed publikacją.
Autor zachowuje prawa autorskie. Zależności zachowują odrębne licencje.
Szczegóły i granice wykonanych testów: [publication/README.md](publication/README.md).

## Okno „Załaduj zdjęcie”

Okno wyboru zdjęcia ma miniatury oraz podgląd zaznaczonego pliku. Dotyczy to
wyłącznie dialogu otwieranego przez „Załaduj zdjęcie” / „Otwórz” / Ctrl+O.
Główna galeria, renderer, jakość zdjęć i animacje zachowują dotychczasowy kod.
Miniatury korzystają z obecnego dekodera libvips/LibRaw i opcjonalnego nvJPEG;
nie uruchamiają dekoderów obrazu z wtyczek Qt. Mają własny cache obrazów
48 MiB, kolejkę do 96 żądań i najwyżej dwa zadania dekodujące. Odczyty są
asynchroniczne, a zamknięcie okna anuluje oczekujące zadania. Nie ma timera
odpytywania ani pracy w bezczynności. Uszkodzony obraz pokazuje brak podglądu.
Zaznaczenie nie zmienia kolorów miniatury. W trybie szczegółów nadal działa
standardowa lista Qt i podgląd zaznaczonego pliku.

Ikona okna i systemowa ikona aplikacji odwzorowują siedem łopatek z „About”.
Wersja SVG ma przezroczyste tło; ikona Qt jest rysowana wektorowo z palety
aplikacji. Napis przycisku pozostaje dokładnie „Załaduj zdjęcie”.

## Język PL / EN

Przycisk obok „About” pokazuje bieżący język i flagę Polski albo Wielkiej
Brytanii. Kliknięcie lub Ctrl+Alt+L obraca przycisk i przełącza cały interfejs
bez zamykania zdjęcia. Po 340 ms animacja kończy pracę; nie działa w tle.
Domyślny język to polski. Wybrany język jest zapamiętywany w
`~/.local/share/omarchy-prism/language.json` (respektuje XDG_DATA_HOME).
Backup/eksport programu zachowuje ten wybór, a instalacja nie nadpisuje
istniejącej preferencji. Nazwy plików, ścieżki i dane zdjęć nie są tłumaczone.

Zaawansowane uruchomienie: `PRISM_LANGUAGE=en ky-prism` lub
`PRISM_LANGUAGE=pl ky-prism` ustawia język startowy dla danego procesu.
Własny katalog tłumaczeń: `i18n/en.json`; po edycji uruchom
`python3 i18n/generate.py`. Gotowy program nie wymaga Qt Linguist ani sieci.
Polskie teksty wbudowanych dialogów Qt korzystają z tłumaczeń Qt z systemu;
podstawowe etykiety mają też własny fallback. To nie zmienia języka systemu.

### Diagnostyka procesu, który pozostał po zamknięciu

`./run.sh diagnose-hang PID --seconds 3` zapisuje na standardowe wyjście JSON
z użyciem CPU całego procesu i każdego wątku, kanałem oczekiwania, RAM oraz
liczbą deskryptorów. Z katalogu nadrzędnego użyj `./run.sh photos diagnose-hang PID`.
Podaj PID widoczny w `top`. Narzędzie tylko odczytuje `/proc`: nie zatrzymuje
ani nie zabija procesu, nie otwiera zdjęć i nie wysyła raportu do sieci.
400% oznacza cztery w pełni zajęte rdzenie, a nie wyciek pamięci sam w sobie.

Zamykanie najpierw anuluje wszystkie kolejki, następnie dołącza wątki i zwalnia
zasoby dekoderów oraz nvJPEG przed zakończeniem bibliotek. Nie używa wymuszonego
przerywania wątków. Test `shutdown` sprawdza zakończenie osobnych procesów,
łącznie z zamknięciem podczas odczytu, porównania, pracy miniaturek,
otwartego dialogu zdjęć i przełączania języka.

# Przeniesienie samego ky-prism na Ubuntu 26.04.1

Regulacja krycia interfejsu: `PRISM_UI_OPACITY=95 ky-prism` (0–100; 100 daje
pełne krycie). Na stałe: `~/.config/ky-prism/appearance.json` z zawartością
`{"ui_opacity_percent":95}`. Uruchom program ponownie po zmianie. Parametr
dotyczy teł paneli, nagłówków i okien dialogowych; fotografie zachowują pełne
krycie i kolory. Brak ustawienia zachowuje dotychczasowy materiał.

Z katalogu `remote-desktop`:

```bash
./run.sh photos export /ścieżka/do/paczki --target ubuntu
./run.sh photos backup /ścieżka/do/kopii --target ubuntu
```

Powstaje prywatna, zweryfikowana paczka źródeł z instalatorem Ubuntu,
testami i preferencjami. Po rozpakowaniu na Ubuntu:

```bash
bash run.sh install --install-cuda --nvjpeg ON --test
bash run.sh restore-data
bash run.sh doctor --gpu
```

Wymagany działający sterownik NVIDIA. CUDA jest opcjonalna; `--nvjpeg ON`
wymaga jej działania, `AUTO` pozwala na fallback CPU. Dołączono natywny
kod sm_89 dla RTX 4070, zachowując sm_75 i PTX. Instrukcje CPU dobierane są
bezpiecznie podczas działania (AVX2/scalar), bez wymuszania AVX-512.
Szczegóły: [przenoszenie i zależności](publication/templates/docs/TRANSFER-PL.md).

**To nie jest pełny eksport offline Ubuntu**: jego zależności `.deb` są
instalowane z repozytoriów Ubuntu. Główne `./run.sh backup/export` nadal
służą do odtworzenia Omarchy z pakietami Arch. Nie instaluj ich na Ubuntu.

