#!/usr/bin/env python3 -I
"""Applies the Yoga 920 patch ("yoga1") to a pristine VoodooRMI 1.4.3 (83868f0) tree.
Every replacement must match exactly once, or the script aborts.
Usage: patch.py <src dir>"""
import sys, pathlib

src = pathlib.Path(sys.argv[1])

def sub(rel, old, new, count=1):
    p = src / rel
    s = p.read_text()
    n = s.count(old)
    if n != count:
        sys.exit(f"PATCH FAILED: {rel}: expected {count} match(es), found {n}:\n{old}")
    p.write_text(s.replace(old, new))
    print(f"patched {rel}")

# 1. Configurable palm Z limit (was the hard-coded RMI_2D_MAX_Z = 140)
sub("Configuration/RMIConfiguration.hpp",
    "    uint8_t palmRejectionMaxObjHeight {0xFF};\n",
    "    uint8_t palmRejectionMaxObjHeight {0xFF};\n"
    "    // Yoga 920 patch: contact strength (Z) above which a contact becomes a palm.\n"
    "    // Upstream hard-coded 140 (RMI_2D_MAX_Z), which also catches firmly placed thumbs.\n"
    "    uint8_t palmRejectionMaxZ {200};\n")

sub("RMIBus.cpp",
    '    update |= Configuration::loadUInt8Configuration(dictionary, "PalmRejectionMaxObjHeight", &conf.palmRejectionMaxObjHeight);\n',
    '    update |= Configuration::loadUInt8Configuration(dictionary, "PalmRejectionMaxObjHeight", &conf.palmRejectionMaxObjHeight);\n'
    '    update |= Configuration::loadUInt8Configuration(dictionary, "PalmRejectionMaxZ", &conf.palmRejectionMaxZ);\n')

# 2. Contact diagnostics ("Contact Stats" on F12/F11)
sub("Functions/Input/RMITrackpadFunction.hpp",
    "    bool isForceTouch(UInt8 pressure);\n};",
    "    bool isForceTouch(UInt8 pressure);\n"
    "\n"
    "    // Yoga 920 patch: contact diagnostics, published as \"Contact Stats\"\n"
    "    UInt8 slotMaxZ[MAX_FINGERS] {};\n"
    "    UInt32 statTouches {0};\n"
    "    UInt32 statPalmZ {0}, statPalmWX {0}, statPalmWY {0}, statPalmInaccurate {0}, statForceTouch {0};\n"
    "    UInt32 maxZHist[7] {};\n"
    "    UInt8 peakZ {0}, peakWX {0}, peakWY {0};\n"
    "    UInt8 maxFingersSeen {0};\n"
    "    UInt64 lastStatsPublish {0};\n"
    "    bool statsDirty {false};\n"
    "    char lastPalm[96] {};\n"
    "    void noteTouchEnd(int slot);\n"
    "    void publishContactStats(AbsoluteTime now);\n"
    "};")

T = "Functions/Input/RMITrackpadFunction.cpp"
sub(T,
    "        // Finger lifted, make finger valid\n"
    "        if (!isValidObj) {\n"
    "            fingerState[i] = RMI_FINGER_LIFTED;\n",
    "        // Finger lifted, make finger valid\n"
    "        if (!isValidObj) {\n"
    "            if (fingerState[i] != RMI_FINGER_LIFTED)\n"
    "                noteTouchEnd(i);\n"
    "            fingerState[i] = RMI_FINGER_LIFTED;\n")
sub(T,
    "        validFingerCount++;\n",
    "        validFingerCount++;\n"
    "        if (obj.z > slotMaxZ[i]) slotMaxZ[i] = obj.z;\n"
    "        if (obj.z > peakZ) peakZ = obj.z;\n"
    "        if (obj.wx > peakWX) peakWX = obj.wx;\n"
    "        if (obj.wy > peakWY) peakWY = obj.wy;\n")
