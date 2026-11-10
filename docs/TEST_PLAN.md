# Testy regresji Prism 0.2

Każde wydanie: `./run.sh photos test`. Brak błędu w testach nie oznacza dowodu
braku wszystkich wycieków, poprawności każdego RAW ani płynności na prawdziwym ekranie.

## Automatyczne, wykonywane teraz

- Dekoder: 8/16-bit TIFF, JPEG, ICC, orientacja, szwy i brzegi kafelków,
  nieistniejący plik, fałszywy JPEG, anulowanie, wielokrotne otwarcia.
- Kontroler: 100 żądań otwarcia różnych plików, w tym uszkodzonego, zanim
  zakończy się worker. Tylko ostatni wynik może zmienić metadane/obraz.
- 100 naprzemiennych żądań zoomu i końcowe przybliżenie: skończone dodatnie
  współczynniki, wiele klatek animacji, zakończenie timera i brak pracy idle.
- Zamknięcie/stop podczas odczytu: czekanie na worker bez użycia zwolnionych danych.
- Błędny plik, potem poprawny: aplikacja wraca do poprawnego działania.
- Nazwy z odstępami, nawiasami kwadratowymi, polskimi znakami.
- Shader EGL: RGBA16, kolejność kanałów i kierunek obrazu. Może działać na Mesa.
- Instalacja i rollback, puste/wadliwe configerrors, zachowanie domyślnych
  skojarzeń MIME, dodanie Prism do alternatyw, ścieżka z odstępami, eksport.
- Wskazany zbiór rzeczywistych JPEG/TIFF: odczyt podglądu i kafelków kilku
  poziomów; wyniki w reports/sample-benchmark.json. Bez zapisu do oryginałów.

## Przed oznaczeniem wydania jako sprawdzonego live

1. Na RTX otworzyć katalog Sample-jpg. Zmieniać zdjęcia 5–10 razy/s przez
   30 s, także podczas pierwszego odczytu największego pliku.
2. Kręcić kółkiem w obie strony, przesuwać zdjęcie i zmieniać rozmiar okna.
   Nie może być czarnych dziur między kafelkami ani zmiany na poprzednie zdjęcie.
   Podczas doczytywania wolno widzieć podgląd o niższej rozdzielczości.
3. F11/Esc 20 razy, zmiana monitora/DPI, otwarcie/zamknięcie paneli. Sprawdzić
   1:1 oraz geometrię kursora na skali 1 i ułamkowej skali XWayland.
4. Otworzyć plik przez Nautilus „Otwórz za pomocą”. Wybór nie może zmienić
   dotychczasowej domyślnej aplikacji. Wywołać również przycisk Otwórz w Prism:
   ma to być dialog Qt, bez natywnego podglądu GTK/glycin.
5. Zamknąć w trakcie odczytu JPEG/TIFF/RAW i od razu uruchomić ponownie.
6. Przez 10 min przechodzić między zdjęciami i poziomami. Monitorować RSS/VRAM:
   cache może rosnąć do limitu, ale nie bez końca. Po 60 s bezczynności nie
   powinno być stałej aktywności animacji. Po wyjściu proces ma się zakończyć.
7. Porównać kolor i orientację z zarządzanym kolorystycznie wzorcem; osobno
   sRGB, Adobe RGB/ICC, grayscale, CMYK z ICC, RAW konkretnego aparatu.

## Korpus negatywny do rozszerzania

Pliki 0 bajtów, JPEG ucięty w nagłówku i w środku danych, błędne długości EXIF,
ICC z niepoprawną sygnaturą, osiem orientacji, skrajnie szeroki/wysoki obraz,
BigTIFF i TIFF paskowy/kafelkowy/wielostronicowy, nazwa zaczynająca się od '-',
plik usunięty/podmieniony podczas odczytu, brak uprawnień, niedostatek miejsca
na cache, błąd alokacji tekstury i utrata kontekstu GL. Brak pliku testowego lub
sprzętu oznacza „nie sprawdzono”, nie „zaliczono”. Nie fuzzować aktywnej sesji
ani nie nadpisywać oryginałów — korpus tworzyć w katalogach tymczasowych.

