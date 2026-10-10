#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
default_gcc=arm-none-eabi-gcc
default_objcopy=arm-none-eabi-objcopy
if ! command -v "$default_gcc" >/dev/null 2>&1 && [[ -x "$root/tools/linux/usr/bin/arm-none-eabi-gcc" ]]; then
    default_gcc="$root/tools/linux/usr/bin/arm-none-eabi-gcc"
    default_objcopy="$root/tools/linux/usr/bin/arm-none-eabi-objcopy"
fi
gcc="${ARM_GCC:-$default_gcc}"
objcopy="${ARM_OBJCOPY:-$default_objcopy}"
args=()
for define in "$@"; do
    if [[ ! "$define" =~ ^[A-Za-z_][A-Za-z0-9_]*(=[0-9]+)?$ ]]; then
        echo "Invalid define: $define" >&2
        exit 2
    fi
    args+=("-D$define")
done
for tool in "$gcc" "$objcopy"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Missing tool: $tool. Run ./setup-ubuntu.sh first." >&2
        exit 1
    fi
done
mkdir -p "$root/build"
# Remove previous outputs so a failed build cannot leave an old firmware to flash.
rm -f "$root/build/demo.elf" "$root/build/demo.bin" "$root/build/demo.map"
profile_args=(--profile "${KB_EFFECT_PROFILE_FILE:-$root/configs/effects/classic-breathe.json}"
              --output "$root/build/effect_config.h")
if [[ -n "${KB_EFFECT_PEAK:-}" ]]; then profile_args+=(--peak "$KB_EFFECT_PEAK"); fi
if [[ -n "${KB_EFFECT_GCLK_HZ:-}" ]]; then profile_args+=(--gclk-hz "$KB_EFFECT_GCLK_HZ"); fi
python3 "$root/tools/scripts/effect_profiles.py" "${profile_args[@]}"
"$gcc" "${args[@]}" -mcpu=cortex-m0plus -mthumb -Os -g -ffreestanding \
    -fdata-sections -ffunction-sections -Wall -Wextra -nostdlib \
    -Wl,--gc-sections "-Wl,-Map=$root/build/demo.map" \
    -I "$root/build" -I "$root/firmware/include" \
    -T "$root/firmware/linker/stm32l051c8.ld" "$root/firmware/startup/startup.s" \
    "$root/firmware/src/app/main.c" \
    "$root/firmware/src/board/board.c" "$root/firmware/src/drivers/mbia045.c" "$root/firmware/src/keyboard/keyboard_rgb.c" \
    "$root/firmware/src/effects/keyboard_effect.c" \
    "$root/firmware/src/effects/keyboard_wave.c" \
    -o "$root/build/demo.elf" -lgcc
"$objcopy" -O binary "$root/build/demo.elf" "$root/build/demo.bin"
ls -lh "$root/build/demo.elf" "$root/build/demo.bin"
