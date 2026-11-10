#!/usr/bin/env bash
set -Eeuo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
case "${1:-help}" in
 diagnose-hang) shift; exec python3 -B "$root/diagnose-hang.py" "$@" ;;
 publish) shift; exec python3 -B "$root/publication/prepare.py" "$@" ;;
 build|test|install)
   python3 -B "$root/package.py" check-source
   python3 -B "$root/i18n/generate.py" --check
   if [[ $1 == install ]]; then python3 -B "$root/dependencies.py" install; else python3 -B "$root/dependencies.py" check; fi
   runtime_check=OFF
   if [[ $1 == install && ${PRISM_WITH_NVJPEG:-AUTO} == ON ]]; then runtime_check=ON; fi
   fresh=()
   # A build tree copied from another checkout/machine has a foreign cache.
   if [[ -f $root/build/CMakeCache.txt ]] && ! grep -Fqx "CMAKE_HOME_DIRECTORY:INTERNAL=$root" "$root/build/CMakeCache.txt"; then fresh=(--fresh); fi
   cmake "${fresh[@]}" -S "$root" -B "$root/build" -DCMAKE_BUILD_TYPE=Release -DPRISM_WITH_NVJPEG="${PRISM_WITH_NVJPEG:-AUTO}" -DPRISM_REQUIRE_NVJPEG_RUNTIME="$runtime_check"
   cmake --build "$root/build" -j "${PRISM_BUILD_JOBS:-2}"
   # The live-window test (label "display") depends on the running compositor;
   # it belongs to build/test but never blocks an installation.
   if [[ $1 == install ]]; then ctest --test-dir "$root/build" --output-on-failure -LE display
   else ctest --test-dir "$root/build" --output-on-failure; fi
   python3 -B -m unittest discover -s "$root/tests" -p 'test_*.py'
   if [[ $1 == install ]]; then exec python3 -B "$root/package.py" install; fi
   ;;
 gtk-test)
   # Real window of the GTK front end on the session display; --screenshots DIR saves PNGs.
   shift
   if [[ -d /proc/driver/nvidia/gpus ]]; then export __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia; fi
   exec "$root/build/prism-gtk-test" "$@" ;;
 gpu-test)
   # Legacy Qt canvas (ky-prism-qt); built only when Qt Widgets is available.
   if [[ -d /proc/driver/nvidia/gpus && -n ${DISPLAY:-} ]]; then export __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia QT_QPA_PLATFORM=xcb; fi
   exec "$root/build/prism-gpu-test" ;;
 gallery-bench)
   shift
   for arg in "$@"; do
     if [[ $arg == --gpu && -d /proc/driver/nvidia/gpus && -n ${DISPLAY:-} ]]; then
       export __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia QT_QPA_PLATFORM=xcb
     fi
   done
   exec "$root/build/prism-gallery-bench" "$@" ;;
 dependencies) exec python3 -B "$root/dependencies.py" "${2:-check}" ;;
 nvjpeg-test) exec "$root/build/prism-nvjpeg-test" --require-nvjpeg ;;
 nvjpeg-sanitize)
   sanitizer=$(command -v compute-sanitizer || true)
   if [[ -z $sanitizer && -x /opt/cuda/bin/compute-sanitizer ]]; then sanitizer=/opt/cuda/bin/compute-sanitizer; fi
   if [[ -z $sanitizer ]]; then printf '%s\n' 'Brakuje compute-sanitizer z CUDA Toolkit.' >&2; exit 1; fi
   exec "$sanitizer" --tool memcheck --leak-check full --error-exitcode 1 "$root/build/prism-nvjpeg-test" --require-nvjpeg ;;
 nvjpeg-bench) shift; exec "$root/build/prism-nvjpeg-test" "$@" --no-reference --require-nvjpeg ;;
 cpu-bench) shift; exec "$root/build/prism-nvjpeg-test" "$@" --cpu ;;
 run) shift; exec "$root/build/ky-prism" "$@" ;;
 export|backup)
   if [[ $# == 4 && $3 == --target && $4 == ubuntu ]]; then
     exec python3 -B "$root/transfer.py" "$1" "$2"
   fi
   exec python3 -B "$root/package.py" "$@" ;;
 transfer) shift; exec python3 -B "$root/transfer.py" export "$@" ;;
 verify) exec python3 -B "$root/package.py" verify ;;
 *) printf '%s\n' 'ky-prism: ./run.sh {build|test|install|run [zdjęcie]|gtk-test [--screenshots KATALOG]|gallery-bench [KATALOG] [--gpu]|export KATALOG|backup KATALOG|publish [--output KATALOG]|verify|dependencies [check|plan|install]|diagnose-hang PID [--seconds N]}' 'Instalator dobiera brakujące zależności z repozytoriów dystrybucji (sudo).' ;;
esac