Awaria z 2026-10-21: potwierdzony stos libqgtk3 → gdk_pixbuf_new_from_file_at_scale
→ gly_image_get_transformation_orientation → gufo-exif (SIGABRT). Regresja ma
pilnować użycia AA_DontUseNativeDialogs + Qt DontUseNativeDialog i ikon bez
miniatur GTK; pełne potwierdzenie wymaga powtórzenia akcji z awarii live.

## 0.3 — układ, integracja i bezczynność

Automatycznie: układ powitalny, ukryj/pokaż panel i stan przycisku, szerokość
paska <1200 px, bezpieczne zamknięcie; geometria przysłony 101 stanów × 8 listków
w 24/256/800/4320 px; idempotentna instalacja ikony po delfinie, zachowanie
reszty paska oraz domyślnego edytora tekstu, domyślne MIME zdjęć.

Live: pojedyncze kliknięcie zdjęcia, Enter na pliku i katalogu, szybkie 100
przełączeń, 1:1 podczas zoomowania (100%), przywrócenie widoku, rozmiar okna
z małym i bardzo dużym plikiem, F11/Esc, jasny/ciemny theme, monitory 100/150/200%.
Sprawdzić wallpaper blur wyłącznie pod panelem i paskiem, pełną nieprzezroczystość
fotografii. Dziesięć kliknięć ikony podczas animacji ma otworzyć tylko jedno okno.
Po animacji i po zakończeniu dekodowania porównać CPU przez 30 s; bez animacji idle.
Dopiero po potwierdzeniu działania w instalacji: ./run.sh finish --validated.


## 0.4 — regresja oryginalnej skali i szybkiego podglądu

Automatyczne testy obejmują poprawną miniaturę EXIF IFD1 i uszkodzony JPEG
miniatury wewnątrz poprawnego pliku. W obu przypadkach końcowy obraz musi zostać
odczytany; niepoprawna miniatura nie może wywołać awarii. Test piramidy JPEG
4099×2051 (progressive): losowa kolejność poziomów 3/1/2/0 i kafelków, przejście
na inny obraz i powrót z cache. Oryginał w UI ma zachować geometrię okna i dawać
skalę 100%; po tym okno nadal musi dać się przeskalować. Geometria ikon: siedem
trójkątów/listków w obu wariantach, bez wartości NaN/undefined w 24–4320 px.

Live: kliknąć Oryginał podczas odczytu dużego pliku, przesunąć zdjęcie i upewnić
się, że zakończenie odczytu pełnego podglądu nie resetuje wybranej skali. Zmiana
zdjęcia w tej chwili nie może pokazać miniatury poprzedniego. Sprawdzić krawędzie
okna, również przy panelu i obszarze zdjęcia, oraz fullscreen. Animacja startu
ma trwać ok. 720 ms, nie opóźniać startu programu i znikać wraz z Loaderem.


## 0.5 — zapis i odnajdywanie ulubionych

Automatycznie: wzorcowy MD5, zapis/odczyt, brak duplikatu ścieżki, dwóch
równoległych zapisujących, aktywna blokada, anulowanie, modyfikacja w trakcie
haszowania, szukanie zdjęcia o zmienionej nazwie, odmowa innego MD5, zachowanie
UUID po przeniesieniu, ochrona uszkodzonego JSON. UI: serce po zapisie, usunięcie,
lista ulubionych, zakończenie animacji, maksymalizacja i przywracanie po F11.
Pakowanie: odtworzenie ulubionych na nowym koncie i zachowanie nowszej listy.

`build/prism-memory-test` mierzy osiem obiegów po 20 obrazów, z wymianą cache.
Po rozgrzaniu dopuszcza maks. 32 MiB rozrzutu RSS, drukuje też clear. To nie
jest pomiar VRAM; ASan/UBSan uzupełniają kontrolę właścicieli pamięci.

