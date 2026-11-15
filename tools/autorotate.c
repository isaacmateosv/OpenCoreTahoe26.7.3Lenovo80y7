// autorotate: tilt-driven screen rotation for the Yoga 920 Hackintosh, controlled by one switch.
//
//   autorotate toggle        turn auto-rotate on or off (prints "on" / "off"); this is what the Shortcut runs
//   autorotate on | off      explicit
//   autorotate status        prints "on" or "off"
//   autorotate run           the reader itself (launchd starts it; not meant to be run by hand)
//
// The reader polls the sensor hub's accelerometer (ITE8353 on I2C3, HID sensor page, input report 1) over I2C
// four times a second with GET_REPORT (sensor in D0, asynchronous reports off), so it needs no interrupts at all
// (VoodooGPIO would turn them into ~470/s). It rotates the built-in display only when the screen is tilted at
// least ~35 degrees from flat, the new angle is within +-30 degrees of an orientation, and that orientation has
// held for ~0.75 s; otherwise the current angle is kept. It never reads or rotates while the screen is off (lid
// shut, display sleep, dark wakes). It powers the sensor down before every sleep and re-arms it once the screen
// is back on after a wake, so dark wakes never touch it. "off" stops the reader, powers the sensor down (nothing
// is read) and returns the display to 0 degrees. After a reboot it starts off.
// Events (on/off, sleep, wake, each rotation) go to ~/.local/share/autorotate/autorotate.log.
//
// Build: clang -O2 -framework IOKit -framework CoreFoundation -framework ApplicationServices -o autorotate autorotate.c
#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOMessage.h>
#include <IOKit/graphics/IOGraphicsLib.h>
#include <IOKit/graphics/IOGraphicsTypes.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <mach/mach_time.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define LABEL "org.yoga920.autorotate"
#define kIOFBSetTransform 0x00000400

// ---------- display rotation (same method as tools/rotate.c) ----------
static io_service_t builtin_framebuffer(void) {
    io_iterator_t it;
    io_service_t fb, found = IO_OBJECT_NULL;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("IOFramebuffer"), &it) != KERN_SUCCESS)
        return IO_OBJECT_NULL;
    while ((fb = IOIteratorNext(it))) {
        int builtin = 0;
        io_iterator_t kids;
        if (!found && IORegistryEntryCreateIterator(fb, kIOServicePlane, kIORegistryIterateRecursively, &kids) == KERN_SUCCESS) {
            io_registry_entry_t k;
            while (!builtin && (k = IOIteratorNext(kids))) {
                builtin = IOObjectConformsTo(k, "AppleBacklightDisplay");
                IOObjectRelease(k);
            }
            IOObjectRelease(kids);
        }
        if (builtin) found = fb;
        else IOObjectRelease(fb);
    }
    IOObjectRelease(it);
    return found;
}

static CGDirectDisplayID builtin_display(void) {
    CGDirectDisplayID ids[16];
    uint32_t n = 0;
    if (CGGetOnlineDisplayList(16, ids, &n) != kCGErrorSuccess) return 0;
    for (uint32_t i = 0; i < n; i++)
        if (CGDisplayIsBuiltin(ids[i])) return ids[i];
    return 0;
}

static int current_degrees(void) {
    CGDirectDisplayID d = builtin_display();
    return d ? ((int)(CGDisplayRotation(d) + 0.5)) % 360 : -1;
}

static void logmsg(const char *fmt, int v) {   // one line per event in ~/.local/share/autorotate/autorotate.log
    time_t now = time(NULL);
    char ts[32];
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", localtime(&now));
    fprintf(stderr, "%s ", ts);
    fprintf(stderr, fmt, v);
    fprintf(stderr, "\n");
}

static int set_degrees(int deg) {
    IOOptionBits bits = deg == 90 ? kIOScaleRotate90 : deg == 180 ? kIOScaleRotate180 : deg == 270 ? kIOScaleRotate270 : kIOScaleRotate0;
    if (current_degrees() == deg) return 0;
    io_service_t fb = builtin_framebuffer();
    if (!fb) return -1;
    kern_return_t kr = IOServiceRequestProbe(fb, kIOFBSetTransform | (bits << 16));
    IOObjectRelease(fb);
    for (int i = 0; i < 40 && current_degrees() != deg; i++) usleep(50000);
    return kr == KERN_SUCCESS ? 0 : -1;
}

