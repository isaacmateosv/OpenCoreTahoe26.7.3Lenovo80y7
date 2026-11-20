#!/bin/bash
# install-tools.sh: builds and installs this repo's everyday tools for the Yoga 920 Hackintosh in one go.
#
#   autobright   automatic brightness from the real light sensor, switched by System Settings > Displays >
#                "Automatically adjust brightness" (login agent; docs/auto-brightness.md)
#   autorotate   the Auto-Rotate switch, driven by the "Auto-Rotate" Shortcut (starts off after each reboot)
#   rotate       manual screen angle: rotate 0|90|180|270
#   hubsensor    test tool: light sensor (lux) and hinge angle (docs/sensors-and-pen.md)
# (musicfix is retired: with full SIP, Apple Music is fixed by the MusicGVA kext; this script removes its agent.)
#
# Usage: tools/install-tools.sh               build and install everything (safe to run again)
#        tools/install-tools.sh --uninstall   remove the login agent, switch auto-rotate off, delete the programs
#
# No sudo. Needs Xcode (license accepted) or the Command Line Tools (xcode-select --install).
set -eu
cd "$(dirname "$0")"
BIN="$HOME/.local/bin"
say() { printf '%s\n' "$*"; }

if [ "${1:-}" = "--uninstall" ]; then
    if [ -x "$BIN/musicfix" ]; then "$BIN/musicfix" uninstall; fi
    if [ -x "$BIN/autorotate" ]; then "$BIN/autorotate" off >/dev/null; fi
    if [ -x "$BIN/autobright" ]; then "$BIN/autobright" off >/dev/null; fi
    rm -f "$BIN/musicfix" "$BIN/autorotate" "$BIN/rotate" "$BIN/autobright" "$BIN/hubsensor"
    say "Removed autobright (and its login agent), autorotate, rotate, hubsensor and any old musicfix."
    say "The Auto-Rotate shortcut stays in Shortcuts; delete it there if you no longer want it."
    exit 0
fi
if [ $# -gt 0 ]; then
    say "usage: $0 [--uninstall]"
    exit 2
fi

# A compiler that works. Xcode's clang refuses to run until its license is accepted, so fall back to the
# Command Line Tools' own clang (pointed at their SDK).
CC=""
SDK=()
if /usr/bin/clang --version >/dev/null 2>&1; then
    CC=/usr/bin/clang
elif [ -x /Library/Developer/CommandLineTools/usr/bin/clang ]; then
    CC=/Library/Developer/CommandLineTools/usr/bin/clang
    SDK=(-isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk)
else
    say "No working C compiler. Run: xcode-select --install (or open Xcode once and accept its license), then run this again."
    exit 1
fi

mkdir -p "$BIN"
build() {   # build NAME SOURCE FLAGS...: compile next to the old program, then swap it in (safe while it runs)
    local name="$1" src="$2"
    shift 2
    "$CC" -O2 -Wall "${SDK[@]+"${SDK[@]}"}" "$@" -o "$BIN/$name.new" "$src"
    mv -f "$BIN/$name.new" "$BIN/$name"
    say "  built $BIN/$name"
}
say "Building:"
build autorotate autorotate.c -Wno-unused-result -framework IOKit -framework CoreFoundation -framework ApplicationServices
build rotate rotate.c -framework ApplicationServices -framework IOKit
build autobright autobright.c -framework IOKit -framework CoreFoundation -framework ApplicationServices -lobjc
build hubsensor hubsensor.c -framework IOKit -framework CoreFoundation

if [ -x "$BIN/musicfix" ]; then
    "$BIN/musicfix" uninstall >/dev/null 2>&1 || true
    rm -f "$BIN/musicfix"
    say "Removed the retired musicfix agent (Apple Music is fixed by the MusicGVA kext)."
fi

say "Auto-brightness:"
if "$BIN/autobright" status | grep -q on; then
    launchctl kickstart -k "gui/$(id -u)/org.yoga920.autobright" && say "  agent restarted with the new build"
else
    "$BIN/autobright" on >/dev/null && say "  agent installed (login agent org.yoga920.autobright)"
fi
say "  switch it with System Settings > Displays > Automatically adjust brightness"

say "Auto-Rotate:"
if [ "$("$BIN/autorotate" status)" = on ]; then
    say "  it is on now; switch it off and on again to use the new build"
fi
if shortcuts list 2>/dev/null | grep -qx "Auto-Rotate"; then
    say "  the Auto-Rotate shortcut is already in Shortcuts"
else
    # macOS only imports signed shortcuts, and a signature belongs to the Mac that made it, so the repo ships the
    # shortcut unsigned and it is signed here, on your own Mac.
    tmpdir=$(mktemp -d)
    if shortcuts sign --mode anyone --input shortcuts/Auto-Rotate.shortcut --output "$tmpdir/Auto-Rotate.shortcut" >/dev/null 2>&1; then
        open "$tmpdir/Auto-Rotate.shortcut"
        say "  Shortcuts opened the Auto-Rotate shortcut: click Add Shortcut"
    else
        say "  could not sign the Auto-Rotate shortcut (macOS needs a signed copy to import it). Sign it by hand:"
        say "    shortcuts sign --mode anyone --input tools/shortcuts/Auto-Rotate.shortcut --output ~/Desktop/Auto-Rotate.shortcut"
        say "  then double-click the copy on the Desktop. Or build it yourself in Shortcuts: one Run Shell Script action"
        say "  running  \$HOME/.local/bin/autorotate toggle"
    fi
fi
say "  the switch needs Shortcuts > Settings > Advanced > Allow Running Scripts"
say "Done."
