#!/bin/bash
# lidwatch-check.sh: shows LidWatch's counters and, with "selftest", proves the replay path end to end.
#   tools/lidwatch-check.sh            -> print counters
#   tools/lidwatch-check.sh selftest   -> (lid open) fake a lost "open" event and confirm LidWatch replays it
set -u
stats() { ioreg -rc LidWatch -k "LidWatch Stats" -d 1 | grep -o '"LidWatch Stats" = {.*}' | sed 's/"LidWatch Stats" = //; s/,/\n  /g; s/^{/  /; s/}$//'; }
if ! ioreg -rc LidWatch -d 1 | grep -q LidWatch; then
    echo "LidWatch is not loaded (check Kernel > Add and SSDT-LIDW.aml in config.plist)"; exit 1
fi
echo "LidWatch counters:"; stats
[ "${1:-}" = "selftest" ] || exit 0
before=$(ioreg -rc LidWatch -k "LidWatch Stats" -d 1 | grep -o '"Replayed events"=[0-9]*' | cut -d= -f2)
osascript -l JavaScript -e '
ObjC.import("IOKit");
var svc = $.IOServiceGetMatchingService(0, $.IOServiceMatching("LidWatch"));
var d = $.NSDictionary.dictionaryWithObjectForKey($.NSNumber.numberWithBool(true), "SelfTest");
var kr = $.IORegistryEntrySetCFProperties(svc, d);
"SelfTest request -> 0x" + (kr >>> 0).toString(16);' 2>&1
echo "waiting 8 s for the watchdog to notice and replay..."; sleep 8
after=$(ioreg -rc LidWatch -k "LidWatch Stats" -d 1 | grep -o '"Replayed events"=[0-9]*' | cut -d= -f2)
stats
if [ "${after:-0}" -gt "${before:-0}" ]; then echo "PASS: missed event detected and replayed ($before -> $after)"; else echo "FAIL: no replay ($before -> $after)"; fi
/usr/bin/log show --last 15s --style compact --predicate 'eventMessage CONTAINS "PMRD: Clamshell"' 2>/dev/null | tail -n +2
