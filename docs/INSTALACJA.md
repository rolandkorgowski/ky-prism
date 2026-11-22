# ky-prism — instalacja, kopie i przenoszenie

Wszystko robi jeden skrypt, `./run.sh`, zgodny z POSIX sh. Instaluje
ky-prism na koncie użytkownika (`~/.local`). Brakujące pakiety pobiera
z oficjalnych repozytoriów systemu: przez `sudo` na Linuksie, `doas` lub
`sudo` na BSD. Nie dodaje obcych repozytoriów i nie zmienia ustawień systemu.

Obsługiwane systemy: Ubuntu 24.04 i 26.04, Debian 13, Fedora 40+, Arch
Linux i pochodne (Omarchy, Manjaro, EndeavourOS), FreeBSD 14/15, OpenBSD 7.6+.
Wymagane: GTK ≥ 4.12, Qt ≥ 6.4, libvips, LibRaw, OpenGL lub OpenGL ES.

## 1. Przygotowanie systemu (raz)

Potrzebny jest tylko Python 3. Resztę `./run.sh install` dobierze sam.

| System | Polecenie |
| --- | --- |
| Ubuntu 24.04 / 26.04, Debian, Mint | `sudo apt install git python3` |
| Arch, Omarchy, Manjaro, EndeavourOS | `sudo pacman -Syu --needed git python` |
| Fedora | `sudo dnf install git python3` |
| FreeBSD, OpenBSD | dwa warianty — niżej |

### FreeBSD i OpenBSD: po co root i dwa warianty

**Root jest potrzebny wyłącznie do instalacji pakietów systemowych**: GTK,
Qt, libvips, LibRaw, CMake i Python. Na BSD pakiety (`pkg`, `pkg_add`) może
instalować tylko root. Wszystko inne skrypt robi na Twoim koncie i zapisuje
tylko w Twoim katalogu domowym (`~/.local`): sprawdza źródła, kompiluje,
uruchamia testy, instaluje program, wpis w menu i ikonę. Pakietów
systemowych nie da się na BSD zainstalować bez roota — wybierasz tylko, kto
wykona ten jeden krok.

**Wariant A — skrypt sam instaluje pakiety (przez sudo/doas).** Raz, jako
root:
- FreeBSD: `pkg install -y git python3 sudo`, potem w `visudo` odkomentuj
  `%wheel ALL=(ALL:ALL) ALL`.
- OpenBSD: `pkg_add git python%3`, potem **dopisz** regułę (istniejący plik
  zostaje):
  `grep -qx 'permit persist :wheel' /etc/doas.conf || echo 'permit persist :wheel' >> /etc/doas.conf`.

Potem jako zwykły użytkownik z grupy `wheel`: `git clone …`, `cd ky-prism`,
`./run.sh install`. Skrypt sam doinstaluje brakujące pakiety, pytając
o Twoje hasło.

**Wariant B — bez roota w skrypcie.** Raz, jako root (Ty albo
administrator), wszystkie pakiety naraz:
- FreeBSD: `pkg install -y git cmake pkgconf python3 qt6-base gtk4 libepoxy vips libraw lcms2 jpeg-turbo libglvnd mesa-libs desktop-file-utils xdg-utils`
- OpenBSD: `pkg_add git cmake python%3 qt6-qtbase gtk+4 libvips libraw lcms2 jpeg desktop-file-utils xdg-utils`

Potem jako dowolny użytkownik, bez grupy `wheel`, sudo i doas:
`git clone …`, `cd ky-prism`, `./run.sh install --bez-roota`. Skrypt nigdy
nie wywoła sudo ani doas. Gdy jakiegoś pakietu brakuje, zatrzyma się
i wypisze dokładne polecenie dla roota.

**Różnice:**

| | Wariant A | Wariant B |
| --- | --- | --- |
| Root robi raz | Git, Python, reguła sudo/doas | wszystkie pakiety |
| Twoje konto | w grupie `wheel`, może używać sudo/doas | dowolne |
| `./run.sh install` | doinstalowuje pakiety sam (pyta o hasło) | nigdy nie podnosi uprawnień |
| Nowa wersja potrzebuje nowego pakietu | instaluje się automatycznie | skrypt wypisze polecenie dla roota |
| Dla kogo | własny komputer | komputer współdzielony, konto bez uprawnień administratora |

Program działa w sesji graficznej X11 albo Wayland z OpenGL.

## 2. Instalacja z GitHuba

```sh
git clone https://github.com/rolandkorgowski/ky-prism.git
cd ky-prism
./run.sh install
```