// ---------- sensor hub (HID) ----------
static IOHIDManagerRef manager;
static IOHIDDeviceRef hub;
static IOHIDElementRef power_el, report_el;   // accelerometer Power State / Reporting State (feature, report 1)

static int open_hub(void) {
    if (hub) return 0;
    if (!manager) {
        manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
        int page = 0xFF83, usage = 0x80;   // the hub's top-level collection
        CFNumberRef p = CFNumberCreate(NULL, kCFNumberIntType, &page), u = CFNumberCreate(NULL, kCFNumberIntType, &usage);
        const void *k[] = {CFSTR(kIOHIDPrimaryUsagePageKey), CFSTR(kIOHIDPrimaryUsageKey)}, *v[] = {p, u};
        CFDictionaryRef m = CFDictionaryCreate(NULL, k, v, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        IOHIDManagerSetDeviceMatching(manager, m);
        CFRelease(m); CFRelease(p); CFRelease(u);
        IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone);
    }
    CFSetRef devs = IOHIDManagerCopyDevices(manager);
    if (!devs || !CFSetGetCount(devs)) { if (devs) CFRelease(devs); return -1; }
    IOHIDDeviceRef d;
    CFSetGetValues(devs, (const void **)&d);
    CFRetain(d);
    CFRelease(devs);
    if (IOHIDDeviceOpen(d, kIOHIDOptionsTypeNone) != kIOReturnSuccess) { CFRelease(d); return -1; }
    CFArrayRef els = IOHIDDeviceCopyMatchingElements(d, NULL, kIOHIDOptionsTypeNone);
    power_el = report_el = NULL;
    for (CFIndex i = 0; els && i < CFArrayGetCount(els); i++) {
        IOHIDElementRef e = (IOHIDElementRef)CFArrayGetValueAtIndex(els, i);
        IOHIDElementRef par = IOHIDElementGetParent(e);
        if (!par || IOHIDElementGetReportID(e) != 1 || IOHIDElementGetType(e) != kIOHIDElementTypeFeature ||
            IOHIDElementGetUsagePage(e) != 0x20 || IOHIDElementGetUsage(e) != 0xffffffff) continue;
        if (IOHIDElementGetUsage(par) == 0x319) power_el = (IOHIDElementRef)CFRetain(e);
        if (IOHIDElementGetUsage(par) == 0x316) report_el = (IOHIDElementRef)CFRetain(e);
    }
    if (els) CFRelease(els);
    if (!power_el || !report_el) { IOHIDDeviceClose(d, kIOHIDOptionsTypeNone); CFRelease(d); return -1; }
    hub = d;
    return 0;
}

static void close_hub(void) {
    if (!hub) return;
    IOHIDDeviceClose(hub, kIOHIDOptionsTypeNone);
    CFRelease(hub);
    hub = NULL;
}

static void set_el(IOHIDElementRef e, CFIndex v) {
    IOHIDValueRef val = IOHIDValueCreateWithIntegerValue(NULL, e, mach_absolute_time(), v);
    IOHIDDeviceSetValue(hub, e, val);
    CFRelease(val);
}

// This ITE hub counts its named-array selectors from 1, not 0, despite its descriptor's Logical Minimum 0
// (Linux handles the same Lenovo Yoga hubs with HID_SENSOR_HUB_ENUM_QUIRK). So:
//   Reporting State: 1 = No Events, 2 = All Events, 3 = Threshold Events
//   Power State:     1 = Undefined, 2 = D0 Full Power, 6 = D4 Power Off
// Async reports stay off: VoodooGPIO turns each report into ~10 interrupts (~470/s at the hub's 50 Hz), and the
// reader gets live data by polling GET_REPORT instead, with D0 + No Events and no interrupts at all.
#define REPORTING_NONE 1
#define POWER_D0 2
#define POWER_D4 6

static int armed;   // this process has powered the sensor on (D0)

static int sensor_on(void) {
    if (open_hub() != 0) return -1;
    IOHIDValueRef cur = NULL;
    IOHIDDeviceGetValue(hub, power_el, &cur);   // fetch feature report 1 so its other fields keep their values
    set_el(report_el, REPORTING_NONE);
    set_el(power_el, POWER_D0);
    armed = 1;
    return 0;
}

static void sensor_off(void) {
    armed = 0;
    if (open_hub() != 0) return;
    IOHIDValueRef cur = NULL;
    IOHIDDeviceGetValue(hub, power_el, &cur);
    set_el(report_el, REPORTING_NONE);
    set_el(power_el, POWER_D4);
}

