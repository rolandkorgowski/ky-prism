#!/usr/bin/env bash
set -Eeuo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
command -v python3 >/dev/null || { printf '%s\n' 'Install Python 3 first (Ubuntu: sudo apt install python3; RHEL: sudo dnf install python3).' >&2; exit 1; }
exec python3 -B "$root/tools/install.py" "$@"
