#!/bin/sh
# ky-prism — skrypt uruchomieniowy i narzędziowy.
# Autor programu: Krzysztof Kylen Lenartowicz.
#
#   ./run.sh                     w rozpakowanej paczce (export, kopia programu):
#                                ustawienia z paczki + instalacja (pyta przed zmianami);
#                                w klonie git: ta pomoc
#   ./run.sh install [--kompiluj]
#                                zależności z repozytoriów systemu (sudo/doas), kompilacja,
#                                testy i instalacja do ~/.local; w paczce „na konto”
#                                gotowy program bez kompilacji (--kompiluj wymusza)
#   ./run.sh build | test        kompilacja / kompilacja i wszystkie testy
#   ./run.sh run [ZDJĘCIE]       uruchom zbudowany program
#   ./run.sh backup [--ustawienia|--program]
#                                kopia do ./backups/: same ustawienia (ulubione, zakładki,
#                                język, krycie) albo cały program (źródła, historia git,
#                                zbudowany program, ustawienia); bez opcji — wybór
#   ./run.sh restore PLIK        przywróć ustawienia z kopii albo z paczki export
#   ./run.sh export [--system|--konto] [--bez-ustawien]
#                                paczka do ./dist/: --system (inny komputer lub system:
#                                źródła, kompilacja na miejscu), --konto (inne konto tego
#                                komputera: także gotowy program); bez opcji — wybór
#   ./run.sh import [PLIK]       w rozpakowanej paczce: ustawienia z paczki
#   ./run.sh dependencies [check|plan|install]
#   ./run.sh verify              zgodność zainstalowanych plików z rejestrem
#   ./run.sh gtk-test [--screenshots KATALOG | --stress N]
#   ./run.sh diagnose-hang PID [--seconds N]
#
# Zgodny z POSIX sh: Ubuntu 24.04/26.04, Debian, Fedora, Arch/Omarchy, FreeBSD, OpenBSD.
# Opis instalacji na każdym systemie: docs/INSTALACJA.md.

set -eu
root=$(cd -- "$(dirname -- "$0")" && pwd -P)

say() { printf '\033[1;32m[ky-prism]\033[0m %s\n' "$*"; }
die() { printf '\033[1;31m[ky-prism]\033[0m %s\n' "$*" >&2; exit 1; }

# Polecenie instalacji pakietu na tym systemie (podpowiedź, gdy czegoś brakuje).
install_hint() {
    case "$(uname -s)" in
        FreeBSD) echo "su -m root -c 'pkg install -y $*'  (albo: sudo pkg install $*)" ;;
        OpenBSD) echo "doas pkg_add $*" ;;
        Linux)
            if command -v apt-get >/dev/null 2>&1; then echo "sudo apt install $*"
            elif command -v pacman >/dev/null 2>&1; then echo "sudo pacman -S --needed $*"
            elif command -v dnf >/dev/null 2>&1; then echo "sudo dnf install $*"
            else echo "zainstaluj: $*"; fi ;;
        *) echo "zainstaluj: $*" ;;
    esac
}

# Narzędzia skryptu są w Pythonie 3 — wymaganym także do budowania.
need_python() {
    command -v python3 >/dev/null 2>&1 && return 0
    case "$(uname -s)" in
        OpenBSD) pkg=python%3 ;;
        Linux) command -v pacman >/dev/null 2>&1 && pkg=python || pkg=python3 ;;
        *) pkg=python3 ;;
    esac
    die "Brak python3. Zainstaluj go: $(install_hint "$pkg")  — potem ponów ./run.sh $*"
}

jobs() {
    if [ -n "${PRISM_BUILD_JOBS:-}" ]; then echo "$PRISM_BUILD_JOBS"; return; fi
    n=$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)
    # Kompilacja Qt/GTK zużywa ok. 1 GiB RAM na zadanie.
    [ "$n" -gt 4 ] && n=4
    echo "$n"
}

prism_py() { python3 -B "$root/prismctl.py" "$@"; }

configure_and_build() {
    runtime_check=OFF
    if [ "$1" = install ] && [ "${PRISM_WITH_NVJPEG:-AUTO}" = ON ]; then runtime_check=ON; fi
    fresh=""
    # Katalog build skopiowany z innego klonu/komputera ma obcy cache.
    if [ -f "$root/build/CMakeCache.txt" ] && ! grep -Fqx "CMAKE_HOME_DIRECTORY:INTERNAL=$root" "$root/build/CMakeCache.txt"; then
        fresh=--fresh
    fi
    # shellcheck disable=SC2086
    cmake $fresh -S "$root" -B "$root/build" -DCMAKE_BUILD_TYPE=Release \
        -DPRISM_WITH_NVJPEG="${PRISM_WITH_NVJPEG:-AUTO}" -DPRISM_REQUIRE_NVJPEG_RUNTIME="$runtime_check"
    cmake --build "$root/build" -j "$(jobs)"
}

nvidia_env() {
    if [ -d /proc/driver/nvidia/gpus ]; then
        export __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia
    fi
}

