// autobright: automatic display brightness for the Yoga 920 Hackintosh from its real ambient light sensor,
// switched by macOS's own System Settings > Displays > "Automatically adjust brightness".
//
//   autobright on | off      install / remove the login agent (once; afterwards use the System Settings switch)
//   autobright status        is the agent installed and running ("on" / "off")
//   autobright read          diagnostic: lux, curve value, brightness, state of the macOS switch
//   autobright run           the agent itself (launchd starts it)
//
// Why an agent: macOS only learns light levels from an SMC light sensor; here that is the fake ALS0 device
// (constant), while the real sensor sits in the ITE sensor hub on I2C (HID sensor page, input report 2,
// illuminance x 10^-2 lux). With the switch on, macOS has nothing real to adapt to and simply holds your last
// level, undoing plain DisplayServicesSetBrightness changes within a second. The agent therefore changes the
// brightness with DisplayServicesSetBrightnessSmooth(display, delta), the relative call the brightness keys use,
// which macOS accepts as a user adjustment. With the switch off the agent idles and powers its sensor off.
//
// Behaviour: brightness follows a log-lux curve (dark ~20-30 %, dim room 35-50 %, office 60-80 %, daylight 90-100
// %) plus your personal offset. Switching on keeps the current level. When you change the brightness yourself
// (keys, Control Centre), that level is kept until the light changes by 2x, and becomes your offset; you may go
// below the automatic floor (12 %). Changes fade in 0.6 % steps ten times a second (about 6 %/s). The sensor is
// powered (D0, no asynchronous reports, polled once a second, no interrupts) only while active and the screen is
// on; it is powered off before sleep and re-armed once the screen is back on (dark wakes untouched).
// Log: ~/.local/share/autobright/autobright.log (touch ~/.local/share/autobright/debug for a per-second trace).
//
// Build: clang -O2 -framework IOKit -framework CoreFoundation -framework ApplicationServices -lobjc \
//        -o ~/.local/bin/autobright autobright.c
#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOMessage.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <dlfcn.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <mach/mach_time.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define LABEL "org.yoga920.autobright"
#define RID 2                    // ambient light sensor report ID on this hub
#define TICK 0.1                 // seconds per timer tick (ramp steps)
#define READ_EVERY 10            // ticks between sensor reads (1 s)

static void logmsg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void logmsg(const char *fmt, ...) {   // stderr = ~/.local/share/autobright/autobright.log under launchd
    char ts[32];
    time_t now = time(NULL);
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", localtime(&now));
    fprintf(stderr, "%s ", ts);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}

// ---------- display brightness (DisplayServices, the API Control Centre uses) ----------
static int (*ds_get)(CGDirectDisplayID, float *);
static int (*ds_set)(CGDirectDisplayID, float);
static int (*ds_smooth)(CGDirectDisplayID, float);   // relative change, animated by macOS (brightness keys)

static CGDirectDisplayID builtin_display(void) {
    CGDirectDisplayID ids[8];
    uint32_t n = 0;
    if (CGGetOnlineDisplayList(8, ids, &n) != kCGErrorSuccess) return 0;
    for (uint32_t i = 0; i < n; i++)
        if (CGDisplayIsBuiltin(ids[i])) return ids[i];
    return 0;
}

static int load_ds(void) {
    void *h = dlopen("/System/Library/PrivateFrameworks/DisplayServices.framework/DisplayServices", RTLD_NOW);
    if (!h) return -1;
    ds_get = (int (*)(CGDirectDisplayID, float *))dlsym(h, "DisplayServicesGetBrightness");
    ds_set = (int (*)(CGDirectDisplayID, float))dlsym(h, "DisplayServicesSetBrightness");
    ds_smooth = (int (*)(CGDirectDisplayID, float))dlsym(h, "DisplayServicesSetBrightnessSmooth");
    return ds_get && ds_set ? 0 : -1;
}

static int get_brightness(CGDirectDisplayID d, float *b) { return d && ds_get(d, b) == 0 ? 0 : -1; }
static int set_brightness(CGDirectDisplayID d, float b) { return d && ds_set(d, b) == 0 ? 0 : -1; }

