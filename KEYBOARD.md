# ky-prism — obsługa klawiaturą

W głównym oknie `F1` otwiera tę listę, a `Ctrl+F1` informacje o autorze.
`Tab` / `Shift+Tab` przechodzą między kontrolkami. Widoczna ramka wskazuje fokus.
Przyciski można uruchamiać Spacją lub Enterem. Zwykłe litery i strzałki
nie przechwytują wpisywania tekstu w ścieżkach i filtrach.

| Klawisze | Działanie w głównym oknie |
| --- | --- |
| Ctrl+Alt+L | Przełącz język PL / EN (przycisk z flagą obok About) |
| Ctrl+T | Nowa karta |
| Ctrl+Tab / Ctrl+Shift+Tab | Następna / poprzednia karta |
| Ctrl+Shift+W | Zamknij bieżącą kartę |
| Ctrl+U | Otwórz pendrive |
| Ctrl+Shift+P | Porównaj dwa zdjęcia / zakończ porównanie |
| Ctrl+O | Wybierz zdjęcie do otwarcia |
| Ctrl+B | Pokaż / ukryj listę plików |
| Ctrl+L lub Alt+D | Pokaż panel, zaznacz ścieżkę katalogu do wpisania |
| F6 / Shift+F6 | Kolejny / poprzedni obszar: zdjęcie, lista, ścieżka, widoczny EXIF |
| Alt+← / Alt+→ | Wstecz / Dalej w historii katalogów |
| Alt+↑ | Katalog nadrzędny |
| Ctrl+D | Dodaj / usuń zakładkę bieżącego katalogu (gwiazdka obok serca) |
| Ctrl+Shift+B | Przeglądarka zakładek |
| Ctrl+Shift+H | Dodaj / usuń bieżące zdjęcie z ulubionych (serce) |
| Ctrl+Shift+F | Przeglądarka ulubionych |
| Ctrl+C | Kopiuj wyświetlone zdjęcie do schowka: obraz (edytory, komunikatory) i plik (menedżer plików) |
| Ctrl+E | Eksportuj kopię pliku: systemowe okno „Zapisz jako”, kopia bajt w bajt z EXIF |
| Ctrl+P | Drukuj zaznaczone zdjęcia (bez zaznaczenia: wyświetlone) — drukarka z systemu, karta „Zdjęcie”: rozmiar, skala, kolor, jakość, kilka na kartce |
| Ctrl+Shift+C | Kopiuj zaznaczone pliki; bez zaznaczenia bieżące zdjęcie |
| Ctrl+Shift+D | Wyszukiwanie duplikatów |
| Ctrl+I | Pokaż / ukryj EXIF |
| Ctrl+Alt+O | Wygląd: suwak krycia interfejsu (strzałki ←/→, PageUp/PageDown) i przełącznik „Wygładzaj piksele w powiększeniu” (domyślnie wyłączony: ostre piksele od 100 %); Esc zamyka |
| Ctrl+M | Maksymalizuj / przywróć okno |
| F11 / Esc | Pełny ekran / wyjdź z pełnego ekranu |
| Ctrl++ / Ctrl+− | Zoom do środka widoku |
| Ctrl+0 / Ctrl+1 | Dopasuj / oryginalne 1:1 |
| Ctrl+Esc | Anuluj operację ulubionych |
| Ctrl+W | Zamknij program |

Gdy fokus jest **na zdjęciu**:

| Klawisze | Działanie |
| --- | --- |
| ← / → lub PageUp / PageDown | Poprzednie / następne zdjęcie |
| F / 1 | Dopasuj / oryginalne 1:1 |
| + (również =) / − | Przybliż / oddal |
| Shift+strzałki | Przesuń widok o 64 piksele logiczne |
| Ctrl+Shift+strzałki | Przesuń widok o 256 pikseli logicznych |
| Delete | Przenieś wyświetlone zdjęcie do kosza (zawsze po potwierdzeniu) |

