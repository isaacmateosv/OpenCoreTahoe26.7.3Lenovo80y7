#!/bin/bash
# install-tools.sh: builds and installs this repo's everyday tools for the Yoga 920 Hackintosh in one go.
#
#   musicfix     keeps Apple Music playing (docs/apple-music.md); installed as a login agent
#   autorotate   the Auto-Rotate switch, driven by the "Auto-Rotate" Shortcut (starts off after each reboot)
#   rotate       manual screen angle: rotate 0|90|180|270
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
    rm -f "$BIN/musicfix" "$BIN/autorotate" "$BIN/rotate"
    say "Removed musicfix (and its login agent), autorotate and rotate."
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
build musicfix musicfix.m -fobjc-arc -framework AppKit

say "Apple Music fix:"
if [ ! -e /System/Library/Extensions/AppleIntelKBLGraphicsVADriver.bundle ]; then
    say "  note: this Mac has no AppleIntelKBLGraphicsVADriver; musicfix was written for the Yoga 920's Intel UHD 620"
fi
if ! out=$("$BIN/musicfix" install 2>&1); then
    say "  musicfix install failed: $out"
    exit 1
fi
say "  $out"
"$BIN/musicfix" status | sed 's/^/  /'

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
