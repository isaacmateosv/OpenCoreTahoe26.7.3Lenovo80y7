#!/bin/bash
# Builds LidWatch.kext and SSDT-LIDW.aml (Yoga 920 lid-event watchdog) with Command Line Tools only.
set -euo pipefail
cd "$(dirname "$0")"
SDK=$(xcrun --show-sdk-path)
KH="$SDK/System/Library/Frameworks/Kernel.framework/Headers"
CCKEXT=$(ls /Library/Developer/CommandLineTools/usr/lib/clang/*/lib/darwin/libclang_rt.cc_kext.a | head -1)
IASL=${IASL:-/Applications/MaciASL.app/Contents/MacOS/iasl-stable}
COMMON=(-arch x86_64 -mmacosx-version-min=10.15 -O2 -g -mkernel -fno-builtin -fno-common -fstack-protector-strong
        -nostdlibinc -isystem "$KH" -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT -Wall -Wno-unused-parameter)
rm -rf out && mkdir -p out/obj out/LidWatch.kext/Contents/MacOS
clang   "${COMMON[@]}" -std=gnu11 -c src/kmod_info.c -o out/obj/kmod_info.o
clang++ "${COMMON[@]}" -std=gnu++17 -fapple-kext -fno-rtti -fno-exceptions -c src/LidWatch.cpp -o out/obj/LidWatch.o
clang++ -arch x86_64 -mmacosx-version-min=10.15 -nostdlib -Xlinker -kext -Xlinker -no_data_const -Xlinker -S \
        out/obj/kmod_info.o out/obj/LidWatch.o -L"$SDK/usr/lib" -lkmodc++ -lkmod "$CCKEXT" \
        -o out/LidWatch.kext/Contents/MacOS/LidWatch
cp src/Info.plist out/LidWatch.kext/Contents/Info.plist
"$IASL" -p out/SSDT-LIDW SSDT-LIDW.dsl > out/iasl.log 2>&1 || { cat out/iasl.log; exit 1; }
grep -E "Compilation (successful|complete)|Errors|Warnings" out/iasl.log | head -3
echo "built: $(pwd)/out/LidWatch.kext and out/SSDT-LIDW.aml"
