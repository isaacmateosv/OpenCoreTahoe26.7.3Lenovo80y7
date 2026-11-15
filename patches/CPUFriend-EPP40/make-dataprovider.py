#!/usr/bin/env python3 -I
"""Rebuilds CPUFriendDataProvider.kext from macOS's own MacBookPro15,2 power profile,
changing only the Speed Shift energy hint (EPP) in every FrequencyVector.
Same kext layout as acidanthera's Tools/ResourceConverter.sh.
Usage: make-dataprovider.py [EPP hex, default 0x40] [output dir, default .]"""
import os, plistlib, sys
NEW_EPP = int(sys.argv[1], 16) if len(sys.argv) > 1 else 0x40
OUT = sys.argv[2] if len(sys.argv) > 2 else "."
SRC = "/System/Library/Extensions/IOPlatformPluginFamily.kext/Contents/PlugIns/X86PlatformPlugin.kext/Contents/Resources/Mac-827FB448E656EC26.plist"
raw = open(SRC, "rb").read()
fmt = plistlib.FMT_BINARY if raw.startswith(b"bplist") else plistlib.FMT_XML
d = plistlib.loads(raw)
key = b"epp" + b"\x00" * 17          # 20-byte key slot, value follows
vecs = d["IOPlatformPowerProfile"]["FrequencyVectors"]
for i, blob in enumerate(vecs):
    b = bytearray(blob); j = b.find(key)
    assert j >= 0 and b.count(key) == 1, f"vector {i}: epp slot not found"
    print(f"vector {i}: EPP {b[j + 20]:#04x} -> {NEW_EPP:#04x}"); b[j + 20] = NEW_EPP; vecs[i] = bytes(b)
data = plistlib.dumps(d, fmt=fmt)
info = {"CFBundleIdentifier": "org.acidanthera.driver.CPUFriendDataProvider", "CFBundleInfoDictionaryVersion": "6.0",
        "CFBundleName": "CPUFriendDataProvider", "CFBundlePackageType": "KEXT", "CFBundleShortVersionString": "1.0.1",
        "CFBundleVersion": "1.0.1", "OSBundleRequired": "Root",
        "NSHumanReadableCopyright": f"Copyright © 2017-2022 PMheart. Data: MacBookPro15,2 profile with EPP {NEW_EPP:#04x} (Yoga 920).",
        "IOKitPersonalities": {"CPUFriendDataProvider": {"CFBundleIdentifier": "com.apple.driver.AppleACPIPlatform",
            "IOClass": "AppleACPICPU", "IONameMatch": "processor", "IOProbeScore": 1100,
            "IOProviderClass": "IOACPIPlatformDevice", "cf-frequency-data": data}}}
k = os.path.join(OUT, "CPUFriendDataProvider.kext", "Contents"); os.makedirs(k, exist_ok=True)
plistlib.dump(info, open(os.path.join(k, "Info.plist"), "wb"), fmt=plistlib.FMT_XML)
print("wrote", os.path.join(OUT, "CPUFriendDataProvider.kext"))
