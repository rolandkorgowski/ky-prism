#!/usr/bin/env bash
set -Eeuo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
action=${1:-help}
if (($#)); then shift; fi
case "$action" in
  install) exec bash "$root/install.sh" "$@" ;;
  export|backup|restore-data|doctor|verify) exec python3 -B "$root/tools/transfer.py" "$action" "$@" ;;
  *) printf '%s\n' 'ky-prism: ./run.sh install [--install-cuda --nvjpeg ON --test]' './run.sh {export|backup} KATALOG' './run.sh restore-data' './run.sh doctor [--gpu]' './run.sh verify' 'Instrukcja: docs/TRANSFER-PL.md' ;;
esac