action=${1:-}
case "$action" in
    "")
        # Rozpakowana paczka: pierwsze uruchomienie (ustawienia + instalacja).
        if [ -f "$root/KOPIA.txt" ] && [ ! -f "$root/.ky-prism-z-paczki" ]; then
            need_python
            exec python3 -B "$root/prismctl.py" z-paczki
        fi
        sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'
        ;;
    -h|--help|help)
        sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'
        ;;
    build|test|install)
        need_python "$action"
        shift
        compile=0
        for a in "$@"; do
            case "$a" in
                --kompiluj|--compile) compile=1 ;;
                *) die "Nieznana opcja $action: $a" ;;
            esac
        done
        python3 -B "$root/package.py" check-source
        # Paczka „na konto”: gotowy program dla tego systemu — bez kompilacji.
        if [ "$action" = install ] && [ "$compile" = 0 ] && [ ! -f "$root/build/ky-prism" ] \
            && prism_py prebuilt-ok; then
            say "Instalacja gotowego programu z paczki (bez kompilacji; ./run.sh install --kompiluj wymusza kompilację)"
            exec python3 -B "$root/package.py" install --prebuilt
        fi
        python3 -B "$root/i18n/generate.py" --check
        if [ "$action" = install ]; then python3 -B "$root/dependencies.py" install
        else python3 -B "$root/dependencies.py" check; fi
        configure_and_build "$action"
        [ "$action" = build ] && exit 0
        # Test prawdziwego okna (etykieta "display") zależy od kompozytora:
        # należy do build/test, ale nigdy nie blokuje instalacji.
        if [ "$action" = install ]; then ctest --test-dir "$root/build" --output-on-failure -LE display
        else ctest --test-dir "$root/build" --output-on-failure; fi
        python3 -B -m unittest discover -s "$root/tests" -p 'test_*.py'
        if [ "$action" = install ]; then exec python3 -B "$root/package.py" install; fi
        ;;
    backup|restore|export|import|z-paczki)
        need_python "$action"
        shift
        if [ "$action" = export ] || [ "$action" = backup ]; then
            # Stara forma: ./run.sh export KATALOG — paczka trafia do KATALOG.
            case "${1:-}" in
                ""|-*) ;;
                *) [ -d "$1" ] || mkdir -p "$1"; dir=$1; shift; set -- "$@" --katalog "$dir" ;;
            esac
        fi
        exec python3 -B "$root/prismctl.py" "$action" "$@"
        ;;
    run)
        shift
        exec "$root/build/ky-prism" "$@"
        ;;
    gtk-test)
        # Prawdziwe okno GTK w sesji; --screenshots KATALOG zapisuje PNG, --stress N mierzy pamięć.
        shift
        nvidia_env
        exec "$root/build/prism-gtk-test" "$@"
        ;;
    gpu-test)
        # Poprzednie płótno Qt (ky-prism-qt); budowane tylko z Qt Widgets.
        if [ -d /proc/driver/nvidia/gpus ] && [ -n "${DISPLAY:-}" ]; then
            export __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia QT_QPA_PLATFORM=xcb
        fi
        exec "$root/build/prism-gpu-test"
        ;;
    gallery-bench)
        shift
        for arg in "$@"; do
            if [ "$arg" = --gpu ] && [ -d /proc/driver/nvidia/gpus ] && [ -n "${DISPLAY:-}" ]; then
                export __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia QT_QPA_PLATFORM=xcb
            fi
        done
        exec "$root/build/prism-gallery-bench" "$@"
        ;;
    dependencies)
        need_python "$action"
        exec python3 -B "$root/dependencies.py" "${2:-check}"
        ;;
    nvjpeg-test) exec "$root/build/prism-nvjpeg-test" --require-nvjpeg ;;
    nvjpeg-sanitize)
        sanitizer=$(command -v compute-sanitizer || true)
        if [ -z "$sanitizer" ] && [ -x /opt/cuda/bin/compute-sanitizer ]; then sanitizer=/opt/cuda/bin/compute-sanitizer; fi
        [ -n "$sanitizer" ] || die 'Brakuje compute-sanitizer z CUDA Toolkit.'
        exec "$sanitizer" --tool memcheck --leak-check full --error-exitcode 1 "$root/build/prism-nvjpeg-test" --require-nvjpeg
        ;;
    nvjpeg-bench) shift; exec "$root/build/prism-nvjpeg-test" "$@" --no-reference --require-nvjpeg ;;
    cpu-bench) shift; exec "$root/build/prism-nvjpeg-test" "$@" --cpu ;;
    diagnose-hang) need_python "$action"; shift; exec python3 -B "$root/diagnose-hang.py" "$@" ;;
    publish) need_python "$action"; shift; exec python3 -B "$root/publication/prepare.py" "$@" ;;
    verify) need_python "$action"; exec python3 -B "$root/package.py" verify ;;
    *)
        sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'
        die "Nieznane polecenie: $action"
        ;;
esac
