#!/bin/bash
# Builds MusicGVA.kext (Lilu plugin) against the Lilu 1.7.3 source headers and MacKernelSDK.
#   ./build.sh            RELEASE
#   ./build.sh debug      DEBUG (logs with -musicgvadbg / -liludbgall)
set -euo pipefail
cd "$(dirname "$0")"
if [[ -z "${DL:-}" ]]; then   # downloads/ next to the repo (workspace layout) or two levels up (build/ layout)
  for d in ../../../../downloads ../../downloads; do [[ -d "$d/lilu-src" ]] && { DL="$(cd "$d" && pwd)"; break; }; done
fi
[[ -n "${DL:-}" ]] || { echo "set DL=<folder with lilu-src and mackernelsdk>"; exit 1; }
LILU="$DL/lilu-src/Lilu"; KSDK="$DL/mackernelsdk"
CCKEXT=$(ls /Library/Developer/CommandLineTools/usr/lib/clang/*/lib/darwin/libclang_rt.cc_kext.a | head -1)
CFG=RELEASE; DEFS=(-DMGVA); [[ "${1:-}" == debug ]] && { CFG=DEBUG; DEFS=(-DMGVA -DDEBUG); }
COMMON=(-arch x86_64 -mmacosx-version-min=10.15 -O2 -g -mkernel -fno-builtin -fno-common -fstack-protector-strong
        -nostdlibinc -isystem "$KSDK/Headers" -I "$LILU" -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT
        -DPRODUCT_NAME=MusicGVA -DMODULE_VERSION=1.0.0 "${DEFS[@]}" -Wall -Wno-unused-parameter)
rm -rf out && mkdir -p out/obj out/MusicGVA.kext/Contents/MacOS
clang   "${COMMON[@]}" -std=gnu11 -c src/kmod_info.c -o out/obj/kmod_info.o
for f in src/kern_musicgva.cpp "$LILU/Library/plugin_start.cpp"; do
  clang++ "${COMMON[@]}" -std=gnu++17 -fapple-kext -fno-rtti -fno-exceptions -c "$f" -o "out/obj/$(basename "${f%.cpp}").o"
done
clang++ -arch x86_64 -mmacosx-version-min=10.15 -nostdlib -Xlinker -kext -Xlinker -no_data_const -Xlinker -S \
        out/obj/*.o -L"$(xcrun --show-sdk-path)/usr/lib" -lkmodc++ -lkmod "$CCKEXT" \
        -o out/MusicGVA.kext/Contents/MacOS/MusicGVA 
cp src/Info.plist out/MusicGVA.kext/Contents/Info.plist
echo "built ($CFG): $(pwd)/out/MusicGVA.kext"
