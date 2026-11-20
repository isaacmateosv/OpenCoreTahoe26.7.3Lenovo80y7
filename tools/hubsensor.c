// hubsensor: reads the Yoga 920 sensor hub's ambient light sensor and hinge sensor (read-only test tool).
//   hubsensor light [seconds]   illuminance in lux (report 2)
//   hubsensor hinge [seconds]   hinge angle in degrees + keyboard-base accelerometer (report 3)
// The sensor is powered (D0, asynchronous reports off) only while this runs and switched off (D4) afterwards, the
// same way autorotate handles the accelerometer (report 1). ITE hub selectors are 1-based (see autorotate.c).
// Build: clang -O2 -framework IOKit -framework CoreFoundation -o ~/.local/bin/hubsensor hubsensor.c
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <mach/mach_time.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static IOHIDDeviceRef hub;
static IOHIDElementRef pw, rp;
static volatile sig_atomic_t stop;
static void on_sig(int s) { (void)s; stop = 1; }

static void set_el(IOHIDElementRef e, CFIndex v) {
    IOHIDValueRef val = IOHIDValueCreateWithIntegerValue(NULL, e, mach_absolute_time(), v);
    IOHIDDeviceSetValue(hub, e, val);
    CFRelease(val);
}

int main(int argc, char **argv) {
    if (argc < 2 || (strcmp(argv[1], "light") && strcmp(argv[1], "hinge"))) {
        fprintf(stderr, "usage: hubsensor light|hinge [seconds]\n"); return 2;
    }
    int rid = strcmp(argv[1], "light") == 0 ? 2 : 3, secs = argc > 2 ? atoi(argv[2]) : 15;
    IOHIDManagerRef m = IOHIDManagerCreate(NULL, 0);
    int page = 0xFF83, usage = 0x80;
    CFNumberRef p = CFNumberCreate(NULL, kCFNumberIntType, &page), u = CFNumberCreate(NULL, kCFNumberIntType, &usage);
    const void *k[] = {CFSTR(kIOHIDPrimaryUsagePageKey), CFSTR(kIOHIDPrimaryUsageKey)}, *v[] = {p, u};
    CFDictionaryRef match = CFDictionaryCreate(NULL, k, v, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    IOHIDManagerSetDeviceMatching(m, match);
    IOHIDManagerOpen(m, 0);
    CFSetRef devs = IOHIDManagerCopyDevices(m);
    if (!devs || !CFSetGetCount(devs)) { fprintf(stderr, "sensor hub not found\n"); return 1; }
    CFSetGetValues(devs, (const void **)&hub);
    if (IOHIDDeviceOpen(hub, 0)) { fprintf(stderr, "cannot open the sensor hub\n"); return 1; }
    CFArrayRef els = IOHIDDeviceCopyMatchingElements(hub, NULL, 0);
    for (CFIndex i = 0; els && i < CFArrayGetCount(els); i++) {
        IOHIDElementRef e = (IOHIDElementRef)CFArrayGetValueAtIndex(els, i), par = IOHIDElementGetParent(e);
        if (!par || IOHIDElementGetReportID(e) != (uint32_t)rid || IOHIDElementGetType(e) != kIOHIDElementTypeFeature ||
            IOHIDElementGetUsagePage(e) != 0x20 || IOHIDElementGetUsage(e) != 0xffffffff) continue;
        if (IOHIDElementGetUsage(par) == 0x319) pw = e;
        if (IOHIDElementGetUsage(par) == 0x316) rp = e;
    }
    if (!pw || !rp) { fprintf(stderr, "sensor controls not found\n"); return 1; }
    signal(SIGINT, on_sig); signal(SIGTERM, on_sig);
    IOHIDValueRef cur = NULL;
    IOHIDDeviceGetValue(hub, pw, &cur);
    set_el(rp, 1); set_el(pw, 2);                      // No Events, D0 Full Power
    usleep(300000);
    printf("%s for %d s (Ctrl-C stops)\n", rid == 2 ? "ambient light" : "hinge", secs); fflush(stdout);
    for (int i = 0; i < secs * 2 && !stop; i++) {
        uint8_t b[64]; CFIndex len = sizeof b;
        if (IOHIDDeviceGetReport(hub, kIOHIDReportTypeInput, rid, b, &len) != kIOReturnSuccess) { printf("read failed\n"); }
        else if (rid == 2 && len >= 7) {
            int o = b[0] == 2;
            int32_t raw = (int32_t)(b[o+2] | b[o+3] << 8 | b[o+4] << 16 | (uint32_t)b[o+5] << 24);
            printf("%5.1fs  %8.2f lux\n", i / 2.0, raw / 100.0);   // unit exponent -2
        } else if (rid == 3 && len >= 14) {
            int o = b[0] == 3;
            int16_t c[6]; for (int j = 0; j < 6; j++) c[j] = (int16_t)(b[o+2+2*j] | b[o+3+2*j] << 8);
            printf("%5.1fs  hinge %6.1f deg (alt %6.1f)  c4 %5.1f  base accel x %+.2f y %+.2f z %+.2f g\n", i / 2.0,
                   c[4] / 10.0, c[5] / 10.0, c[3] / 10.0, c[0] / 1000.0, c[1] / 1000.0, c[2] / 1000.0);
        }
        fflush(stdout);
        usleep(500000);
    }
    // Leave the sensor on if an agent uses it (autobright: light, autorotate: report 1); otherwise power it off.
    int busy = rid == 2 && system("/bin/launchctl print gui/$(id -u)/org.yoga920.autobright >/dev/null 2>&1") == 0;
    if (!busy) {
        IOHIDDeviceGetValue(hub, pw, &cur);
        set_el(rp, 1); set_el(pw, 6);                  // D4 Power Off
    }
    IOHIDDeviceClose(hub, 0);
    return 0;
}
