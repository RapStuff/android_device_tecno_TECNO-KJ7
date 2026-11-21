#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
device_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
exec python3 "$device_dir/tools/blobtool.py"
