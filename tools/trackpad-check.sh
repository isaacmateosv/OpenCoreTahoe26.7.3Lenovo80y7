#!/bin/bash
# Trackpad health check for the Yoga 920 (patched RMII2C "wake fix 2").
# Usage: trackpad-check.sh [seconds]   — keep hands off for an idle reading,
#        or move a finger on the trackpad during the run for a "moving" reading.
DIR="$(cd "$(dirname "$0")" && pwd)"
SECS="${1:-5}"
echo "== loaded RMII2C =="
kmutil showloaded 2>/dev/null | grep -E "RMII2C|VoodooRMI" | sed -E 's/ +/ /g' | awk '{for(i=1;i<=NF;i++) if($i ~ /^com\./){print $i, $(i+1), $(i+2)}}'
echo "== RMII2C Stats (IORegistry) =="
ioreg -w0 -l -c RMII2C 2>/dev/null | grep -E '"(Interrupt mode|RMII2C Stats|RMII2C Patch)"' | sed 's/^[ |]*//' | tr ',' '\n' | sed 's/^/   /'
echo "== interrupt counters over ${SECS}s =="
if [ ! -x "$DIR/irqstat" ]; then
    echo "   (building tools/irqstat first)"
    clang -O2 -framework CoreFoundation -framework IOKit -o "$DIR/irqstat" "$DIR/irqstat.c" ||
        { echo "   could not build irqstat; install the Command Line Tools (xcode-select --install)"; exit 1; }
fi
"$DIR/irqstat" "$SECS" | grep -E "TPL1|totals|TPD0" | tail -4
