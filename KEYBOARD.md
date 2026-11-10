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
| Ctrl+Shift+C | Kopiuj zaznaczone pliki; bez zaznaczenia bieżące zdjęcie |
| Ctrl+Shift+D | Wyszukiwanie duplikatów |
| Ctrl+I | Pokaż / ukryj EXIF |
| Ctrl+Alt+O | Krycie interfejsu: suwak (strzałki ←/→, PageUp/PageDown); Esc zamyka |
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

W lewej liście strzałki wybierają pozycję, a Enter otwiera plik lub katalog.
F6 przenosi fokus z listy do ścieżki i dalej do zdjęcia (lub widocznego EXIF).
W polach tekstowych działają zwykłe skróty edycji, m.in. Ctrl+A/C/V/X/Z.
Ctrl+C kopiuje zaznaczony tekst; kopiowanie zdjęcia używa osobnego Ctrl+Shift+C.

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
