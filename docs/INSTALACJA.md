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
| FreeBSD (jako root) | `pkg install -y git python3 sudo`, potem `visudo` i odkomentuj `%wheel ALL=(ALL:ALL) ALL` |
| OpenBSD (jako root) | `pkg_add git python%3` oraz `echo 'permit persist :wheel' > /etc/doas.conf` |

Na FreeBSD i OpenBSD konto użytkownika musi należeć do grupy `wheel`.
Pakiety instaluje się przez `sudo` (FreeBSD) lub `doas` (OpenBSD).
Program działa w sesji graficznej X11 albo Wayland z OpenGL.

## 2. Instalacja z GitHuba

```sh
git clone https://github.com/rolandkorgowski/ky-prism.git
cd ky-prism
./run.sh install
```

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
tar xzf ky-prism-0.13.0-przeniesienie.tar.gz
cd ky-prism-0.13.0
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