W lewej liście strzałki wybierają pozycję, a Enter otwiera plik lub katalog
(fokus zostaje w liście). Kliknięcie pliku myszą otwiera zdjęcie i przenosi
na nie fokus, więc ← / → od razu przełączają zdjęcia z jego folderu.
F6 przenosi fokus z listy do ścieżki i dalej do zdjęcia (lub widocznego EXIF).
W polach tekstowych działają zwykłe skróty edycji, m.in. Ctrl+A/C/V/X/Z.
Ctrl+C w polu tekstowym lub w zaznaczonym tekście kopiuje ten tekst; poza nimi
kopiuje wyświetlone zdjęcie do schowka. Prawy przycisk myszy na zdjęciu, klawisz
Menu lub Shift+F10 otwiera menu: kopiuj do schowka, eksportuj kopię pliku,
ulubione, EXIF, pełny ekran, przenieś do kosza.

## Przeciąganie myszą z lewej listy

W trybie listy i miniatur zdjęcie można przeciągnąć lewym przyciskiem myszy.
Jeśli zdjęcie należy do zaznaczenia (Ctrl/Shift+klik), przeciąga się całe
zaznaczenie. Foldery nie są przeciągane.

- **Do innego programu** (menedżer plików, przeglądarka, komunikator)
  przeciągnięcie przekazuje pliki tylko do skopiowania. Oryginał zostaje
  na miejscu.
- **Na zdjęcie w programie** otwiera je; na drugie zdjęcie porównania
  wstawia je do porównania.
- **Na folder na liście** kopiuje zdjęcia do tego folderu (folder podświetla
  się pod kursorem). Gdy jest tam już **inny** plik o tej samej nazwie,
  pojawia się pytanie: Anuluj, Pomiń istniejące (domyślne) albo Nadpisz.
  Bez wybrania „Nadpisz” nic nie zostanie nadpisane. Gdy w folderze jest już
  dokładnie ten sam plik (identyczny co do bajtu), okienko o tym informuje
  i niczego nie nadpisuje. Dowiązania, foldery i pliki specjalne o tej nazwie
  nigdy nie są zmieniane.

## Przenoszenie do kosza (Delete)

- **Delete na liście lub miniaturach** przenosi do kosza zaznaczone pliki
  (Ctrl/Shift+klik zaznacza wiele). To samo robi prawy przycisk myszy na
  pliku w bibliotece → „Przenieś do kosza…”. Prawy przycisk na
  niezaznaczonym pliku najpierw zaznacza tylko ten plik.
- **Delete na zdjęciu** (fokus na zdjęciu) albo „Przenieś do kosza…” w menu
  zdjęcia przenosi tylko wyświetlone zdjęcie.
- Zawsze pojawia się pytanie z listą plików, ich rozmiarem i datą.
  **Domyślna odpowiedź to Anuluj**: Enter i Esc niczego nie zmieniają.
  Przenosi dopiero przycisk „Przenieś do kosza”: kliknij go albo przejdź do
  niego Tabem i naciśnij Enter. Ten przycisk celowo nie ma skrótu Alt+litera.
- Pliki trafiają do **kosza systemowego**, skąd można je przywrócić (np.
  w menedżerze plików). Program nigdy nie usuwa niczego trwale. Gdy na danym
  dysku nie ma kosza albo brak uprawnień do folderu, plik zostaje na miejscu,
  a pasek stanu mówi dlaczego.
- Foldery, dowiązania i pliki specjalne nigdy nie trafiają do kosza. Plik,
  który zmienił się po pytaniu, zostaje na miejscu.
- Gdy do kosza trafi wyświetlone zdjęcie, w jego miejscu otwiera się
  następne zdjęcie z folderu (albo poprzednie, gdy było ostatnie).
- W polu ścieżki katalogu Delete usuwa znak, jak zwykle.

## Zakładki i ulubione

- Ctrl+F: filtr; Tab przechodzi do tabeli, Enter otwiera zaznaczoną pozycję.
- Zakładki: Delete usuwa tylko zakładkę, zachowując katalog. Alt+D zapisuje
  bieżący katalog, Alt+U usuwa wybraną zakładkę, Alt+O ją otwiera.
- Zakładki: Alt+1 sortuje po nazwie, Alt+2 po ścieżce. Powtórzenie zmienia kierunek.
- Ulubione: Alt+1…4 sortują po nazwie, ścieżce, MD5 i rozmiarze (liczbowo).
- Ulubione: Alt+D uruchamia odnajdywanie pliku, Alt+U usuwa odwołanie do zdjęcia,
  Alt+A anuluje pracę, Alt+O otwiera zaznaczone zdjęcie.
