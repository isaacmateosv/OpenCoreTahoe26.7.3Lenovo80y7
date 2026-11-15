// ecwatch: prints a timestamped line every time the embedded controller (H_EC)
// raises its ACPI interrupt (SCI), using the kernel's own interrupt accounting
// via IOReport (no sudo). Every EC event (lid, AC adapter, battery, hotkeys)
// arrives as one or more SCIs, so a lid close that produces no SCI never left
// the EC, while one that produces an SCI but no "Clamshell closed" was lost in ACPI.
// Build: clang -O2 -framework CoreFoundation -framework IOKit -o ecwatch ecwatch.c
// Usage: ecwatch [seconds]   (default: run until killed)
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
typedef uint64_t (*GetDriverIDFn)(CFDictionaryRef);
typedef CFStringRef (*GetStrFn)(CFDictionaryRef);
typedef int64_t (*GetIntFn)(CFDictionaryRef, int);

static void stamp(char *buf, size_t n) {
    struct timeval tv; gettimeofday(&tv, NULL);
    struct tm tm; localtime_r(&tv.tv_sec, &tm);
    size_t k = strftime(buf, n, "%H:%M:%S", &tm);
    snprintf(buf + k, n - k, ".%03d", (int)(tv.tv_usec / 1000));
}

int main(int argc, char **argv) {
    int secs = argc > 1 ? atoi(argv[1]) : 0;
    void *h = dlopen("/usr/lib/libIOReport.dylib", RTLD_NOW);
    if (!h) { fprintf(stderr, "cannot open libIOReport\n"); return 1; }
    CopyGroupFn pCopyGroup = dlsym(h, "IOReportCopyChannelsInGroup");
    CreateSubFn pCreateSub = dlsym(h, "IOReportCreateSubscription");
    CreateSamplesFn pSamples = dlsym(h, "IOReportCreateSamples");
    GetDriverIDFn pDriverID = dlsym(h, "IOReportChannelGetDriverID");
    GetStrFn pChanName = dlsym(h, "IOReportChannelGetChannelName");
    GetIntFn pInt = dlsym(h, "IOReportSimpleGetIntegerValue");
    if (!pCopyGroup || !pCreateSub || !pSamples || !pDriverID || !pChanName || !pInt) {
        fprintf(stderr, "missing IOReport symbols\n"); return 1;
    }

    // Registry ID of the EC's ACPI device node (the interrupt statistics are reported against it).
    io_service_t ec = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceNameMatching("H_EC"));
    if (!ec) { fprintf(stderr, "H_EC not found\n"); return 1; }
    uint64_t ecid = 0; IORegistryEntryGetRegistryEntryID(ec, &ecid); IOObjectRelease(ec);

    CFDictionaryRef chans = pCopyGroup(CFSTR("Interrupt Statistics (by index)"), NULL, 0, 0, 0);
    if (!chans) { fprintf(stderr, "no interrupt statistics channels\n"); return 1; }
    CFMutableDictionaryRef want = CFDictionaryCreateMutableCopy(NULL, 0, chans), subbed = NULL;
    void *sub = pCreateSub(NULL, want, &subbed, 0, NULL);
    if (!sub || !subbed) { fprintf(stderr, "subscription failed\n"); return 1; }

    long long last = -1; char ts[32];
    time_t start = time(NULL);
    for (;;) {
        CFDictionaryRef s = pSamples(sub, subbed, NULL);
        long long count = 0;
        CFArrayRef arr = s ? CFDictionaryGetValue(s, CFSTR("IOReportChannels")) : NULL;
        for (CFIndex i = 0; arr && i < CFArrayGetCount(arr); i++) {
            CFDictionaryRef ch = CFArrayGetValueAtIndex(arr, i);
            if (pDriverID(ch) != ecid) continue;
            char nm[128] = {0}; CFStringRef cn = pChanName(ch);
            if (cn) CFStringGetCString(cn, nm, sizeof nm, kCFStringEncodingUTF8);
            if (strstr(nm, "First Level Interrupt Handler Count")) count += pInt(ch, 0);
        }
        if (s) CFRelease(s);
        if (count != last) {
            stamp(ts, sizeof ts);
            if (last < 0) printf("%s EC SCI count %lld (start)\n", ts, count);
            else printf("%s EC SCI count %lld (+%lld)\n", ts, count, count - last);
            fflush(stdout);
            last = count;
        }
        if (secs && time(NULL) - start >= secs) break;
        usleep(250000);
    }
    return 0;
}
