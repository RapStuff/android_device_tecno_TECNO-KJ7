#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Source after build/envsetup.sh. Accept the firmware-spelled target as an alias.
case "${1:-userdebug}" in
  userdebug|lineage_TECNO-KJ7-userdebug|lineage_TECNO_KJ7-userdebug) lunch lineage_TECNO_KJ7-userdebug ;;
  eng|lineage_TECNO-KJ7-eng|lineage_TECNO_KJ7-eng) lunch lineage_TECNO_KJ7-eng ;;
  *) printf 'Usage: source device/tecno/TECNO-KJ7/lunch-kj7.sh [userdebug|eng]\n' >&2; return 2 ;;
esac
