# Przygotowanie publicznego ky-prism

Ta paczka przygotowuje **osobne repozytorium aplikacji**, z jawnej listy źródeł.
Nie publikuj całego `remote-desktop`, kopii konta ani eksportu systemowego.

```bash
python3 -B photo-viewer/publication/prepare.py
```

Domyślny katalog: `photo-viewer/exports/github/ky-prism`. Obok powstają archiwum
źródeł i SHA-256. Istniejący katalog nie jest nadpisywany. Kolejną wersję
przygotuj przez `--output /nowy/katalog/ky-prism` i porównaj zmiany przed pushem.
Wewnątrz są instrukcje instalacji, repozytoriów RHEL, opcjonalnego nvJPEG,
audyt licencji, testy i workflow budowania DEB/RPM na GitHub.

Licencja do przeglądu przed publikacją: **MIT**, z zachowaniem praw autorskich
Krzysztofa Kylena Lenartowicza. Pytanie o wybór MIT/GPL zostało przedstawione
użytkownikowi; dopóki nie zostanie rozstrzygnięte, traktuj ten katalog jako
konkretny projekt publikacji, a nie zgodę na publiczny push. Wariant GPL
wymaga ponownej analizy opcjonalnego połączenia z własnościowymi bibliotekami
NVIDIA; samo `dlopen` nie rozstrzyga zgodności GPL.

Publiczne źródła zachowują silnik aplikacji. Publiczny symbol otwierania
zdjęć i ikona aplikacji są prostymi własnymi figurami geometrycznymi;
prywatne referencje graficzne, fonty, Chrome i animacje Omarchy są pomijane.
Zgodność Ubuntu/RHEL wymaga wyników macierzy CI i testów na pulpitach,
nie wyłącznie wygenerowanego YAML. Docker w sesji przygotowania był
niedostępny, a CLI nie ma dostępu DNS; nie deklarujemy lokalnie wykonanych
testów na systemach, których tutaj nie uruchomiono.

Testy generatora i instalatora:

```bash
python3 -B -m unittest discover -s photo-viewer/publication/tests -v
python3 -B -m unittest discover -s photo-viewer/publication/templates/tests -v
```

Przygotowanie repozytorium nie zmienia działającej instalacji Omarchy ani
skryptu routingu RTX. Nowe źródła i instrukcje należą do pełnego backup/export
projektu. `finish --validated` pozostaje wymagane po potwierdzonym wdrożeniu live.
