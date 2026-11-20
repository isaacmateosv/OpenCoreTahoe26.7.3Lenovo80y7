#!/bin/bash
# After the reboot: is MusicGVA active, does Music play, is hardware video decoding back? (no sudo)
HERE="$(cd "$(dirname "$0")" && pwd)"
echo "== boot-args: $(nvram boot-args 2>/dev/null | cut -f2-)"
echo "== kext loaded: $(kextstat 2>/dev/null | grep -c org.yoga920.MusicGVA) (1 = yes)"
echo "== kernel log:"; /usr/bin/log show --last boot --style compact --predicate 'process == "kernel" AND eventMessage CONTAINS "mgva"' 2>/dev/null | grep -oE 'mgva.*' | sort | uniq -c | head
echo "== hwgva-id seen by ioreg (expect the REAL board-id, Mac-827FB448E656EC26):"
ioreg -p IODeviceTree -d2 -l | grep -E '"hwgva-id"|"board-id"' | head -2
echo "== hardware video decode (expect frames OK, hardware required):"
swift "$HERE/hwdecode-test.swift" 2>/dev/null | grep -E 'decode|FAILED'
echo "== Music: open Music and play any song, then:"
echo "   osascript -e 'tell application \"Music\" to get {player state, player position}'   (position must grow)"
