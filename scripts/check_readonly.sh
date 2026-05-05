#!/usr/bin/env bash
# SAFETY GUARD: this firmware is read-only on the Modbus bus by design.
# This script fails CI if any source file under src/ or lib/ contains a
# Modbus *write* function code where it could plausibly be used as one.
#
# Modbus function codes we forbid:
#   0x05  Write Single Coil
#   0x06  Write Single Register
#   0x0F  Write Multiple Coils
#   0x10  Write Multiple Registers
#
# We also forbid the obvious method names. If you ever add legitimate write
# support (don't), update this script AND the SAFETY note in
# lib/modbus_client/include/ModbusClient.h.

set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# Search only first-party source. Excludes vendored libs, .pio, tests-of-write-detection.
SEARCH_PATHS=(src lib/modbus_client lib/poller lib/inverters lib/meters lib/inverter_core lib/mqtt_client lib/wifi_manager lib/webui lib/config)

PATTERN_OPCODES='\b(0x05|0x06|0x0F|0x10)\b'
PATTERN_METHODS='write(SingleCoil|SingleRegister|MultipleCoils|MultipleRegisters|Coil|Register)\s*\('

violations=0

# Look for write opcodes used as Modbus function codes. We accept the false
# positives (e.g. 0x10 used as a buffer size) and require an explicit allowlist
# comment "// MODBUS_WRITE_OPCODE_OK: <reason>" on the same line if intentional.
for path in "${SEARCH_PATHS[@]}"; do
    [ -d "$path" ] || continue
    while IFS= read -r line; do
        # skip lines explicitly allowlisted
        if grep -q 'MODBUS_WRITE_OPCODE_OK' <<< "$line"; then continue; fi
        echo "FORBIDDEN OPCODE: $line"
        violations=$((violations + 1))
    done < <(grep -RInE "$PATTERN_OPCODES" "$path" \
                --include="*.cpp" --include="*.h" --include="*.hpp" \
                | grep -v -E ':\s*//')   # skip pure comment lines
done

# Look for write method names regardless of opcode literals.
for path in "${SEARCH_PATHS[@]}"; do
    [ -d "$path" ] || continue
    while IFS= read -r line; do
        echo "FORBIDDEN METHOD: $line"
        violations=$((violations + 1))
    done < <(grep -RInE "$PATTERN_METHODS" "$path" \
                --include="*.cpp" --include="*.h" --include="*.hpp" \
                | grep -v -E ':\s*//')
done

if [ "$violations" -gt 0 ]; then
    echo ""
    echo "❌  $violations read-only safety violation(s) found."
    echo "    This firmware MUST NOT issue Modbus write commands."
    echo "    If a flagged line is intentional and unrelated to Modbus writes,"
    echo "    add the comment '// MODBUS_WRITE_OPCODE_OK: <reason>' on the same line."
    exit 1
fi

echo "✅  Read-only Modbus invariant holds: no write opcodes or write method names found."
exit 0