static int read_xyz(int *x, int *y, int *z) {
    if (!hub) return -1;
    uint8_t b[64];
    CFIndex len = sizeof b;
    if (IOHIDDeviceGetReport(hub, kIOHIDReportTypeInput, 1, b, &len) != kIOReturnSuccess || len < 9) return -1;
    int o = b[0] == 1 ? 1 : 0;   // report ID byte, then sensor state, sensor event, X, Y, Z (int16 LE)
    *x = (int16_t)(b[o + 2] | b[o + 3] << 8);
    *y = (int16_t)(b[o + 4] | b[o + 5] << 8);
    *z = (int16_t)(b[o + 6] | b[o + 7] << 8);
    return 0;
}

// Orientation from gravity in the screen plane: Y along the screen's height (negative when upright), X across it.
// Same sign convention as VoodooI2CHID's accelerometer driver (x>0 -> 90, y>0 -> 180, x<0 -> 270).
static int classify(int x, int y, int z) {
    double m2 = (double)x * x + (double)y * y, g2 = m2 + (double)z * z;
    if (g2 < 300.0 * 300.0) return -1;              // no sensible reading
    if (m2 < 0.33 * g2) return -1;                  // within ~35 degrees of flat: keep the current angle
    double a = atan2((double)x, (double)-y) * 180.0 / M_PI;   // 0 = upright, +90 = right side down
    const int centers[] = {0, 90, 180, -90, -180}, degs[] = {0, 90, 180, 270, 180};
    for (int i = 0; i < 5; i++)
        if (fabs(a - centers[i]) <= 30.0) return degs[i];
    return -1;                                      // between two orientations: keep the current angle
}

// ---------- the reader ----------
static volatile sig_atomic_t stop_requested;
static io_connect_t pm_root;
static int candidate = -1, streak, failures, rearm_after_ms, rearm_pending, sleeping;

static void on_signal(int s) { (void)s; stop_requested = 1; }

static void power_cb(void *ref, io_service_t s, natural_t type, void *arg) {
    (void)ref; (void)s;
    switch (type) {
    case kIOMessageCanSystemSleep:
        IOAllowPowerChange(pm_root, (long)arg);
        break;
    case kIOMessageSystemWillSleep:
        sleeping = 1;        // nothing touches the sensor again until the next wake (the system may take
        rearm_pending = 0;   // another ~15 s to reach S3, and a pending re-arm must not fire in that window)
        if (armed) { sensor_off(); logmsg("sleep: sensor off%.0d", 0); }
        IOAllowPowerChange(pm_root, (long)arg);
        break;
    case kIOMessageSystemHasPoweredOn:   // every wake; also sent when a sleep is cancelled after WillSleep
        sleeping = 0;
        rearm_pending = 1;   // the hub resets in S3: power the sensor back on once the screen is on
        rearm_after_ms = 1500;
        candidate = -1; streak = 0;
        break;
    }
}

static void tick(CFRunLoopTimerRef t, void *info) {
    (void)t; (void)info;
    if (stop_requested) {
        sensor_off();
        set_degrees(0);
        close_hub();
        logmsg("off: reader stopped, display back to 0%.0d", 0);
        exit(0);
    }
    if (sleeping) return;
    CGDirectDisplayID bd = builtin_display();
    int screen_on = bd && !CGDisplayIsAsleep(bd);
    if (rearm_pending) {
        if (rearm_after_ms > 0) rearm_after_ms -= 250;
        if (rearm_after_ms > 0 || !screen_on) return;   // a dark wake keeps the screen off: leave the sensor off
        rearm_pending = 0;
        close_hub();
        logmsg(sensor_on() == 0 ? "wake: sensor re-armed%.0d" : "wake: sensor hub not found yet%.0d", 0);
        return;
    }
    if (!screen_on) { streak = 0; return; }   // lid shut or display asleep: never rotate
    int x, y, z;
    if (read_xyz(&x, &y, &z) != 0) {
        if (++failures % 8 == 0) { close_hub(); sensor_on(); }   // hub gone or reset: reopen every ~2 s
        return;
    }
    failures = 0;
    int c = classify(x, y, z);
    if (c < 0) { streak = 0; return; }
    if (c != candidate) { candidate = c; streak = 1; return; }
    if (++streak >= 3 && c != current_degrees() && set_degrees(c) == 0) logmsg("rotated to %d", c);
}