Live: policzyć MD5 pliku 2 GB, przerwać, zamknąć aplikację podczas szukania,
sprawdzić brak pozostawionego procesu. Otworzyć dwa okna i dodać różne zdjęcia;
oba wpisy mają zostać zachowane. Zmienić nazwę/przenieść plik, wskazać katalog,
sprawdzić aktualizację wyłącznie po zgodnym rozmiarze i MD5. Przy kilku kopiach
wybrać jedną. Obserwować RSS i VRAM po 100 przejściach na rzeczywistym RTX.


## 0.6 — wymagany test w sesji po instalacji

- □: maksymalizacja zachowuje pasek; ponownie □ przywraca rozmiar. Powtórz
  bez zdjęcia, ze zdjęciem, po F11/Esc, na obu monitorach i po ich odłączeniu.
- Pasek: menu → delfin → Prism → Alacritty. Otwórz terminal i zamknij go;
  pozostałe widgety oraz ich konfiguracja pozostają zgodne.
- Katalogi A → B → C, Wstecz/Dalej; cofnięcie do B i wejście D usuwa C z gałęzi.
  Szybkie klikanie, brakujący katalog, ukryj/pokaż panel. Zdjęcia nadal jednym kliknięciem.
- Zakładka: zapisz katalog, restart, otwórz, usuń zakładkę (katalog ma pozostać).
- Kopia: źródło z EXIF/ICC, wybierz miejsce, Anuluj przed i w trakcie operacji,
  poprawny zapis i porównanie sum. Cel istniejący / utworzony w trakcie,
  pełny lub tylko do odczytu dysk; źródło zawsze pozostaje. FAT/exFAT: sprawdź bezpieczny zapis lub czytelną odmowę przy braku wsparcia systemu plików.
- Duplikaty na jednorazowym katalogu: identyczna nazwa+rozmiar+MD5 = grupa;
  inna nazwa albo zawartość = brak wspólnej grupy. Anuluj skan. Wybierz ścieżkę,
  anuluj osobne potwierdzenie; następnie potwierdź przeniesienie jednej kopii
  do kosza. Sprawdź odtworzenie przez Pliki. Usuń/zamień drugą kopię po skanie:
  program ma odmówić przeniesienia ostatniej/zmienionej kopii.
- Jasna i ciemna skórka: dialogi, menu, zaznaczenia, czytelność ścieżek.
  Animacje tylko w czasie otwarcia/operacji, brak pracy w tle po zamknięciu.
- Po potwierdzeniu działania: ./run.sh finish --validated z remote-desktop.


## 0.7 — sprawdzenie w sesji użytkownika

- Po zamknięciu poprzedniej wersji: ./run.sh photos install; ky-prism.
  Sprawdź nazwę w pasku, menu, „Otwórz za pomocą”, --version i polecenie zgodności.
  Istniejące ulubione, gwiazdki i ustawienia widgetu muszą pozostać dostępne.
- Gwiazdka bezpośrednio za sercem; brak przycisku zakładek w lewym panelu.
  Zapisz katalog, otwórz ☆≡, filtruj, otwórz Enterem, usuń Delete — katalog zostaje.
- About: autor i © 2026, animacja kończy się i nie pracuje na ukrytej stronie.
  Otwórz / zamknij wiele razy, przełącz paletę jasną i ciemną. F1: skróty,
  Ctrl+F1: autor, Alt+1/2: strony. Zamknij główne okno z otwartym About.
- Wykonaj scenariusze KEYBOARD.md bez myszki. Zweryfikuj klawisze także na
  fizycznej klawiaturze i pod swoim układem klawiszy, na obu monitorach.
  Sprawdź widoczny fokus i brak przejmowania F, 1, strzałek w filtrach/ścieżce.
- Native Hyprland / NVIDIA / portal mogą różnić się od Qt offscreen; testy
  automatyczne nie zastępują potwierdzenia działania w sesji.
- Po potwierdzeniu wykonaj ./run.sh finish --validated w remote-desktop.