sub(T,
    "            case RMI_FINGER_LIFTED:\n"
    "                fingerState[i] = RMI_FINGER_STARTED_IN_ZONE;\n",
    "            case RMI_FINGER_LIFTED:\n"
    "                statTouches++;\n"
    "                slotMaxZ[i] = obj.z;\n"
    "                statsDirty = true;\n"
    "                fingerState[i] = RMI_FINGER_STARTED_IN_ZONE;\n")
sub(T,
    "                if (obj.z > RMI_2D_MAX_Z ||\n"
    "                    obj.wx > conf.palmRejectionMaxObjWidth ||\n"
    "                    obj.wy > conf.palmRejectionMaxObjHeight ||\n"
    "                    obj.type == RMI_2D_OBJECT_INACCURATE) {\n",
    "                if (obj.z > conf.palmRejectionMaxZ ||\n"
    "                    obj.wx > conf.palmRejectionMaxObjWidth ||\n"
    "                    obj.wy > conf.palmRejectionMaxObjHeight ||\n"
    "                    obj.type == RMI_2D_OBJECT_INACCURATE) {\n"
    "                    if (obj.z > conf.palmRejectionMaxZ) statPalmZ++;\n"
    "                    else if (obj.wx > conf.palmRejectionMaxObjWidth) statPalmWX++;\n"
    "                    else if (obj.wy > conf.palmRejectionMaxObjHeight) statPalmWY++;\n"
    "                    else statPalmInaccurate++;\n"
    "                    snprintf(lastPalm, sizeof(lastPalm), \"slot %d z=%u wx=%u wy=%u type=%d contacts=%d\",\n"
    "                             i, obj.z, obj.wx, obj.wy, (int) obj.type, (int) maxIdx);\n"
    "                    statsDirty = true;\n")
sub(T,
    "                if (isForceTouch(obj.z) && fingerState[i] == RMI_FINGER_VALID) {\n"
    "                    fingerState[i] = RMI_FINGER_FORCE_TOUCH;\n",
    "                if (isForceTouch(obj.z) && fingerState[i] == RMI_FINGER_VALID) {\n"
    "                    statForceTouch++;\n"
    "                    fingerState[i] = RMI_FINGER_FORCE_TOUCH;\n")
sub(T,
    "    if (validFingerCount >= 4 && freeFingerTypes[kMT2FingerTypeThumb]) {\n",
    "    if (validFingerCount > maxFingersSeen)\n"
    "        maxFingersSeen = validFingerCount;\n"
    "\n"
    "    if (validFingerCount >= 4 && freeFingerTypes[kMT2FingerTypeThumb]) {\n")
sub(T,
    "    sendVoodooInputPacket(kIOMessageVoodooInputMessage, &inputEvent);\n",
    "    sendVoodooInputPacket(kIOMessageVoodooInputMessage, &inputEvent);\n"
    "    publishContactStats(report->timestamp);\n")

