#!/bin/bash
# Builds RMII2C.kext (VoodooRMI 1.4.3 I2C transport + Yoga 920 wake fix) with Command Line Tools only.
set -euo pipefail
cd "$(dirname "$0")"
SDK=$(xcrun --show-sdk-path)
KH="$SDK/System/Library/Frameworks/Kernel.framework/Headers"
CCKEXT=$(ls /Library/Developer/CommandLineTools/usr/lib/clang/*/lib/darwin/libclang_rt.cc_kext.a | head -1)
COMMON=(-arch x86_64 -mmacosx-version-min=10.15 -O2 -g -mkernel -fno-builtin -fno-common -fstack-protector-strong
        -nostdlibinc -isystem "$KH" -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT -Wall -Wno-unused-parameter -Wno-empty-body)
rm -rf out && mkdir -p out/obj out/RMII2C.kext/Contents/MacOS
clang   "${COMMON[@]}" -std=gnu11 -c src/kmod_info.c -o out/obj/kmod_info.o
clang++ "${COMMON[@]}" -std=gnu++17 -fapple-kext -fno-rtti -fno-exceptions -Iinclude -Isrc -c src/RMII2C.cpp -o out/obj/RMII2C.o
clang++ -arch x86_64 -mmacosx-version-min=10.15 -nostdlib -Xlinker -kext -Xlinker -no_data_const -Xlinker -S \
        out/obj/kmod_info.o out/obj/RMII2C.o -L"$SDK/usr/lib" -lkmodc++ -lkmod "$CCKEXT" \
        -o out/RMII2C.kext/Contents/MacOS/RMII2C
cp src/Info.plist out/RMII2C.kext/Contents/Info.plist
echo "built: $(pwd)/out/RMII2C.kext"
