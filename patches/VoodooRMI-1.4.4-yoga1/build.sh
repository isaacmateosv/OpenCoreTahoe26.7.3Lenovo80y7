#!/bin/bash
# Builds the main VoodooRMI.kext with the Command Line Tools only: upstream VoodooRMI 1.4.3 (83868f0) with the
# Yoga 920 patch 1 applied by patch.py, versioned 1.4.4.
# Uses src/ (the patched VoodooRMI source) and the VoodooInput headers in ./voodooinput-headers.
# -Xlinker -S keeps the debug map (which records this folder's absolute path) out of the kext.
set -euo pipefail
cd "$(dirname "$0")"
SDK=$(xcrun --show-sdk-path); KH="$SDK/System/Library/Frameworks/Kernel.framework/Headers"
VI="$(pwd)/voodooinput-headers"
CCKEXT=$(ls /Library/Developer/CommandLineTools/usr/lib/clang/*/lib/darwin/libclang_rt.cc_kext.a | head -1)
INC=(-Isrc -Isrc/Configuration -Isrc/Functions -Isrc/Functions/Input -Isrc/LinuxCompat -Isrc/Transports -Isrc/Utility -I"$VI" -I"$VI/VoodooInputMultitouch")
COMMON=(-arch x86_64 -mmacosx-version-min=10.15 -O2 -g -mkernel -fno-builtin -fno-common -fstack-protector-strong
        -nostdlibinc -isystem "$KH" -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT -D__ACIDANTHERA_MAC_SDK=1 -Wall -Wno-unused-parameter -Wno-empty-body)
SRCS=(src/RMIBus.cpp src/RMIBusPDT.cpp src/Configuration/RMIConfiguration.cpp
      src/Functions/F01.cpp src/Functions/F03.cpp src/Functions/F11.cpp src/Functions/F12.cpp src/Functions/F17.cpp src/Functions/F30.cpp src/Functions/F3A.cpp
      src/Functions/Input/RMIFunction.cpp src/Functions/Input/RMIGPIOFunction.cpp src/Functions/Input/RMITrackpadFunction.cpp src/Functions/Input/RMITrackpointFunction.cpp)
rm -rf out && mkdir -p out/obj out/VoodooRMI.kext/Contents/MacOS
clang "${COMMON[@]}" -std=gnu11 -c kmod_info.c -o out/obj/kmod_info.o
OBJS=(out/obj/kmod_info.o)
for s in "${SRCS[@]}"; do
  o="out/obj/$(basename "${s%.cpp}").o"
  clang++ "${COMMON[@]}" -std=gnu++17 -fapple-kext -fno-rtti -fno-exceptions "${INC[@]}" -c "$s" -o "$o"
  OBJS+=("$o")
done
clang++ -arch x86_64 -mmacosx-version-min=10.15 -nostdlib -Xlinker -kext -Xlinker -no_data_const -Xlinker -S \
        "${OBJS[@]}" -L"$SDK/usr/lib" -lkmodc++ -lkmod "$CCKEXT" -o out/VoodooRMI.kext/Contents/MacOS/VoodooRMI
cp Info.plist out/VoodooRMI.kext/Contents/Info.plist
echo "built: $(pwd)/out/VoodooRMI.kext"