- Odświeżanie: F5 / Ctrl+R, a także Alt+W. Zamknięcie: Ctrl+W, Esc lub Alt+Z.
- Klawisze sortowania i usuwania działają w swoich przeglądarkach; Delete
  w polu filtra edytuje tekst, zamiast usuwać zakładkę.

## Kopiowanie i duplikaty

- Alt+W: wybierz miejsce kopii / katalog do przeszukania. Systemowe okno wyboru
  obsługuje Tab, wpisywanie ścieżek, Enter oraz Esc (anulowanie wyboru).
- Alt+Z: zapisz kopię po wybraniu miejsca. Cel istniejący nie jest nadpisywany.
- W wynikach duplikatów Home i strzałki wybierają grupę; → rozwija grupę,
  ↓ wybiera konkretną ścieżkę. Tab przechodzi do przycisków.
- Alt+K: poproś o przeniesienie zaznaczonego pliku do kosza. Pojawia się
  **osobne potwierdzenie**, domyślnie Anuluj. Esc pozostawia oba pliki;
  aby potwierdzić, wybierz przycisk przeniesienia Tabem i naciśnij Enter.
- Alt+A: anuluj trwającą pracę; po zakończeniu ten sam klawisz zamyka dialog.
- Ctrl+W / Esc w pracującym dialogu najpierw anuluje pracę; po jej zakończeniu
  zamknij okno ponownie. Zamknięcie całej aplikacji anuluje jej zadania.

## About

Przycisk About lub Ctrl+F1: autor **Krzysztof Kylen Lenartowicz** i informacja
© 2026. Animacja trwa 640 ms, zatrzymuje się także po ukryciu strony lub okna.
F1 otwiera stronę Klawiatura. Alt+1 / Alt+2 przełączają strony tego dialogu;
Ctrl+W, Esc lub Alt+Z zamykają go. W tabeli skrótów można używać strzałek,
PageUp/PageDown i Tab; nie trzeba przewijać myszką.

Ctrl+F4 działa jako dodatkowe zamknięcie okna. Zoom ma warianty Ctrl+=,
Ctrl+Plus oraz klawisze bloku numerycznego.


0.9: Lista/Miniatury są oddzielnymi przyciskami osiągalnymi Tabem; Enter lub
Spacja wybiera tryb. F6 przechodzi także do aktywnej siatki miniatur. Strzałki
wybierają pozycje, Enter otwiera zdjęcie/katalog; Ctrl/Shift zaznaczają wiele.
Sortowanie: Tab do pola, Alt+↓ / Spacja otwiera menu pod polem; strzałki,
Enter zatwierdza, Esc zamyka. Przy jednej karcie użyj Ctrl+T lub ⊞, aby dodać
kolejną. Przyciski × w porównaniu osiągalne są Tabem i zamykają osobno zdjęcia.

0.10: sortowanie ma cztery bezpośrednie przyciski (A–Z, MB, Utw., Zmi.).
Wszystkie są dostępne Tabem i aktywowane Enterem/Spacją. Duplikaty mają własny
przycisk; skrót Ctrl+Shift+D pozostaje. Kopiowanie: Alt+W wybiera cel,
Alt+Z zapisuje, Esc/Ctrl+W zamyka lub anuluje trwające zadanie.

0.13 (GTK 4): skróty bez zmian. Doszło `Ctrl+Alt+O` — suwak krycia interfejsu.
W oknie „Załaduj zdjęcie”: Alt+↑ katalog nadrzędny, Ctrl+L ścieżka, Enter
otwiera zaznaczone zdjęcie lub wchodzi do katalogu, Esc anuluje. Alt+litera
uruchamia przycisk z podkreśloną literą także w dialogach GTK.

0.14: Delete przenosi do kosza zaznaczone pliki biblioteki albo wyświetlone
zdjęcie, zawsze po potwierdzeniu (domyślnie Anuluj). Prawy przycisk na pliku
w bibliotece otwiera menu z tą samą czynnością.

0.14.1: kliknięcie pliku w bibliotece lub karty przenosi fokus na zdjęcie, więc
← / → od razu przełączają zdjęcia. Enter w liście zostawia fokus w liście.