## 0.8: UI, izolacja testów i bezpieczeństwo

`browser`: sortowanie 4 kluczy, kierunek, Ctrl/Shift multiselect, długie polskie
nazwy z nawiasami/metaznakami, 60 szybkich otwarć/zamknięć kart, limit 24 kart,
przywrócenie zdjęcia i porównania, zwolnienie drugiego widoku, wolne miejsce,
JSON urządzeń: pominięcie NVMe, USB zamontowane/niezamontowane, uszkodzony JSON.
`keyboard`: odizolowane XDG_CONFIG_HOME/cache i numer linii oczekiwania w razie
błędu; rzeczywiste zdarzenia klawiszy, akcje kopiowania i kosza, brak kolizji z
pisaniem w ścieżce. Testy UI nigdy nie wymagają GL na offscreen.
`file_ops`: wieloplikowe kopie zachowują bytes/nazwy, wcześniejsza kolizja nie
publikuje części paczki, NUL odrzucony, istniejące przypadki race/cancel/zmiany
źródła i potwierdzenia kosza nadal obowiązkowe.
`gpu-test` (ręcznie w sesji): QOpenGLWidget, rzeczywisty GL_RENDERER,
12 ponownych odczytów/resize, RGBA16, kolor 210/100/30 z tolerancją 1,
1:1 i brak ciągłej animacji. Wymaga pulpitu; brak testu RTX nie oznacza PASS.
Sanitizery: ASAN+UBSan dekoder/UI/pliki/karty; LSAN jeśli dostępny. RSS po
rozgrzaniu cache: 160 odczytów, nie większy niż 32 MiB rozrzut ostatnich rund.
Live: jasny/ciemny theme, polskie ścieżki na USB (także spacje), anulowanie
polkit, zniknięcie nośnika podczas kopii, 1:1 na obu monitorach. Blur zależy od
kompozytora; zdjęcie musi pozostawać kryjące i pozbawione efektów.


## 0.9 — ostrość, miniatury i cykl życia

Automatycznie: za mała miniatura EXIF nie trafia do głównego widoku; docelowy
podgląd ma wystarczającą rozdzielczość. Dopasowanie uwzględnia proporcje obszaru
zdjęcia; po powiększeniu okna za mały cache nie może być użyty jako gotowy.
Test współdzielonego cache między dokumentami, przygotowania sąsiadów,
anulowania, braku pracy miniatur w trybie listy i przy wstrzymaniu, kolejki do
24 + 1 pracującej, limitu RAM, uszkodzonej miniatury EXIF i orientacji TIFF.
Adapter TIFF: classic/BigTIFF i oba endian, rzadki plik z prywatnym tagiem
64 MiB, zgodne piksele i ICC, suma oryginału przed/po, niepoprawne offsety.
UI: popup pod przyciskiem, jedna/dwie karty, list/grid, wspólne zaznaczenie,
skalowanie komórek, osobne × lewe/prawe, ponownie otwarta para bez zapadnięcia
panelu, klawiatura i nieruchomy układ podczas zmiany fokusu przycisku.

Pomiary: te same JPEG-i przed/po, naprzemienna kolejność wersji i rozdzielczość
1600×900 fizycznych pikseli. Osobno pełne 33 próbki i miniatury, RSS po 160
odczytach z wymuszonym wypieraniem cache oraz liczba otwartych deskryptorów.
Brak okna GPU w tym pomiarze oznacza benchmark dekodera, nie czas prezentacji.

Live: sprawdź Sample-jpg na obu monitorach i swoich skalach DPI, przełączając
powoli i 5–10 razy/s. Porównaj dopasowanie, 1:1 i duże TIFF/FFF. Wybierz
Miniatury, przewiń daleko, ukryj panel i zminimalizuj okno: zadania mają ustać.
Przełącz jasny/ciemny motyw, sprawdź Name/Size, popup, przyciski i cieńsze logo
Chrome. Zamknij lewe zdjęcie, otwórz je ponownie, zamknij prawe i obydwa;
pozostały widok ma zachować poprawny zoom, EXIF, serce i obsługę kopii.
Po instalacji i potwierdzeniu live: ./run.sh finish --validated.

