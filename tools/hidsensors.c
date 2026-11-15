// hidsensors: lists the HID sensor-page (0x20) elements of the Yoga 920 sensor hub (ITE8353 on I2C3)
// so the auto-rotate reader knows which controls exist. With no option it only lists them; "-read" also fetches
// the current feature values (GET_REPORT; harmless). "-test" turns the accelerometer's own reports on for 4 s
// (expect ~470 interrupts/s from VoodooGPIO meanwhile); "-poll" polls input report 1 the way autorotate does.
// This ITE hub numbers its named-array selectors from 1 (Linux: HID_SENSOR_HUB_ENUM_QUIRK):
//   Reporting State 1 = No Events, 2 = All Events; Power State 2 = D0 Full Power, 6 = D4 Power Off.
// Build: clang -O2 -framework IOKit -framework CoreFoundation -o hidsensors hidsensors.c
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int num(CFTypeRef t) { int v = 0; if (t && CFGetTypeID(t) == CFNumberGetTypeID()) CFNumberGetValue(t, kCFNumberIntType, &v); return v; }

int main(int argc, char **argv) {
    int doread = argc > 1 && !strcmp(argv[1], "-read");
    IOHIDManagerRef m = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    int page = 0xFF83, usage = 0x80;   // the sensor hub's top-level (vendor) collection
    CFNumberRef p = CFNumberCreate(NULL, kCFNumberIntType, &page), u = CFNumberCreate(NULL, kCFNumberIntType, &usage);
    const void *k[] = {CFSTR(kIOHIDPrimaryUsagePageKey), CFSTR(kIOHIDPrimaryUsageKey)}, *v[] = {p, u};
    CFDictionaryRef match = CFDictionaryCreate(NULL, k, v, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    IOHIDManagerSetDeviceMatching(m, match);
    IOHIDManagerOpen(m, kIOHIDOptionsTypeNone);
    CFSetRef devs = IOHIDManagerCopyDevices(m);
    if (!devs || !CFSetGetCount(devs)) { fprintf(stderr, "sensor hub HID device not found\n"); return 1; }
    IOHIDDeviceRef d; CFSetGetValues(devs, (const void **)&d);
    IOReturn r = IOHIDDeviceOpen(d, kIOHIDOptionsTypeNone);
    printf("open: 0x%x  product=%s\n", r, "SHUB");
    CFArrayRef els = IOHIDDeviceCopyMatchingElements(d, NULL, kIOHIDOptionsTypeNone);
    const char *tn[] = {"?", "in-misc", "in-button", "in-axis", "in-scancodes", "?", "?", "?", "?", "output", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "feature", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?"};
    for (CFIndex i = 0; i < CFArrayGetCount(els); i++) {
        IOHIDElementRef e = (IOHIDElementRef)CFArrayGetValueAtIndex(els, i);
        IOHIDElementType t = IOHIDElementGetType(e);
        uint32_t up = IOHIDElementGetUsagePage(e), us = IOHIDElementGetUsage(e);
        if (t == kIOHIDElementTypeCollection) {
            printf("collection page 0x%x usage 0x%x\n", up, us);
            continue;
        }
        if (up != 0x20) continue;
        const char *ts = t == kIOHIDElementTypeFeature ? "feature" : t == kIOHIDElementTypeOutput ? "output" :
                         t == kIOHIDElementTypeInput_Misc ? "in-misc" : t == kIOHIDElementTypeInput_Axis ? "in-axis" :
                         t == kIOHIDElementTypeInput_Button ? "in-button" : (t < (int)(sizeof tn / sizeof *tn) ? tn[t] : "?");
        IOHIDElementRef parent = IOHIDElementGetParent(e);
        printf("  %-8s cookie %4u rid %3u usage 0x%04x size %2u x%u log %ld..%ld%s parent-usage 0x%x",
               ts, (unsigned)IOHIDElementGetCookie(e), IOHIDElementGetReportID(e), us,
               IOHIDElementGetReportSize(e), IOHIDElementGetReportCount(e),
               (long)IOHIDElementGetLogicalMin(e), (long)IOHIDElementGetLogicalMax(e),
               IOHIDElementIsArray(e) ? " ARRAY" : "", parent ? IOHIDElementGetUsage(parent) : 0);
        if (doread && t == kIOHIDElementTypeFeature) {
            IOHIDValueRef val = NULL;
            if (IOHIDDeviceGetValue(d, e, &val) == kIOReturnSuccess && val) printf("  = %ld", (long)IOHIDValueGetIntegerValue(val));
        }
        printf("\n");
    }
    if (argc > 1 && !strcmp(argv[1], "-test")) {
        // Find the accelerometer's controls by usage inside report ID 1.
        IOHIDElementRef power = NULL, report = NULL, ax[3] = {NULL, NULL, NULL};
        for (CFIndex i = 0; i < CFArrayGetCount(els); i++) {
            IOHIDElementRef e = (IOHIDElementRef)CFArrayGetValueAtIndex(els, i);
            IOHIDElementRef par = IOHIDElementGetParent(e);
            uint32_t us = IOHIDElementGetUsage(e), pu = par ? IOHIDElementGetUsage(par) : 0;
            if (IOHIDElementGetReportID(e) != 1 || IOHIDElementGetUsagePage(e) != 0x20) continue;
            if (IOHIDElementGetType(e) == kIOHIDElementTypeFeature && us == 0xffffffff && pu == 0x319) power = e;
            if (IOHIDElementGetType(e) == kIOHIDElementTypeFeature && us == 0xffffffff && pu == 0x316) report = e;
            if (IOHIDElementGetType(e) != kIOHIDElementTypeFeature && us >= 0x453 && us <= 0x455) ax[us - 0x453] = e;
        }
        if (!power || !report || !ax[0]) { fprintf(stderr, "controls not found\n"); return 1; }
        IOHIDValueRef cur = NULL;
        IOHIDDeviceGetValue(d, power, &cur);   // fetch feature report 1 so the other fields keep their values
        printf("before: power=%ld", cur ? (long)IOHIDValueGetIntegerValue(cur) : -1);
        IOHIDDeviceGetValue(d, report, &cur);
        printf(" reporting=%ld\n", cur ? (long)IOHIDValueGetIntegerValue(cur) : -1);
        IOHIDValueRef v1 = IOHIDValueCreateWithIntegerValue(NULL, power, mach_absolute_time(), 2);   // D0 full power
        IOHIDValueRef v2 = IOHIDValueCreateWithIntegerValue(NULL, report, mach_absolute_time(), 2);  // all events
        printf("set power D0: 0x%x, reporting all: 0x%x\n", IOHIDDeviceSetValue(d, power, v1), IOHIDDeviceSetValue(d, report, v2));
        IOHIDDeviceScheduleWithRunLoop(d, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
        for (int s = 0; s < 8; s++) {
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.5, false);
            long xyz[3] = {0, 0, 0};
            for (int a = 0; a < 3; a++) {
                IOHIDValueRef val = NULL;
                if (ax[a] && IOHIDDeviceGetValue(d, ax[a], &val) == kIOReturnSuccess && val) xyz[a] = IOHIDValueGetIntegerValue(val);
            }
            printf("t=%.1fs  x=%6ld y=%6ld z=%6ld\n", (s + 1) * 0.5, xyz[0], xyz[1], xyz[2]);
        }
        IOHIDValueRef o1 = IOHIDValueCreateWithIntegerValue(NULL, report, mach_absolute_time(), 1);  // no events
        IOHIDValueRef o2 = IOHIDValueCreateWithIntegerValue(NULL, power, mach_absolute_time(), 6);   // D4 power off
        printf("set reporting none: 0x%x, power D4: 0x%x\n", IOHIDDeviceSetValue(d, report, o1), IOHIDDeviceSetValue(d, power, o2));
    }
    if (argc > 1 && !strcmp(argv[1], "-poll")) {
        // Power the accelerometer on, keep asynchronous reports off, and poll input report 1 directly.
        IOHIDElementRef power = NULL, report = NULL;
        for (CFIndex i = 0; i < CFArrayGetCount(els); i++) {
            IOHIDElementRef e = (IOHIDElementRef)CFArrayGetValueAtIndex(els, i);
            IOHIDElementRef par = IOHIDElementGetParent(e);
            if (IOHIDElementGetReportID(e) != 1 || IOHIDElementGetType(e) != kIOHIDElementTypeFeature ||
                IOHIDElementGetUsage(e) != 0xffffffff || !par) continue;
            if (IOHIDElementGetUsage(par) == 0x319) power = e;
            if (IOHIDElementGetUsage(par) == 0x316) report = e;
        }
        IOHIDValueRef cur = NULL;
        IOHIDDeviceGetValue(d, power, &cur);
        IOHIDDeviceSetValue(d, power, IOHIDValueCreateWithIntegerValue(NULL, power, mach_absolute_time(), 2));   // D0
        IOHIDDeviceSetValue(d, report, IOHIDValueCreateWithIntegerValue(NULL, report, mach_absolute_time(), 1)); // no async events
        usleep(200000);
        for (int s = 0; s < 10; s++) {
            uint8_t buf[64]; CFIndex len = sizeof buf;
            IOReturn gr = IOHIDDeviceGetReport(d, kIOHIDReportTypeInput, 1, buf, &len);
            printf("poll %d: 0x%x len %ld  bytes:", s, gr, (long)len);
            for (CFIndex b = 0; b < len && b < 12; b++) printf(" %02x", buf[b]);
            if (gr == kIOReturnSuccess && len >= 9) {
                int off = (buf[0] == 1) ? 1 : 0;   // report ID byte present?
                int16_t x = (int16_t)(buf[off + 2] | buf[off + 3] << 8), y = (int16_t)(buf[off + 4] | buf[off + 5] << 8),
                        z = (int16_t)(buf[off + 6] | buf[off + 7] << 8);
                printf("   x=%d y=%d z=%d", x, y, z);
            }
            printf("\n");
            usleep(300000);
        }
        IOHIDDeviceSetValue(d, power, IOHIDValueCreateWithIntegerValue(NULL, power, mach_absolute_time(), 6));   // D4 off
    }
    (void)num;
    return 0;
}
