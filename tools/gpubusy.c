// gpubusy: iGPU activity every 500 ms, no sudo. Columns:
//   awake  = render C-state RC0 residency ("GPU C-States" IOReport group; awake, not necessarily busy)
//   util   = IntelAccelerator "Device Utilization %" (busy time scaled to max clock)
//   @clk   = "Device Utilization % at cur p-state" (busy time at the current clock)
//   clock  = util / @clk = current GPU clock as a share of its maximum
// For the same animation workload, a GPU that ramps its clock higher shows a higher "clock"
// and a lower "@clk" (more headroom per frame).
// Build: clang -O2 -framework CoreFoundation -framework IOKit -o gpubusy gpubusy.c
// Usage: gpubusy [seconds]
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

typedef CFDictionaryRef (*CopyGroupFn)(CFStringRef, CFStringRef, uint64_t, uint64_t, uint64_t);
typedef void *(*CreateSubFn)(void *, CFMutableDictionaryRef, CFMutableDictionaryRef *, uint64_t, CFTypeRef);
typedef CFDictionaryRef (*CreateSamplesFn)(void *, CFMutableDictionaryRef, CFTypeRef);
typedef CFDictionaryRef (*CreateDeltaFn)(CFDictionaryRef, CFDictionaryRef, CFTypeRef);
typedef CFStringRef (*GetStrFn)(CFDictionaryRef);
typedef int64_t (*GetIntFn)(CFDictionaryRef, int);

static int perfStat(io_service_t accel, CFStringRef key) {
    int v = -1;
    CFDictionaryRef ps = IORegistryEntryCreateCFProperty(accel, CFSTR("PerformanceStatistics"), NULL, 0);
    if (!ps) return -1;
    CFNumberRef n = CFDictionaryGetValue(ps, key);
    if (n) CFNumberGetValue(n, kCFNumberIntType, &v);
    CFRelease(ps);
    return v;
}

int main(int argc, char **argv) {
    int secs = argc > 1 ? atoi(argv[1]) : 30;
    if (secs < 1) secs = 30;
    void *h = dlopen("/usr/lib/libIOReport.dylib", RTLD_NOW);
    if (!h) { fprintf(stderr, "cannot open libIOReport\n"); return 1; }
    CopyGroupFn pCopyGroup = dlsym(h, "IOReportCopyChannelsInGroup");
    CreateSubFn pCreateSub = dlsym(h, "IOReportCreateSubscription");
    CreateSamplesFn pSamples = dlsym(h, "IOReportCreateSamples");
    CreateDeltaFn pDelta = dlsym(h, "IOReportCreateSamplesDelta");
    GetStrFn pChanName = dlsym(h, "IOReportChannelGetChannelName");
    GetIntFn pInt = dlsym(h, "IOReportSimpleGetIntegerValue");
    if (!pCopyGroup || !pCreateSub || !pSamples || !pDelta || !pChanName || !pInt) {
        fprintf(stderr, "missing IOReport symbols\n"); return 1;
    }
    CFDictionaryRef chans = pCopyGroup(CFSTR("GPU C-States"), NULL, 0, 0, 0);
    if (!chans) { fprintf(stderr, "no GPU C-States channels\n"); return 1; }
    CFMutableDictionaryRef want = CFDictionaryCreateMutableCopy(NULL, 0, chans), subbed = NULL;
    void *sub = pCreateSub(NULL, want, &subbed, 0, NULL);
    if (!sub || !subbed) { fprintf(stderr, "subscription failed\n"); return 1; }

    io_service_t accel = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("IntelAccelerator"));
    CFDictionaryRef prev = pSamples(sub, subbed, NULL);
    int n = secs * 2, busyN = 0;
    double busySum = 0, peak = 0, clkSum = 0, atSum = 0;
    for (int i = 0; i < n; i++) {
        usleep(500000);
        CFDictionaryRef cur = pSamples(sub, subbed, NULL);
        CFDictionaryRef d = pDelta(prev, cur, NULL);
        double rc0 = 0, total = 0;
        CFArrayRef arr = CFDictionaryGetValue(d, CFSTR("IOReportChannels"));
        for (CFIndex k = 0; arr && k < CFArrayGetCount(arr); k++) {
            CFDictionaryRef ch = CFArrayGetValueAtIndex(arr, k);
            char nm[32] = {0}; CFStringRef cn = pChanName(ch);
            if (cn) CFStringGetCString(cn, nm, sizeof nm, kCFStringEncodingUTF8);
            double v = (double)pInt(ch, 0);
            total += v;
            if (!strcmp(nm, "RC0")) rc0 += v;
        }
        double pct = total > 0 ? 100.0 * rc0 / total : 0;
        struct timeval tv; gettimeofday(&tv, NULL); struct tm tm; localtime_r(&tv.tv_sec, &tm);
        char ts[16]; strftime(ts, sizeof ts, "%H:%M:%S", &tm);
        int util = accel ? perfStat(accel, CFSTR("Device Utilization %")) : -1;
        int at = accel ? perfStat(accel, CFSTR("Device Utilization % at cur p-state")) : -1;
        double clk = (util >= 0 && at > 0) ? 100.0 * util / at : -1;
        printf("%s.%d  awake %5.1f%%  util %3d%%  @clk %3d%%  clock %s%5.0f%%  %.*s\n", ts,
               (int)(tv.tv_usec / 100000), pct, util, at, clk < 0 ? " n/a" : "", clk < 0 ? 0 : clk,
               at > 0 ? at / 3 : 0, "##################################");
        fflush(stdout);
        if (at >= 15) { busyN++; busySum += at; clkSum += clk; atSum += util; }
        if (at > peak) peak = at;
        CFRelease(d); CFRelease(prev); prev = cur;
    }
    printf("---- %d samples; %d active (@clk >= 15%%): avg @clk %.1f%%, avg util %.1f%%, avg clock %.0f%% of max; peak @clk %.0f%%\n",
           n, busyN, busyN ? busySum / busyN : 0, busyN ? atSum / busyN : 0, busyN ? clkSum / busyN : 0, peak);
    return 0;
}
