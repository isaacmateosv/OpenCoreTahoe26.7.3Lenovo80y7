// iorep: print the current values of every IOReport channel in a group, per driver.
// Build: clang -O2 -framework CoreFoundation -framework IOKit -o iorep iorep.c
// Usage: iorep "IOHIDDevice Input Report Errors"
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <dlfcn.h>
#include <stdio.h>

typedef CFDictionaryRef (*CopyGroupFn)(CFStringRef, CFStringRef, uint64_t, uint64_t, uint64_t);
typedef void *(*CreateSubFn)(void *, CFMutableDictionaryRef, CFMutableDictionaryRef *, uint64_t, CFTypeRef);
typedef CFDictionaryRef (*CreateSamplesFn)(void *, CFMutableDictionaryRef, CFTypeRef);
typedef uint64_t (*GetDriverIDFn)(CFDictionaryRef);
typedef CFStringRef (*GetStrFn)(CFDictionaryRef);
typedef int64_t (*GetIntFn)(CFDictionaryRef, int);

static void regname(uint64_t id, char *out, size_t n) {
    snprintf(out, n, "id 0x%llx", (unsigned long long)id);
    io_service_t s = IOServiceGetMatchingService(kIOMainPortDefault, IORegistryEntryIDMatching(id));
    if (!s) return;
    io_name_t nm = {0}, cls = {0};
    IORegistryEntryGetName(s, nm); IOObjectGetClass(s, cls);
    io_registry_entry_t parent = 0; io_name_t pn = {0};
    // walk up two levels to show which device the simulator belongs to (TPL1 vs TPD0)
    io_registry_entry_t cur = s; IOObjectRetain(cur);
    for (int i = 0; i < 6; i++) {
        if (IORegistryEntryGetParentEntry(cur, kIOServicePlane, &parent) != KERN_SUCCESS) break;
        IOObjectRelease(cur); cur = parent;
        IORegistryEntryGetName(cur, pn);
        if (!strcmp(pn, "TPL1") || !strcmp(pn, "TPD0")) break;
        pn[0] = 0;
    }
    IOObjectRelease(cur);
    snprintf(out, n, "%s (%s)%s%s", nm, cls, pn[0] ? " under " : "", pn);
    IOObjectRelease(s);
}

int main(int argc, char **argv) {
    const char *group = argc > 1 ? argv[1] : "IOHIDDevice Input Report Errors";
    void *h = dlopen("/usr/lib/libIOReport.dylib", RTLD_NOW);
    if (!h) { fprintf(stderr, "cannot open libIOReport\n"); return 1; }
    CopyGroupFn copyGroup = dlsym(h, "IOReportCopyChannelsInGroup");
    CreateSubFn createSub = dlsym(h, "IOReportCreateSubscription");
    CreateSamplesFn samples = dlsym(h, "IOReportCreateSamples");
    GetDriverIDFn driverID = dlsym(h, "IOReportChannelGetDriverID");
    GetStrFn chanName = dlsym(h, "IOReportChannelGetChannelName");
    GetIntFn intVal = dlsym(h, "IOReportSimpleGetIntegerValue");
    if (!copyGroup || !createSub || !samples || !driverID || !chanName || !intVal) return 1;
    CFStringRef g = CFStringCreateWithCString(NULL, group, kCFStringEncodingUTF8);
    CFDictionaryRef chans = copyGroup(g, NULL, 0, 0, 0);
    if (!chans) { printf("no channels in group '%s'\n", group); return 0; }
    CFMutableDictionaryRef want = CFDictionaryCreateMutableCopy(NULL, 0, chans), subbed = NULL;
    void *sub = createSub(NULL, want, &subbed, 0, NULL);
    if (!sub || !subbed) { fprintf(stderr, "subscription failed\n"); return 1; }
    CFDictionaryRef s = samples(sub, subbed, NULL);
    CFArrayRef arr = s ? CFDictionaryGetValue(s, CFSTR("IOReportChannels")) : NULL;
    printf("group: %s\n", group);
    for (CFIndex i = 0; arr && i < CFArrayGetCount(arr); i++) {
        CFDictionaryRef ch = CFArrayGetValueAtIndex(arr, i);
        char nm[128] = {0}, drv[200];
        CFStringRef cn = chanName(ch); if (cn) CFStringGetCString(cn, nm, sizeof nm, kCFStringEncodingUTF8);
        regname(driverID(ch), drv, sizeof drv);
        printf("  %-70.70s %-24s %lld\n", drv, nm, (long long)intVal(ch, 0));
    }
    return 0;
}
