# Praca nad ky-prism w Claude Code z przeglądarki i telefonu

Claude Code w chmurze (claude.ai/code oraz aplikacja Claude na telefonie)
pracuje na repo z GitHuba w maszynie Anthropic. Twój komputer nie musi być
włączony. Sesje rozpoczęte na telefonie widać w przeglądarce i odwrotnie.
Zasady projektu Claude czyta z pliku `CLAUDE.md` w repo.

## Jednorazowe przygotowanie

1. **Dostęp do prywatnego repo.** Zainstaluj aplikację **Claude** na GitHubie:
   https://github.com/apps/claude/installations/new
   - wybierz swoje konto,
   - w „Repository access” zaznacz **Only select repositories**,
   - wybierz `ky-prism-private` (opcjonalnie także `ky-prism`).
2. **Logowanie.** Otwórz https://claude.ai/code, zaloguj się i połącz konto
   GitHub („Sign in with GitHub”).
3. **Środowisko.** Pod polem wiadomości otwórz wybór środowiska i edytuj
   **Default** (albo dodaj nowe, np. „ky-prism”):
   - **Network access:** zostaw **Limited** z zaznaczonym „Allow package
     managers”. To wystarczy do pobierania pakietów Ubuntu.
   - **Setup script:** wklej zawartość pliku
     [`claude-code-cloud-setup.sh`](claude-code-cloud-setup.sh) albo wybierz
     go przyciskiem „Choose file”. Instaluje kompilator, GTK 4, Qt 6,
     libvips, LibRaw i Xvfb.
   - Zapisz zmiany.

## Codzienna praca

- **Przeglądarka:** https://claude.ai/code, repozytorium
  `rolandkorgowski/ky-prism-private`, gałąź `main`. Opisz po polsku, co
  zmienić.
- **Telefon:** aplikacja Claude (iOS/Android) → **Code** (albo
  https://claude.ai/code/new w przeglądarce telefonu). Wybierz repo,
  gałąź i opisz zadanie.
- **Wynik:** Claude kompiluje, uruchamia testy, robi commit na `main`
  w `ky-prism-private`, wysyła go na GitHuba i podaje link do pobrania.
  Na komputerze zainstalujesz go poleceniem `git pull && ./run.sh install`.
- **Publiczne repo** (`ky-prism`) zmieniaj tylko poleceniem „publikuj”.
  Najprościej z sesji na swoim komputerze, gdzie jest skonfigurowane.

## Ograniczenia chmury

- Nie ma ekranu, więc okna programu nie widać. Testy bez okna działają
  normalnie (`ctest -LE display`). Test okna można próbować pod Xvfb
  (`xvfb-run -a ./run.sh test`), ale tego nie sprawdzano.
- Drukarki i schowek Twojego komputera nie są dostępne. Wydruk sprawdzisz
  po instalacji na swoim komputerze.
- Moje notatki z tego komputera nie przechodzą do chmury. Wszystko, co sesja
  w chmurze ma wiedzieć, jest w `CLAUDE.md`.
- Sesje w chmurze są dostępne w planach Pro, Max, Team i Enterprise.
