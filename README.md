# Android device tree for TECNO Spark 20 Pro+ (`TECNO_KJ7`)

```
#
# SPDX-License-Identifier: Apache-2.0
#
```

## Device specifications

| Feature | Specification |
| --- | --- |
| Platform | MediaTek `mt6789` (Helio G99) |
| Architecture | `arm64-v8a` / `armeabi-v7a` (`cortex-a76` / `cortex-a55`) |
| GPU | ARM Mali (`ro.hardware.vulkan=mali`) |
| Display | `1080 x 2436`, `480 dpi` (`xxhdpi`) |
| Storage | UFS (`11270000.ufshci`), Virtual A/B Dynamic Partitions |
| Boot / Recovery | Boot header v4, recovery in `vendor_boot` (`BOARD_MOVE_RECOVERY_RESOURCES_TO_VENDOR_BOOT := true`) |
| Biometrics | Optical UDFPS Fingerprint HAL + Face AIDL HAL |
| NFC | ST NFC (`android.hardware.nfc-service.st`) |
| Shipped Android | Android 12 (Shipping API level `31`) |
| Kernel | Linux `5.10` GKI (`5.10.272-ElationKernel` prebuilt) |

## Repositories & tree paths

| Repository | Branch | Checkout path |
| --- | --- | --- |
| `RapStuff/android_device_tecno_kj7` | `lineage-24.0` | `device/tecno/TECNO_KJ7` |
| `RapStuff/android_device_tecno_kj7-kernel` | `lineage-24.0` | `device/tecno/TECNO-KJ7-kernel` |
| `RapStuff/android_vendor_tecno_kj7` | `lineage-24.0` | `vendor/tecno/TECNO-KJ7` |

## Building

```bash
source build/envsetup.sh
lunch lineage_TECNO_KJ7-cp2a-userdebug
mka bacon
```

### Build notes
- **Filesystem selection:** Builds with `WITH_GMS=true` use `erofs` for `system`, `system_ext`, `product`, and `vendor`; vanilla builds (`WITH_GMS` unset) use `ext4` with Lineage reserved partition sizing (`BoardConfigReservedSize.mk`).
- **AVB / vbmeta:** `vbmeta.img` is generated with `--flags 3` (`HASHTREE_DISABLED | VERIFICATION_DISABLED`) by default (`BOARD_AVB_MAKE_VBMETA_IMAGE_ARGS += --flags 3`).
- **Transsion partitions:** Stock dynamic partitions (`tr_carrier`, `tr_company`, `tr_mi`, `tr_preload`, `tr_product`, `tr_region`, `tr_theme`) are preserved via `transsion-partitions.mk` and `prebuilt-images/`.
