# ky-prism — zasady pracy dla Claude Code

Szybka przeglądarka zdjęć: interfejs GTK 4, silnik C++20 na niewizualnych
modułach Qt 6 i libvips/LibRaw. Działa na Ubuntu 24.04/26.04, Arch/Omarchy,
FreeBSD i OpenBSD. Odpowiadaj po polsku.

## Sposób pracy

- **Gałąź.** Pracujemy na gałęzi `main`. Zdalne repo `origin` to repo
  robocze (prywatne). Publiczne wydanie to osobne repo, opisane w git jako
  `public` (tylko na komputerze właściciela).
- **Po skończonej zmianie, w tej kolejności:**
  1. Uruchom testy.
  2. Odśwież manifest: `python3 -B package.py manifest`.
  3. Zrób commit na `main` i `git push origin main`.
  4. Podaj link do pobrania tego commita, w postaci
     `https://github.com/<repo>/archive/<pełny-sha>.tar.gz` (oraz `.zip`),
     i jak go zainstalować:
     - pierwszy raz: sklonuj repo, potem `./run.sh install`,
     - aktualizacja: `git pull && ./run.sh install`.
- **Publiczne repo.** Wysyłaj do niego tylko na wyraźne polecenie
  „publikuj”. Najpierw sprawdź nowe commity pod kątem prywatnych danych:
  ścieżek `/home/…`, adresów e-mail, zrzutów folderów użytkownika, logów
  z `reports/`. Potem wyślij wyłącznie `main` (`git push public main:main`).
  W sesji w chmurze repo publicznego zwykle nie ma — powiedz o tym zamiast
  próbować.
- **Commity.** Autor ma adres noreply GitHuba (ustawiony w repo), nigdy
  prywatny e-mail.
- **Testy z oknem.** `./run.sh test` i `./run.sh gtk-test` otwierają
  prawdziwe okno na pulpicie i podmieniają schowek. Na komputerze
  użytkownika uprzedź o tym przed uruchomieniem.

## Testy i wyniki — zasady

- **Tylko testy związane ze zmianą.** C++: `ctest --test-dir build -R <nazwa>`.
  Python: `python3 -B -m unittest tests.test_<moduł>`.
- **Pełny zestaw raz, przed wydaniem.** `./run.sh test` uruchamiaj raz przed
  „publikuj”, wcześniej uprzedzając o oknie testowym i schowku.
- **Wynik przefiltrowany** do podsumowania i błędów
  (`grep -E "tests passed|Failed|FAIL|Error|^OK"`), bez pełnych logów
  kompilacji.
- **Tekst zamiast zrzutów ekranu,** gdy wystarczy: asercje w testach, tekst
  paska stanu. Zrzut (`gtk-test --screenshots`) oglądaj tylko wtedy, gdy
  zmiana dotyczy samego wyglądu.

## Budowanie i testy

```sh
./run.sh build            # zależności (sprawdzenie), kompilacja
./run.sh test             # kompilacja, CTest (z oknem) i testy Python
./run.sh install          # zależności, kompilacja, testy bez okna, instalacja do ~/.local
python3 -B package.py manifest   # po każdej zmianie plików źródłowych
python3 -B i18n/generate.py      # po zmianie i18n/en.json
```

- **Bez ekranu** (sesja w chmurze) uruchom `ctest --test-dir build -LE display`
  i `python3 -B -m unittest discover -s tests -p 'test_*.py'`.
- **Test okna `gtk_ui`** wymaga ekranu. W chmurze można spróbować
  `xvfb-run -a ./run.sh test`; tego nie sprawdzano.
- **Zależności w chmurze** instaluje skrypt środowiska z
  `docs/claude-code-cloud-setup.sh` (opis: `docs/CLAUDE-CODE.md`).

## Kod

- W plikach GTK nagłówek `gtkbase.h` jest pierwszy.
- Cały projekt kompiluje się z `QT_NO_KEYWORDS`: pisz `Q_SIGNALS`, `Q_SLOTS`
  i `Q_EMIT`, nie `signals` i `emit`.
- **Teksty interfejsu.** Każdy polski tekst w `src/` musi mieć tłumaczenie
  w `i18n/en.json`, a potem trzeba uruchomić `python3 -B i18n/generate.py`.
  Pilnuje tego test `test_language_catalog`. Odmieniaj liczebniki
  (1 zdjęcie, 2 zdjęcia, 5 zdjęć).
- **Przenośność.** Skrypty (`run.sh`, programy startowe) są w POSIX sh.
  Kod musi kompilować się także na FreeBSD i OpenBSD (clang, brak
  `renameat2`, libvips bez lcms2 i libexif na OpenBSD).
- **Pliki użytkownika.** Program nigdy nie nadpisuje ani nie zmienia jego
  plików. Kopie i eksport odmawiają nadpisania. Jedyny wyjątek to
  przeciągnięcie zdjęć na folder: nadpisuje wyłącznie po wybraniu
  „Nadpisz” w oknie pytania, atomowo (`FileOps::copyItems`) i tylko plik,
  który się nie zmienił od pytania.
- **Struktura:** silnik w `src/*.cpp`, okno GTK w `src/gtk/`, kopie,
  przenoszenie i paczki w `prismctl.py`, instalacja w `package.py`,
  zależności systemu w `dependencies.py`.
