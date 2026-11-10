# Przeniesienie ky-prism na Ubuntu 26.04.1 / RTX 4070

To jest pakiet **samej aplikacji**, nie obraz systemu Omarchy. Zawiera bieżący
kod C++, instalator Ubuntu, testy, ikonę i opcjonalnie ulubione, zakładki oraz
język. Nie zawiera zdjęć, cache, konfiguracji pulpitu, kont przeglądarek,
sterownika NVIDIA ani paczek Arch. Skopiuj zdjęcia osobno. Paczka z `user-data`
jest prywatną kopią użytkownika — może zawierać ścieżki do zdjęć.

## Instalacja na nowym komputerze

1. Przenieś `.tar.gz` oraz `.tar.gz.sha256`. W ich katalogu wykonaj
   `sha256sum -c ky-prism-export-...tar.gz.sha256` (wpisz rzeczywistą nazwę).
   Rozpakuj archiwum; wejdź do katalogu `ky-prism`.
2. Sprawdź `nvidia-smi`. Powinno pokazywać RTX 4070. Sterownik zainstaluj przez
   **Oprogramowanie i aktualizacje → Dodatkowe sterowniki** w Ubuntu, jeśli go
   jeszcze nie ma; po instalacji uruchom system ponownie.
3. Uruchom jako zwykły użytkownik, bez `sudo` przed całym poleceniem:

   ```bash
   bash run.sh install --install-cuda --nvjpeg ON --test
   bash run.sh restore-data
   bash run.sh doctor --gpu
   ~/.local/bin/ky-prism
   ```

Instalator sam poprosi o `sudo` przy instalacji zależności. Domyślne repozytoria
Ubuntu **Universe i Multiverse** muszą być dostępne. `--install-cuda` instaluje
`cuda-toolkit` na Ubuntu 26.04 (na 24.04: `nvidia-cuda-toolkit`) z podpisanych
repozytoriów Ubuntu. Nie dodaje repozytorium
innej wersji dystrybucji, nie zmienia routingu GPU ani nie wymusza wersji sterownika.
Jeżeli masz już CUDA Toolkit ≥12 z nvcc i nvJPEG, pomiń `--install-cuda`.

`--nvjpeg ON` wymaga nie tylko kompilacji modułu, ale też pomyślnego dekodowania
syntetycznych JPEG na GPU przed instalacją. Przy błędzie otrzymasz diagnostykę;
instalator nie zastąpi działającej wersji aplikacji. Możesz świadomie zainstalować
bez SDK przez `bash run.sh install --nvjpeg AUTO --test`; GPU nadal renderuje
obraz, dekodowanie miniaturek korzysta wtedy z libvips/libjpeg-turbo.

`restore-data` nigdy nie nadpisuje istniejących preferencji. Uruchom je przed
pierwszym otwarciem aplikacji i przy zamkniętym ky-prism. Ścieżki do zdjęć
pozostają oryginalne. Gdy zmienisz nazwę konta lub położenie zdjęć, użyj funkcji
odnajdywania zdjęcia w ulubionych; zakładki wskaż ponownie.

## Sprzęt i wydajność

- CUDA zawiera natywny kod **sm_89 dla RTX 4070 (Ada)**, sm_75 dla RTX 2060 oraz
  PTX dla zgodnych nowszych GPU. RTX 4070 nie musi kompilować naszego kernela
  skalowania przy pierwszym użyciu. Biblioteki NVIDIA mogą mieć własny rozruch.
- nvJPEG dekoduje obsługiwane JPEG miniaturek, CUDA wykonuje skalowanie Lanczos3,
  OpenGL rysuje zdjęcia i atlas miniaturek. RAW, TIFF, pełne obrazy i przypadki
  wymagające zachowania profilu koloru mają odpowiednie ścieżki CPU.
- Własna konwersja pikseli wybiera **AVX2** podczas działania. Oficjalna karta
  **i9-11900KB** wymienia SSE4.1/SSE4.2/AVX2. Nie należy utożsamiać go z 11900K
  ani wymuszać AVX-512 tylko na podstawie nazwy i9. `doctor` pokazuje flagi
  dostępne w nowym systemie. Brak globalnego `-march=native` chroni przenośność.
- Zachowane są asynchroniczne kolejki, ograniczenia RAM/VRAM oraz kontrola
  anulowania. Stałe 100% CPU/GPU nie jest celem przeglądarki; liczy się czas
  wyświetlenia i responsywność. Nie obiecujemy przyspieszenia bez pomiaru na
  docelowym komputerze. Budowanie dobiera 1–8 zadań do CPU i pamięci; możesz
  podać `--jobs 4`. To nie zmienia liczby wątków dekodera podczas przeglądania.
- Skalowanie/kolory nie są upraszczane dla uzyskania wyższego wyniku. Brak
  globalnego `-ffast-math`, agresywnego overclockingu i zmian konfiguracji RTX.

Na Ubuntu dostępność rozmycia zależy od kompozytora (GNOME nie musi odtwarzać
efektów Hyprland). Funkcje aplikacji nie wymagają Omarchy. Sprawdź renderer
w aplikacji; `doctor --gpu` potwierdza CUDA, a nie renderer okna OpenGL.

## Backup/export i zależności offline

```bash
bash run.sh backup /ścieżka/do/nośnika
bash run.sh export /ścieżka/do/nośnika
bash run.sh verify
```

Oba polecenia tworzą zweryfikowaną SHA-256 kopię źródeł i preferencji. Nie
kopiują binarki powiązanej z poprzednią dystrybucją. Na nowym systemie aplikacja
jest kompilowana z jego bibliotekami. `verify` sprawdza źródła w rozpakowanym
katalogu. Archiwum jest dodatkowo sprawdzane podczas tworzenia.

**Ten pakiet nie jest instalatorem w pełni offline.** Źródła i preferencje
są kompletne, ale pierwsza instalacja zależności Ubuntu wymaga Internetu lub
lokalnego repozytorium Ubuntu zawierającego te zależności. Pełny eksport Omarchy
z paczkami pacmana nie dostarcza odpowiednich `.deb`. Po zapewnieniu zależności
i CUDA można budować bez sieci przez `bash run.sh install --no-deps --nvjpeg ON`.
Nie przedstawiamy takiego trybu jako testu czystego Ubuntu bez sieci.

## Dokumentacja sprzętu

- [Intel i9-11900KB — specyfikacja](https://www.intel.com/content/www/us/en/products/sku/215570/intel-core-i911900kb-processor-24m-cache-up-to-4-90-ghz/specifications.html)
- [NVIDIA nvJPEG](https://docs.nvidia.com/cuda/nvjpeg/)
- [CUDA na Linux](https://docs.nvidia.com/cuda/cuda-installation-guide-linux/)
- [Ubuntu 26.04: cuda-toolkit](https://packages.ubuntu.com/en/resolute/amd64/cuda-toolkit)

Testy na źródłowym Omarchy nie zastępują testów Ubuntu/RTX 4070. Wynik `doctor`
i testy uruchomione instalatorem na nowym komputerze są końcową weryfikacją.