Na BSD w wariancie B: `./run.sh install --bez-roota`.

`install` wykonuje po kolei:
1. sprawdza sumy kontrolne źródeł,
2. instaluje brakujące pakiety,
3. kompiluje program,
4. uruchamia testy,
5. instaluje program, wpis w menu aplikacji i ikonę.

Przed nadpisaniem plików robi ich kopię w
`~/.local/state/omarchy-prism/install-backups/`.

Aktualizacja: `git pull && ./run.sh install`.

**PATH na BSD.** Jeśli polecenie `ky-prism` nie działa w terminalu, dopisz
do `~/.profile` linię `export PATH="$HOME/.local/bin:$PATH"` i zaloguj się
ponownie. Menu aplikacji działa bez tego.

**Pendrive (USB)** w programie korzysta z `udisksctl`, więc działa tylko na
Linuksie. Na BSD zamontuj nośnik systemowo i otwórz jego katalog.

**OpenBSD: kolor i EXIF.** Pakiet libvips w OpenBSD jest zbudowany bez lcms2
i bez libexif. Dlatego:
- Zdjęcia z osadzonym profilem ICC są pokazywane jako sRGB. Panel
  informacji o tym pisze. Zdjęć w CMYK nie da się otworzyć.
- Panel EXIF pokazuje mniej pól.
- Orientacja zdjęcia (pion/poziom) działa, bo ky-prism czyta ją z EXIF sam.

## 3. Kopie zapasowe — `./run.sh backup`

```sh
./run.sh backup               # w terminalu pyta: ustawienia czy cały program
./run.sh backup --ustawienia  # ulubione, zakładki, język, krycie interfejsu
./run.sh backup --program     # źródła, historia git, zbudowany program + ustawienia
./run.sh restore backups/ky-prism-ustawienia-RRRRMMDD-GGMMSS.tar.gz
```

- **Miejsce kopii.** Kopie trafiają do `backups/` w katalogu programu (prawa
  700), a pliki mają prawa 600. Kolejna kopia nigdy nie nadpisuje
  poprzedniej.
- **Co jest w kopii.** Bez zdjęć i bez cache miniatur.
- **Co przyjmuje `restore`.** Kopię ustawień, kopię programu albo paczkę z
  `export`. Każdy plik jest sprawdzany (format, rozmiar, brak dowiązań
  symbolicznych). Obecne ustawienia są najpierw zapisywane w
  `backups/ky-prism-ustawienia-przed-przywroceniem-….tar.gz`.

## 4. Przenoszenie — `./run.sh export`

```sh
./run.sh export               # w terminalu pyta, dokąd przenosisz
./run.sh export --system      # inny komputer lub inny system
./run.sh export --konto       # inne konto na tym samym komputerze
./run.sh export --system --bez-ustawien
```

| Wariant | Plik w `dist/` | Zawartość |
| --- | --- | --- |
| `--system` | `ky-prism-WERSJA-przeniesienie.tar.gz` | źródła, historia git, ustawienia; kompilacja na miejscu |
| `--konto` | `ky-prism-WERSJA-na-konto.tar.gz` | to samo i gotowy program — bez kompilacji |

W każdej paczce są `KOPIA.txt` (co zawiera, z jakiego systemu) i
`INSTALACJA.txt` (te kroki w skrócie). Obok paczki powstaje plik `.sha256`.

Na docelowym komputerze lub koncie:

```sh
tar xzf ky-prism-0.15.0-przeniesienie.tar.gz
cd ky-prism-0.15.0
./run.sh
```

`./run.sh` w rozpakowanej paczce najpierw pokazuje, co zrobi, i pyta o zgodę.
Potem przywraca ustawienia z paczki i instaluje program. To samo ręcznie:
`./run.sh import`, a następnie `./run.sh install`.

Gotowy program z paczki `--konto` instaluje się tylko wtedy, gdy system,
wersja i biblioteki się zgadzają. W innym wypadku `./run.sh install`
kompiluje ky-prism na miejscu. `./run.sh install --kompiluj` wymusza
kompilację.

Historię git z paczki odtworzysz poleceniem
`git clone ky-prism.git.bundle ky-prism`.

## Sprawdzone systemy

Instalację z paczki `export --system` sprawdzono w maszynach wirtualnych:
rozpakowanie, `./run.sh import`, `./run.sh install` z pakietami
z oficjalnych repozytoriów, testy i start programu. Wyniki są w
`docs/HISTORY.md`, w sekcji tej wersji.
