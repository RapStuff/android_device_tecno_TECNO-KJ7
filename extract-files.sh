#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
device_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
if [[ $# -ne 1 ]]; then
    printf 'Usage: %s <extracted dump directory>\n' "$0" >&2
    exit 2
fi
exec python3 "$device_dir/tools/blobtool.py" --source "$1"