// ---------- sensor hub (same access pattern as autorotate.c; ITE selectors are 1-based) ----------
#define REPORTING_NONE 1
#define POWER_D0 2
#define POWER_D4 6
static IOHIDManagerRef manager;
static IOHIDDeviceRef hub;
static IOHIDElementRef power_el, report_el;
static int armed;

static int open_hub(void) {
    if (hub) return 0;
    if (!manager) {
        manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
        int page = 0xFF83, usage = 0x80;
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
        if (!par || IOHIDElementGetReportID(e) != RID || IOHIDElementGetType(e) != kIOHIDElementTypeFeature ||
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

static int sensor_on(void) {
    if (open_hub() != 0) return -1;
    IOHIDValueRef cur = NULL;
    IOHIDDeviceGetValue(hub, power_el, &cur);   // fetch feature report 2 so its other fields keep their values
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

static int read_lux(double *lux) {
    if (!hub) return -1;
    uint8_t b[64];
    CFIndex len = sizeof b;
    if (IOHIDDeviceGetReport(hub, kIOHIDReportTypeInput, RID, b, &len) != kIOReturnSuccess || len < 7) return -1;
    int o = b[0] == RID ? 1 : 0;   // report ID, sensor state, sensor event, illuminance (int32 LE, x 10^-2 lux)
    int32_t raw = (int32_t)((uint32_t)b[o + 2] | (uint32_t)b[o + 3] << 8 | (uint32_t)b[o + 4] << 16 | (uint32_t)b[o + 5] << 24);
    if (raw < 0) return -1;
    *lux = raw / 100.0;
    return 0;
}

// ---------- curve: brightness for a light level (log-lux interpolation), before the personal offset ----------
// Targets roughly what Apple ships: dark room ~20-30 %, dim room 35-50 %, office 60-80 %, daylight 90-100 %.
static const double curve_lux[] = {0, 2, 10, 50, 150, 400, 1000, 2500};
static const double curve_bri[] = {0.20, 0.25, 0.35, 0.48, 0.62, 0.76, 0.90, 1.00};

static double curve(double lux) {
    double x = log10(lux + 1);
    int n = sizeof curve_lux / sizeof curve_lux[0];
    if (lux <= curve_lux[0]) return curve_bri[0];
    for (int i = 1; i < n; i++) {
        double x0 = log10(curve_lux[i - 1] + 1), x1 = log10(curve_lux[i] + 1);
        if (x <= x1) return curve_bri[i - 1] + (curve_bri[i] - curve_bri[i - 1]) * (x - x0) / (x1 - x0);
    }
    return curve_bri[n - 1];
}

static double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

// ---------- macOS switch: System Settings > Displays > "Automatically adjust brightness" ----------
// CoreBrightness keeps it as the property DisplayBrightnessAuto. With it on, macOS (which has no real sensor here)
// holds the brightness at your last level and undoes plain DisplayServicesSetBrightness changes, but it accepts
// DisplayServicesSetBrightnessSmooth(display, delta) - the call the brightness keys use - as a user adjustment.
static id cb_client;

static int macos_auto_on(void) {
    if (!cb_client) {
        dlopen("/System/Library/PrivateFrameworks/CoreBrightness.framework/CoreBrightness", RTLD_NOW);
        Class c = objc_getClass("BrightnessSystemClient");
        if (!c) return 0;
        id o = ((id (*)(id, SEL))objc_msgSend)((id)c, sel_registerName("alloc"));
        cb_client = ((id (*)(id, SEL))objc_msgSend)(o, sel_registerName("init"));
        if (!cb_client) return 0;
    }
    CFTypeRef v = ((CFTypeRef (*)(id, SEL, CFStringRef))objc_msgSend)(cb_client, sel_registerName("copyPropertyForKey:"),
                                                                       CFSTR("DisplayBrightnessAuto"));
    int on = 0;
    if (v && CFGetTypeID(v) == CFBooleanGetTypeID()) on = CFBooleanGetValue((CFBooleanRef)v);
    else if (v && CFGetTypeID(v) == CFNumberGetTypeID()) { int i = 0; CFNumberGetValue((CFNumberRef)v, kCFNumberIntType, &i); on = i != 0; }
    if (v) CFRelease(v);
    return on;
}

// ---------- agent ----------
#define MIN_BRIGHT 0.12          // automatic changes never go darker than this (you can, and then it is the floor)
#define HOLD_LOG 0.30            // after you set a level it is kept until the light changes 2x (log10 2 = 0.30)
#define START_DELTA 0.04         // adjust only when the target is this far away (no hunting)
#define STEP 0.006               // per tick (0.1 s) = 6 %/s: small steps, so the fade looks continuous
#define USER_DELTA 0.03          // a level this far from what we asked for was set by the user
#define SETTLE_TICKS 15          // ...once our own last change is 1.5 s old (macOS animates it)
#define USER_DELTA_RAMP 0.09     // while we are fading: more than our animation lag (one key press = 6.25 %)

static volatile sig_atomic_t stop_requested;
static io_connect_t pm_root;
static int sleeping, rearm_pending, rearm_ticks, failures, ticks, ramping, have_light, offset_known, since_set = 1000;
static int active = -1;          // follows the macOS switch (-1 = not checked yet)
static double light_log, offset, last_set = -1, last_lux, target = -1, floor_level = MIN_BRIGHT, hold_log, hold_level;
static int holding;
static char debug_path[1024];

static void on_signal(int s) { (void)s; stop_requested = 1; }

static void power_cb(void *ref, io_service_t s, natural_t type, void *arg) {
    (void)ref; (void)s;
    switch (type) {
    case kIOMessageCanSystemSleep:
        IOAllowPowerChange(pm_root, (long)arg);
        break;
    case kIOMessageSystemWillSleep:
        sleeping = 1;
        rearm_pending = 0;
        ramping = 0;
        if (armed) { sensor_off(); logmsg("sleep: sensor off"); }
        IOAllowPowerChange(pm_root, (long)arg);
        break;
    case kIOMessageSystemHasPoweredOn:
        sleeping = 0;
        target = -1;
        rearm_pending = active == 1;   // the hub resets in S3: power the sensor back on once the screen is on
        rearm_ticks = 15;              // >= 1.5 s
        have_light = 0;                // start smoothing afresh (the light may be very different after a wake)
        last_set = -1;                 // take the brightness as found; the learned offset is kept
        break;
    }
}

static void set_active(int on) {
    if (on == active) return;
    active = on;
    ramping = 0; have_light = 0; offset_known = 0; last_set = -1; target = -1; holding = 0;   // re-learn from the current level
    if (on) { close_hub(); sensor_on(); logmsg("macOS auto-brightness on: following the light sensor"); }
    else { if (armed) sensor_off(); logmsg("macOS auto-brightness off: idle, sensor off"); }
}

static void tick(CFRunLoopTimerRef t, void *info) {
    (void)t; (void)info;
    if (stop_requested) {
        sensor_off();
        close_hub();
        logmsg("agent stopped");
        exit(0);
    }
    if (sleeping) return;
    ++ticks;
    if (active < 0 || ticks % 20 == 0) set_active(macos_auto_on());   // check the switch every 2 s
    if (!active) return;
    CGDirectDisplayID bd = builtin_display();
    int screen_on = bd && !CGDisplayIsAsleep(bd);
    if (rearm_pending) {
        if (rearm_ticks > 0) rearm_ticks--;
        if (rearm_ticks > 0 || !screen_on) return;   // dark wake: leave the sensor off
        rearm_pending = 0;
        close_hub();
        if (sensor_on() == 0) logmsg("wake: sensor re-armed"); else logmsg("wake: sensor hub not found yet");
        return;
    }
    if (!screen_on) { ramping = 0; return; }

    float cur;
    if (get_brightness(bd, &cur) != 0) return;
    if (last_set < 0) last_set = cur;                  // first look after start / wake / switch-on
    if (since_set < 1000) since_set++;
    if ((!ramping && since_set >= SETTLE_TICKS && fabs(cur - last_set) > USER_DELTA) ||
        (ramping && fabs(cur - last_set) > USER_DELTA_RAMP)) {   // the user changed it (keys, slider)
        ramping = 0;
        if (target >= 0) target = cur;      // stop chasing the old target at once
        floor_level = fmin(MIN_BRIGHT, cur);   // you may go darker than the automatic floor
        holding = 1; hold_level = cur;      // keep your level until the light really changes
        hold_log = light_log;
        last_set = cur;
        if (have_light) {
            double lux = pow(10, light_log) - 1;
            offset = clampd(cur - curve(lux), -0.8, 0.8);
            offset_known = 1;
            logmsg("you set %.0f%% at %.0f lux: offset now %+.0f%%", cur * 100, lux, offset * 100);
        }
    }

    if (ticks % (READ_EVERY * 10) == 0) {   // every 10 s: re-assert D0 (another tool may have powered it off)
        IOHIDValueRef v = NULL;
        if (hub && IOHIDDeviceGetValue(hub, power_el, &v) == kIOReturnSuccess) { set_el(report_el, REPORTING_NONE); set_el(power_el, POWER_D0); }
    }
    if (ticks % READ_EVERY == 0) {          // once a second: read the sensor and update the target
        double lux;
        if (read_lux(&lux) != 0) {
            if (++failures % 5 == 0) { close_hub(); sensor_on(); }   // hub gone or reset: reopen every ~5 s
        } else {
            failures = 0;
            last_lux = lux;
            double l = log10(lux + 1);
            if (!have_light) {
                light_log = l;
                have_light = 1;
                if (!offset_known) {        // first reading after start / switch-on: keep the current level
                    offset = clampd(last_set - curve(lux), -0.8, 0.8);
                    offset_known = 1;
                }
            } else {
                double a = fabs(l - light_log) > 0.5 ? 0.5 : 0.2;   // big changes (>3x) settle faster
                light_log += a * (l - light_log);
            }
            if (holding && fabs(light_log - hold_log) < HOLD_LOG) target = hold_level;   // your level stays
            else { holding = 0; target = clampd(curve(pow(10, light_log) - 1) + offset, floor_level, 1.0); }
            if (access(debug_path, F_OK) == 0)   // touch ~/.local/share/autobright/debug to trace every second
                logmsg("debug: lux %.1f (smoothed %.1f) target %.0f%% display %.0f%% offset %+.0f%% ramping %d",
                       last_lux, pow(10, light_log) - 1, target * 100, cur * 100, offset * 100, ramping + 2 * holding);
        }
    }
    if (!have_light || target < 0) return;

    // Every tick: one small step towards the target (macOS animates each step itself).
    double base = ramping ? last_set : cur;   // the readback lags behind the animation: step from what we asked for
    double diff = target - base;
    if (!ramping && fabs(diff) < START_DELTA) return;
    if (fabs(diff) < 0.004) { ramping = 0; return; }
    double delta = clampd(diff, -STEP, STEP);
    if (ds_smooth(bd, (float)(base + delta - cur)) == 0) {   // relative to the panel's current level
        last_set = base + delta;
        since_set = 0;
        ramping = fabs(target - last_set) >= 0.004;
    } else {
        ramping = 0;
    }
}

static int run_agent(void) {
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    if (load_ds() != 0 || !ds_smooth) { logmsg("DisplayServices unavailable"); return 1; }
    snprintf(debug_path, sizeof debug_path, "%s/.local/share/autobright/debug", getenv("HOME") ? getenv("HOME") : "");
    logmsg("agent started (follows System Settings > Displays > Automatically adjust brightness)");
    IONotificationPortRef port;
    io_object_t notifier;
    pm_root = IORegisterForSystemPower(NULL, &port, power_cb, &notifier);
    if (pm_root) CFRunLoopAddSource(CFRunLoopGetCurrent(), IONotificationPortGetRunLoopSource(port), kCFRunLoopDefaultMode);
    CFRunLoopTimerRef timer = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + TICK, TICK, 0, 0, tick, NULL);
    CFRunLoopAddTimer(CFRunLoopGetCurrent(), timer, kCFRunLoopDefaultMode);
    CFRunLoopRun();
    return 0;
}

// ---------- switch (persistent LaunchAgent) ----------
static void paths(char *plist, size_t n, char *self, size_t m, char *dir, size_t k) {
    const char *home = getenv("HOME");
    snprintf(plist, n, "%s/Library/LaunchAgents/" LABEL ".plist", home ? home : "");
    snprintf(self, m, "%s/.local/bin/autobright", home ? home : "");
    snprintf(dir, k, "%s/.local/share/autobright", home ? home : "");
}

static int job_loaded(void) {
    char cmd[256];
    snprintf(cmd, sizeof cmd, "/bin/launchctl print gui/%d/" LABEL " >/dev/null 2>&1", getuid());
    return system(cmd) == 0;
}

static int turn_on(void) {
    char plist[1024], self[1024], dir[1024], cmd[2300];
    paths(plist, sizeof plist, self, sizeof self, dir, sizeof dir);
    snprintf(cmd, sizeof cmd, "/bin/mkdir -p '%s' \"$HOME/Library/LaunchAgents\"", dir);
    system(cmd);
    FILE *f = fopen(plist, "w");
    if (!f) { fprintf(stderr, "autobright: cannot write %s\n", plist); return 1; }
    fprintf(f,
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\"><dict>\n"
        "  <key>Label</key><string>" LABEL "</string>\n"
        "  <key>ProgramArguments</key><array><string>%s</string><string>run</string></array>\n"
        "  <key>RunAtLoad</key><true/>\n"
        "  <key>KeepAlive</key><dict><key>SuccessfulExit</key><false/></dict>\n"
        "  <key>ProcessType</key><string>Interactive</string>\n"
        "  <key>StandardErrorPath</key><string>%s/autobright.log</string>\n"
        "</dict></plist>\n", self, dir);
    fclose(f);
    if (job_loaded()) { printf("on\n"); return 0; }
    snprintf(cmd, sizeof cmd, "/bin/launchctl bootstrap gui/%d '%s'", getuid(), plist);
    int rc = system(cmd);
    printf(rc == 0 ? "on\n" : "failed\n");
    return rc == 0 ? 0 : 1;
}

static int turn_off(void) {
    char plist[1024], self[1024], dir[1024], cmd[1300];
    paths(plist, sizeof plist, self, sizeof self, dir, sizeof dir);
    if (job_loaded()) {
        snprintf(cmd, sizeof cmd, "/bin/launchctl bootout gui/%d/" LABEL " 2>/dev/null", getuid());
        system(cmd);   // the agent powers the sensor off on SIGTERM
    }
    unlink(plist);     // so it does not come back at the next login
    sensor_off();
    close_hub();
    printf("off\n");
    return 0;
}

int main(int argc, char **argv) {
    const char *cmd = argc > 1 ? argv[1] : "status";
    if (!strcmp(cmd, "run")) return run_agent();
    if (!strcmp(cmd, "on")) return turn_on();
    if (!strcmp(cmd, "off")) return turn_off();
    if (!strcmp(cmd, "toggle")) return job_loaded() ? turn_off() : turn_on();
    if (!strcmp(cmd, "status")) { printf(job_loaded() ? "on\n" : "off\n"); return 0; }
    if (!strcmp(cmd, "read")) {
        if (load_ds() != 0 || sensor_on() != 0) { fprintf(stderr, "sensor hub or DisplayServices not available\n"); return 1; }
        usleep(400000);
        double lux;
        float b = -1;
        int ok = read_lux(&lux) == 0;
        get_brightness(builtin_display(), &b);
        if (!job_loaded()) sensor_off();
        if (!ok) { fprintf(stderr, "read failed\n"); return 1; }
        printf("light %.1f lux -> curve %.0f%%, display at %.0f%% (agent %s, macOS auto-brightness %s)\n", lux,
               curve(lux) * 100, b * 100, job_loaded() ? "installed" : "not installed", macos_auto_on() ? "on" : "off");
        return 0;
    }
    fprintf(stderr, "usage: autobright on | off | toggle | status | read\n");
    return 2;
}
