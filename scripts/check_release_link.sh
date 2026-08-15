#!/usr/bin/env bash
# Guards the firmware download link rendered on the /update page.
#
# There is no Unity test for this. generateOTAForm() is private (WebUI.h), and
# WebUI.h pulls in WebServer.h + WebSocketsServer.h while WebSockets is
# lib_ignore'd in [env:native] - so the Web UI cannot be compiled or
# instantiated host-side at all. Widening a private method to public purely to
# assert a hyperlink is a worse trade than this script.
#
# What can actually regress, and what each check catches:
#   1. The constant is deleted or renamed        -> the page loses its link
#   2. The URL drifts to http:// or a wrong repo -> users download nothing,
#                                                   or the wrong firmware
#   3. Someone inlines a literal URL in the UI   -> constants.h stops being the
#      instead of using the constant                single source of truth
#   4. The OTA form stops referencing it         -> constant kept, link gone

set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

CONSTANTS="include/constants.h"
WEBUI="lib/webui/src/WebUI.cpp"
EXPECTED_URL="https://github.com/vukomir/esp32-modbusbridge/releases"

violations=0
fail() {
    echo "❌  $1"
    violations=$((violations + 1))
}

# 1 + 2: the constant exists and holds exactly the expected https URL.
if ! grep -qE "^#define[[:space:]]+FIRMWARE_RELEASES_URL[[:space:]]+\"${EXPECTED_URL}\"$" "$CONSTANTS"; then
    fail "FIRMWARE_RELEASES_URL is missing from $CONSTANTS, or is not exactly:"
    echo "      \"$EXPECTED_URL\""
    echo "    Found: $(grep -n 'FIRMWARE_RELEASES_URL' "$CONSTANTS" || echo '<nothing>')"
fi

# 3: no hardcoded release URLs anywhere in first-party source. The constant's
# own #define line is the single legitimate occurrence.
while IFS= read -r line; do
    fail "Hardcoded release URL - use FIRMWARE_RELEASES_URL instead: $line"
done < <(grep -RIn "github\.com/vukomir/esp32-modbusbridge" src lib include \
            --include="*.cpp" --include="*.h" --include="*.hpp" 2>/dev/null \
            | grep -v "^${CONSTANTS}:.*#define FIRMWARE_RELEASES_URL" \
            | grep -vE '^[^:]+:[0-9]+:[[:space:]]*(//|\*)')
# NOTE: the comment filter above is anchored to grep's "path:line:" prefix on
# purpose. The looser ':[[:space:]]*//' used by check_readonly.sh cannot be
# reused here - it matches the '://' inside https:// and would silently discard
# every URL line, making this check pass unconditionally.

# 4: the OTA form actually renders the constant, and does so safely. A link
# opened with target=_blank from a plain-HTTP LAN page needs rel=noopener.
if ! grep -q "FIRMWARE_RELEASES_URL" "$WEBUI"; then
    fail "$WEBUI no longer references FIRMWARE_RELEASES_URL - the /update page has lost its download link."
elif ! grep -q "FIRMWARE_RELEASES_URL.*rel='noopener" "$WEBUI"; then
    fail "The FIRMWARE_RELEASES_URL link in $WEBUI is missing rel='noopener noreferrer'."
fi

if [ "$violations" -gt 0 ]; then
    echo ""
    echo "❌  $violations firmware-download-link violation(s) found."
    echo "    The /update page must link users to where release .bin files live."
    exit 1
fi

echo "✅  Firmware download link intact: $EXPECTED_URL"
exit 0