Regresje końcowe 0.9: sześciokanałowy RAW z root IFD oznaczonym jako reduced
musi pokazać błąd braku pełnego dekodera; podgląd działa wyłącznie jako
miniatura biblioteki. Test adaptera sztucznie tworzy taki kontener. Wyrównanie
1:1: nieparzyste wymiary i przesunięcie ułamkowe nie mogą pozostawić początku
obrazu między fizycznymi pikselami. GPU-test zawiera paski szerokości 1 px,
które po wyświetleniu 1:1 muszą zachować kontrast, bez mieszania sąsiadów.

## 0.10 — materiały i ładowanie

- `material`: alfa szkła, jasna/ciemna paleta, niezmienny rozmiar stanów,
  timer klepsydry: start/koniec/ukrycie/minimalizacja/przywrócenie, 200 anulowań.
- `interaction`: 100 szybkich otwarć; stan ładowania kończy się po wyniku i błędzie.
- `browser`: cztery bezpośrednie sortowania, brak QMenu, bezpośrednie duplikaty.
- `keyboard`: każdy przycisk paska i sortowania w łańcuchu Tab, kopiowanie,
  anulowanie, bezpieczne potwierdzenie przeniesienia do kosza.
- `PRISM_DIALOG_SNAPSHOTS=/tmp/prism-captures` uruchomione z `prism-browser-test`
  zapisuje realne okna kopiowania/duplikatów w dwóch paletach, bibliotekę i
  klepsydrę; sprawdza zmianę palety w już otwartym dialogu.
- GPU/blur: po instalacji sprawdzić na pulpicie, `./run.sh photos gpu-test`.
  Przełączyć Omarchy theme przy otwartym dialogu; sprawdzić alfę na tapecie.

## 0.10.1 — regresja blokowania myszy

`pointer`: 100 kliknięć i zwolnień bez operacji systemowej, odrzucenie punktu
spoza ramki i syntetycznej aktywacji, ochrona przycisków/pól/list, próg ruchu,
jedna operacja natywna na przeciągnięcie, rogi skalowania i ukrycie okna.
Na pulpicie sprawdzić wielokrotne otwieranie z terminala, Plików i paska,
kliknięcia nagłówka bez przesuwania, przeciąganie, resize, dialogi i Alt+Tab.
Offscreen nie może potwierdzić zachowania natywnego przechwycenia XWayland.

## 0.10.2 — miganie miniaturek przy rozszerzaniu biblioteki

Test `thumbnails` odtworzył błąd na 0.10.1: po wczytaniu miniatury 128 px
żądanie innej szerokości zwracało pusty obraz. Po poprawce wszystkie szerokości
64..512 od razu zwracają ten sam gotowy podgląd, bez pracy dekodera.
`browser`: 180 zmian rozmiaru rzeczywistego splittera, gotowe miniatury bez
pustych klatek i nowych zadań, stały koszt cache, stabilizacja rozmiaru komórek,
brak zmiany geometrii okna i pozostawionego grabbera. `pointer`: uchwyt
splittera nie może rozpocząć przesuwania głównego okna. Sprawdzane jest też
odroczone zatwierdzanie rozmiaru paneli (`opaqueResize=false`).
Na pulpicie: przeciągać separator w obie strony w trybie miniaturek, również
przy odczycie zdjęcia. Puszczenie zatwierdza nowy rozmiar bez migania.


## 0.11.0 — 800 plików i 80 widocznych podglądów

- `gallery`: 800 rzeczywistych małych syntetycznych JPEG-ów, 80 gotowych
  obrazów równocześnie, 60 zmian skali bez nowych zadań, odtworzenie 80 z
  cache dyskowego bez dekodera źródeł, dziesięć stron po 80 z ograniczeniem RAM.
