#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
if (( $# > 1 )); then
    echo 'Usage: ./flash.sh [firmware.elf]' >&2
    exit 2
fi
elf="${1:-$root/build/demo.elf}"
if [[ ! -f "$elf" ]]; then
    echo "Firmware not found: $elf. Run ./build.sh first." >&2
    exit 1
fi
elf="$(realpath -- "$elf")"
ocd="${OPENOCD:-openocd}"
if ! command -v "$ocd" >/dev/null 2>&1; then
    echo "Missing tool: $ocd. Run ./setup-ubuntu.sh first." >&2
    exit 1
fi
# Tcl braced paths support spaces; reject characters that can break Tcl quoting.
if [[ "$elf" == *'{'* || "$elf" == *'}'* || "$elf" == *\\* || "$elf" == *$'\n'* || "$elf" == *$'\r'* ]]; then
    echo 'Firmware path contains unsupported Tcl characters.' >&2
    exit 2
fi
# Native ST-LINK SWD plus software reset works without an NRST connection.
exec "$ocd" -f interface/stlink-dap.cfg -f target/stm32l0.cfg \
    -c 'reset_config none' \
    -c 'stm32l0.cpu configure -event reset-start {adapter speed 100}' \
    -c "program {$elf} verify reset exit"