static int run_reader(void) {
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    for (int i = 0; i < 20 && sensor_on() != 0; i++) sleep(1);
    logmsg(armed ? "on: reader started%.0d" : "on: reader started, sensor hub not found yet%.0d", 0);
    IONotificationPortRef port;
    io_object_t notifier;
    pm_root = IORegisterForSystemPower(NULL, &port, power_cb, &notifier);
    if (pm_root) CFRunLoopAddSource(CFRunLoopGetCurrent(), IONotificationPortGetRunLoopSource(port), kCFRunLoopDefaultMode);
    CFRunLoopTimerRef timer = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + 0.25, 0.25, 0, 0, tick, NULL);
    CFRunLoopAddTimer(CFRunLoopGetCurrent(), timer, kCFRunLoopDefaultMode);
    CFRunLoopRun();
    return 0;
}

// ---------- switch (launchd job) ----------
static void paths(char *plist, size_t n, char *self, size_t m) {
    const char *home = getenv("HOME");
    snprintf(plist, n, "%s/.local/share/autorotate/" LABEL ".plist", home ? home : "");
    snprintf(self, m, "%s/.local/bin/autorotate", home ? home : "");
}

static int job_loaded(void) {
    char cmd[256];
    snprintf(cmd, sizeof cmd, "/bin/launchctl print gui/%d/" LABEL " >/dev/null 2>&1", getuid());
    return system(cmd) == 0;
}

static int turn_on(void) {
    char plist[1024], self[1024], cmd[2300];
    paths(plist, sizeof plist, self, sizeof self);
    char dir[1100];
    snprintf(dir, sizeof dir, "%s/.local/share/autorotate", getenv("HOME"));
    snprintf(cmd, sizeof cmd, "/bin/mkdir -p '%s'", dir);
    system(cmd);
    FILE *f = fopen(plist, "w");
    if (!f) { fprintf(stderr, "autorotate: cannot write %s\n", plist); return 1; }
    fprintf(f,
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\"><dict>\n"
        "  <key>Label</key><string>" LABEL "</string>\n"
        "  <key>ProgramArguments</key><array><string>%s</string><string>run</string></array>\n"
        "  <key>RunAtLoad</key><true/>\n"
        "  <key>KeepAlive</key><dict><key>SuccessfulExit</key><false/></dict>\n"
        "  <key>ProcessType</key><string>Interactive</string>\n"
        "  <key>StandardErrorPath</key><string>%s/autorotate.log</string>\n"
        "</dict></plist>\n", self, dir);
    fclose(f);
    if (job_loaded()) { printf("on\n"); return 0; }
    snprintf(cmd, sizeof cmd, "/bin/launchctl bootstrap gui/%d '%s'", getuid(), plist);
    int rc = system(cmd);
    printf(rc == 0 ? "on\n" : "failed\n");
    return rc == 0 ? 0 : 1;
}

static int turn_off(void) {
    char cmd[256];
    if (job_loaded()) {
        snprintf(cmd, sizeof cmd, "/bin/launchctl bootout gui/%d/" LABEL " 2>/dev/null", getuid());
        system(cmd);   // the reader powers the sensor off and returns to 0 degrees on SIGTERM
    }
    sensor_off();      // belt and braces if the reader was not running
    close_hub();
    set_degrees(0);
    printf("off\n");
    return 0;
}

int main(int argc, char **argv) {
    const char *cmd = argc > 1 ? argv[1] : "status";
    if (!strcmp(cmd, "run")) return run_reader();
    if (!strcmp(cmd, "on")) return turn_on();
    if (!strcmp(cmd, "off")) return turn_off();
    if (!strcmp(cmd, "toggle")) return job_loaded() ? turn_off() : turn_on();
    if (!strcmp(cmd, "status")) { printf(job_loaded() ? "on\n" : "off\n"); return 0; }
    if (!strcmp(cmd, "read")) {   // diagnostic: one reading and what it would choose
        if (sensor_on() != 0) { fprintf(stderr, "sensor hub not found\n"); return 1; }
        usleep(300000);
        int x, y, z;
        if (read_xyz(&x, &y, &z) != 0) { fprintf(stderr, "read failed\n"); return 1; }
        printf("x=%d y=%d z=%d -> %d (display at %d)\n", x, y, z, classify(x, y, z), current_degrees());
        if (!job_loaded()) sensor_off();
        return 0;
    }
    fprintf(stderr, "usage: autorotate toggle | on | off | status | read\n");
    return 2;
}