- `thumbnails`: poprawny mały JPEG osadzony w EXIF pojawia się jako szybki
  podgląd i dokładnie raz przechodzi w wersję 512 px; zniknięcie widoku lub
  zmiana katalogu nie pozostawia niepotrzebnej kolejki uaktualnień.
- Cache dyskowy: bezstratny round-trip, fałszywy ogromny rozmiar, uszkodzona
  suma, ucięty nagłówek. Brak alokacji na podstawie niezweryfikowanych długości.
- `thumbnail_shader`: produkcyjny shader pod EGL, porównanie RGBA i orientacji
  tekstury. Nie zastępuje testu natywnego viewportu Qt na NVIDIA.
- `gallery-bench --stress`: 4800 ponownych wizyt z rotacją RAM, próbki RSS,
  liczba deskryptorów, brak kolejnego odczytu oryginałów po zapisie cache.
- `gallery-bench --gpu`: natywne okno, co najmniej 80 tekstur, kolor piksela,
  brak ponownego uploadu przy odmalowaniu, 80 zmian szerokości, limit VRAM,
  faktyczny renderer i rozmiar bufora. Wymaga dostępnego pulpitu/OpenGL.
- Testy `browser`, `pointer`, `keyboard`, `thumbnails` nadal muszą przechodzić:
  odroczony splitter, resize bez ponownego dekodowania, brak grabbera,
  dezaktywacja/anulowanie, Unicode i nieprawidłowe pliki.
- Live: szybkie przewijanie podczas dekodowania, zmiana katalogu, schowanie
  biblioteki, minimalizacja i zamknięcie w trakcie pracy; mysz i klawiatura
  mają pozostać dostępne. Zmiana theme nie może zniekształcić zdjęć i napisów.
- Dopiero po instalacji i sprawdzeniu na pulpicie: `./run.sh finish --validated`.


## 0.12 — nvJPEG i AVX2

- `pixel_simd`: 0..2049 pikseli, wejście bez dodatkowego paddingu, niewyrównane
  wyjście, canary, zgodność bajtowa ze skalarną konwersją. ASan kontroluje
  granice odczytów 24 bajtów i zapisów 32 bajtów w ścieżce AVX2.
- `nvjpeg_wrapper`, `nvjpeg_bad_abi`, `nvjpeg_init_latency`: kontrolowany moduł
  zastępczy, ICC AdobeRGB→sRGB, EXIF 6, Unicode, błędne ABI, wymuszony błąd,
  auto fallback, strict mode i anulowanie. Status GUI musi wrócić <100 ms przy
  inicjalizacji trwającej 500 ms. NIE jest to dowód wykonania CUDA.
- `nvjpeg_cpu`: syntetyczne RGB, profil ICC, gradient, obrót i progressive.
- `nvjpeg_gpu`: te same pliki na nvJPEG, porównanie geometrii i pikseli z CPU,
  24 zadania z czterech wątków współdzielących dwie sesje, liczniki GPU oraz
  anulowanie. Bez GPU jest SKIP; przy wymaganym runtime musi być błąd.
- `nvjpeg-bench Sample-jpg`: rygorystyczny odczyt rzeczywistych plików, bez
  cache miniaturek i domyślnie bez wcześniejszego odczytu przez CPU. Nie
  usuwamy cache OS. Rejestrować initialization_ms, nvjpeg_ms i gpu_decodes.
- `nvjpeg-sanitize`: compute-sanitizer memcheck/leak-check, rzeczywisty GPU.
- Instalacja ON: CTest wymaga runtime GPU; wynik 0 dekodowań lub brak urządzenia
  zatrzymuje instalację zanim package.py zmieni live pliki.
- Transfer: własne .so oraz nvjpeg-build.json w install/export/backup, źródła
  CUDA i notices; CPU aplikacji nie może mieć DT_NEEDED nvjpeg/cudart.
- Po native instalacji powtórzyć szybkie kliknięcia, zmianę katalogu i szerokości
  przy aktywnym GPU. Następnie finish --validated; nie potwierdzać live z CI.
