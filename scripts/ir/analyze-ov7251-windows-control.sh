#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    printf 'usage: %s /path/to/ov7251.sys\n' "$0" >&2
    exit 2
fi

binary=$1
if [[ ! -f $binary ]]; then
    printf 'not a regular file: %s\n' "$binary" >&2
    exit 1
fi

for tool in sha256sum file objdump strings; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        printf 'missing tool: %s\n' "$tool" >&2
        exit 1
    fi
done

printf 'binary: %s\n' "$binary"
sha256sum "$binary"
file "$binary"

printf '%s\n' 'sections:'
objdump -h "$binary"

printf '%s\n' 'relevant initialization and callback registration:'
objdump -d -M intel \
    --start-address=0x140007a60 --stop-address=0x140007c60 "$binary"

printf '%s\n' 'relevant stream/mode dispatch:'
objdump -d -M intel \
    --start-address=0x140007c78 --stop-address=0x140008020 "$binary"

printf '%s\n' 'exposure/timing callback:'
objdump -d -M intel \
    --start-address=0x1400086d0 --stop-address=0x140008910 "$binary"

printf '%s\n' 'property/control callback:'
objdump -d -M intel \
    --start-address=0x1400089a0 --stop-address=0x140008bb8 "$binary"

printf '%s\n' 'stream callbacks:'
objdump -d -M intel \
    --start-address=0x140008e00 --stop-address=0x140008f20 "$binary"

printf '%s\n' 'candidate resource labels:'
strings -a -t x "$binary" | rg -i 'Reset|Strobe|Torch|Flash|LedRear|LedFront|Power0|Power1|Standby|WriteProtect' || true
