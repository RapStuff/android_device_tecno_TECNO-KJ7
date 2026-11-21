#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Source after build/envsetup.sh. Accept the firmware-spelled target as an alias.
_kj7_rel=""
if [[ -f vendor/lineage/vars/aosp_target_release ]]; then
  _kj7_rel="-$(awk -F= '/^aosp_target_release=/{print $2}' vendor/lineage/vars/aosp_target_release)"
fi
case "${1:-userdebug}" in
  userdebug|lineage_TECNO-KJ7-userdebug|lineage_TECNO_KJ7-userdebug) lunch "lineage_TECNO_KJ7${_kj7_rel}-userdebug" ;;
  eng|lineage_TECNO-KJ7-eng|lineage_TECNO_KJ7-eng) lunch "lineage_TECNO_KJ7${_kj7_rel}-eng" ;;
  *) printf 'Usage: source device/tecno/TECNO-KJ7/lunch-kj7.sh [userdebug|eng]\n' >&2; unset _kj7_rel; return 2 ;;
esac
unset _kj7_rel