# helper implementations appended at the end of the file
p = src / T
p.write_text(p.read_text() + r'''

// ---------------------------------------------------------------------------
// Yoga 920 patch: contact diagnostics
// ---------------------------------------------------------------------------

void RMITrackpadFunction::noteTouchEnd(int slot) {
    if (slot < 0 || slot >= MAX_FINGERS)
        return;
    UInt8 z = slotMaxZ[slot];
    int bucket = z < 60 ? 0 : z < 100 ? 1 : z < 140 ? 2 : z < 170 ? 3 : z < 200 ? 4 : z < 230 ? 5 : 6;
    maxZHist[bucket]++;
    slotMaxZ[slot] = 0;
    statsDirty = true;
}

void RMITrackpadFunction::publishContactStats(AbsoluteTime now) {
    if (!statsDirty || (now - lastStatsPublish) < 1000000000ULL)   // at most once a second
        return;
    lastStatsPublish = now;
    statsDirty = false;

    OSDictionary *d = OSDictionary::withCapacity(16);
    if (!d)
        return;
#define PUT_NUM(k, v) do { OSNumber *n_ = OSNumber::withNumber((unsigned long long)(v), 32); if (n_) { d->setObject(k, n_); n_->release(); } } while (0)
    PUT_NUM("Touches", statTouches);
    PUT_NUM("Palm by Z", statPalmZ);
    PUT_NUM("Palm by width", statPalmWX);
    PUT_NUM("Palm by height", statPalmWY);
    PUT_NUM("Palm by inaccurate", statPalmInaccurate);
    PUT_NUM("Force touch", statForceTouch);
    PUT_NUM("Peak Z", peakZ);
    PUT_NUM("Peak WX", peakWX);
    PUT_NUM("Peak WY", peakWY);
    PUT_NUM("Max contacts", maxFingersSeen);
    PUT_NUM("Palm Z limit", getConfiguration().palmRejectionMaxZ);
#undef PUT_NUM
    char hist[160];
    snprintf(hist, sizeof(hist), "<60:%u 60-99:%u 100-139:%u 140-169:%u 170-199:%u 200-229:%u 230+:%u",
             maxZHist[0], maxZHist[1], maxZHist[2], maxZHist[3], maxZHist[4], maxZHist[5], maxZHist[6]);
    if (OSString *s = OSString::withCString(hist)) { d->setObject("Max Z per touch", s); s->release(); }
    if (lastPalm[0]) {
        if (OSString *s = OSString::withCString(lastPalm)) { d->setObject("Last palm", s); s->release(); }
    }
    setProperty("Contact Stats", d);
    d->release();
}
''')
print(f"patched {T} (helpers appended)")

# 3. F12 register descriptor parser: stop the out-of-bounds read behind the PGZ panic
F = "Functions/F12.cpp"
sub(F,
    "    UInt8 *struct_buf;\n"
    "    int reg;\n",
    "    UInt8 *struct_buf;\n"
    "    size_t struct_buf_size = 0;\n"
    "    int reg;\n")
sub(F,
    "    struct_buf = reinterpret_cast<UInt8 *>(IOMalloc(rdesc->struct_size));\n"
    "    if (!struct_buf)\n"
    "        return -ENOMEM;\n",
    "    // Yoga 920 patch: the parser below can step a few bytes past struct_size\n"
    "    // (e.g. a trailing 0x80 continuation bit). Linux's kmalloc slack hides that;\n"
    "    // macOS's guard allocator turns it into a \"Probabilistic GZAlloc\n"
    "    // out-of-bounds\" panic. Zero-pad the buffer and bound every loop.\n"
    "    struct_buf_size = (size_t) rdesc->struct_size + 16;\n"
    "    struct_buf = reinterpret_cast<UInt8 *>(IOMalloc(struct_buf_size));\n"
    "    if (!struct_buf)\n"
    "        return -ENOMEM;\n"
    "    memset(struct_buf, 0, struct_buf_size);\n")
sub(F,
    "    for (i = 0; i < rdesc->num_registers; i++) {\n"
    "        struct rmi_register_desc_item *item = &rdesc->registers[i];\n"
    "        int reg_size = struct_buf[offset];\n",
    "    for (i = 0; i < rdesc->num_registers; i++) {\n"
    "        struct rmi_register_desc_item *item = &rdesc->registers[i];\n"
    "        if (offset >= (int) rdesc->struct_size) {\n"
    "            IOLogError(\"F12 - register descriptor truncated at item %d of %d\", i, (int) rdesc->num_registers);\n"
    "            rdesc->num_registers = i;\n"
    "            break;\n"
    "        }\n"
    "        int reg_size = struct_buf[offset];\n")
sub(F,
    "        } while (struct_buf[offset++] & 0x80);\n",
    "        } while ((struct_buf[offset++] & 0x80) && offset < (int) rdesc->struct_size);\n")
sub(F,
    "    IOFree(struct_buf, rdesc->struct_size);\n",
    "    IOFree(struct_buf, struct_buf_size);\n")
print("all patches applied")
