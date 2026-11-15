// irqstat: per-second interrupt counters for the Voodoo I2C devices, read from
// the kernel's own interrupt accounting via IOReport (no sudo needed).
// Build: clang -O2 -framework CoreFoundation -framework IOKit -o irqstat irqstat.c
// Usage: irqstat [seconds] [label]
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <dlfcn.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef CFDictionaryRef (*CopyGroupFn)(CFStringRef, CFStringRef, uint64_t, uint64_t, uint64_t);
typedef void *(*CreateSubFn)(void *, CFMutableDictionaryRef, CFMutableDictionaryRef *, uint64_t, CFTypeRef);
typedef CFDictionaryRef (*CreateSamplesFn)(void *, CFMutableDictionaryRef, CFTypeRef);
typedef CFDictionaryRef (*CreateDeltaFn)(CFDictionaryRef, CFDictionaryRef, CFTypeRef);
typedef uint64_t (*GetDriverIDFn)(CFDictionaryRef);
typedef CFStringRef (*GetStrFn)(CFDictionaryRef);
typedef int64_t (*GetIntFn)(CFDictionaryRef, int);

static CopyGroupFn pCopyGroup; static CreateSubFn pCreateSub; static CreateSamplesFn pSamples;
static CreateDeltaFn pDelta; static GetDriverIDFn pDriverID; static GetStrFn pChanName, pSubGroup; static GetIntFn pInt;

#define MAXSRC 64
typedef struct { uint64_t id; char sub[32]; char name[96]; long long c1, c2, t1, cpu2, sys2; } Src;
static Src srcs[MAXSRC]; static int nsrc;

static void regname(uint64_t id, char *out, size_t n) {
    snprintf(out, n, "id 0x%llx", (unsigned long long)id);
    io_service_t s = IOServiceGetMatchingService(kIOMainPortDefault, IORegistryEntryIDMatching(id));
    if (!s) return;
    io_name_t nm = {0}, cls = {0};
    IORegistryEntryGetName(s, nm); IOObjectGetClass(s, cls);
    snprintf(out, n, "%s (%s)", nm, cls);
    IOObjectRelease(s);
}

static Src *find(uint64_t id, const char *sub) {
    for (int i = 0; i < nsrc; i++) if (srcs[i].id == id && !strcmp(srcs[i].sub, sub)) return &srcs[i];
    if (nsrc >= MAXSRC) return NULL;
    Src *s = &srcs[nsrc++]; memset(s, 0, sizeof *s); s->id = id;
    snprintf(s->sub, sizeof s->sub, "%s", sub); regname(id, s->name, sizeof s->name);
    return s;
}

static void cstr(CFStringRef s, char *buf, size_t n) {
    buf[0] = 0; if (s) CFStringGetCString(s, buf, n, kCFStringEncodingUTF8);
}

static void accumulate(CFDictionaryRef delta) {
    for (int i = 0; i < nsrc; i++) srcs[i].c1 = srcs[i].c2 = srcs[i].t1 = srcs[i].cpu2 = srcs[i].sys2 = 0;
    CFArrayRef chans = CFDictionaryGetValue(delta, CFSTR("IOReportChannels"));
    if (!chans) return;
    for (CFIndex i = 0; i < CFArrayGetCount(chans); i++) {
        CFDictionaryRef ch = CFArrayGetValueAtIndex(chans, i);
        char nm[128], sub[64];
        cstr(pChanName(ch), nm, sizeof nm); cstr(pSubGroup(ch), sub, sizeof sub);
        Src *s = find(pDriverID(ch), sub); if (!s) continue;
        long long v = pInt(ch, 0);
        if (strstr(nm, "First Level Interrupt Handler Count")) s->c1 += v;
        else if (strstr(nm, "Second Level Interrupt Handler Count")) s->c2 += v;
        else if (strstr(nm, "First Level Interrupt Handler Time")) s->t1 += v;
        else if (strstr(nm, "Second Level Interrupt Handler CPU Time")) s->cpu2 += v;
        else if (strstr(nm, "Second Level Interrupt Handler System Time")) s->sys2 += v;
    }
}

static int interesting(const Src *s) {
    return strstr(s->name, "TPL1") || strstr(s->name, "TPD0") || strstr(s->name, "I2C") ||
           strstr(s->name, "pci8086,9d6") || strstr(s->name, "GPI");
}

int main(int argc, char **argv) {
    int secs = argc > 1 ? atoi(argv[1]) : 10; if (secs < 1) secs = 10;
    const char *label = argc > 2 ? argv[2] : "";
    void *h = dlopen("/usr/lib/libIOReport.dylib", RTLD_NOW);
    if (!h) { fprintf(stderr, "cannot open libIOReport\n"); return 1; }
    pCopyGroup = dlsym(h, "IOReportCopyChannelsInGroup"); pCreateSub = dlsym(h, "IOReportCreateSubscription");
    pSamples = dlsym(h, "IOReportCreateSamples"); pDelta = dlsym(h, "IOReportCreateSamplesDelta");
    pDriverID = dlsym(h, "IOReportChannelGetDriverID"); pChanName = dlsym(h, "IOReportChannelGetChannelName");
    pSubGroup = dlsym(h, "IOReportChannelGetSubGroup"); pInt = dlsym(h, "IOReportSimpleGetIntegerValue");
    if (!pCopyGroup || !pCreateSub || !pSamples || !pDelta || !pDriverID || !pChanName || !pSubGroup || !pInt) {
        fprintf(stderr, "missing IOReport symbols\n"); return 1;
    }
    CFDictionaryRef chans = pCopyGroup(CFSTR("Interrupt Statistics (by index)"), NULL, 0, 0, 0);
    if (!chans) { fprintf(stderr, "no interrupt statistics channels\n"); return 1; }
    CFMutableDictionaryRef want = CFDictionaryCreateMutableCopy(NULL, 0, chans), subbed = NULL;
    void *sub = pCreateSub(NULL, want, &subbed, 0, NULL);
    if (!sub || !subbed) { fprintf(stderr, "subscription failed\n"); return 1; }
    mach_timebase_info_data_t tb; mach_timebase_info(&tb);
    double ns_per_tick = (double)tb.numer / tb.denom;

    printf("irqstat %s — per-second interrupt counters (kernel interrupt accounting)\n", label);
    printf("%-4s %-34s %7s %7s %10s\n", "sec", "interrupt source", "irq/s", "work/s", "avg work");
    CFDictionaryRef prev = pSamples(sub, subbed, NULL);
    long long tot_c1[MAXSRC] = {0}, tot_c2[MAXSRC] = {0}; double tot_t[MAXSRC] = {0};
    for (int t = 1; t <= secs; t++) {
        sleep(1);
        CFDictionaryRef cur = pSamples(sub, subbed, NULL);
        CFDictionaryRef d = pDelta(prev, cur, NULL);
        accumulate(d);
        for (int i = 0; i < nsrc; i++) {
            Src *s = &srcs[i];
            if (!interesting(s) && s->c1 < 500) continue;
            if (!s->c1 && !s->c2 && !interesting(s)) continue;
            double work_us = s->c2 ? (s->sys2 * ns_per_tick / 1000.0) / s->c2 : 0;
            tot_c1[i] += s->c1; tot_c2[i] += s->c2; tot_t[i] += s->sys2 * ns_per_tick / 1000.0;
            char nm[40]; snprintf(nm, sizeof nm, "%s#%s", s->name, s->sub);
            printf("%-4d %-34.34s %7lld %7lld %8.0fus\n", t, nm, s->c1, s->c2, work_us);
        }
        fflush(stdout);
        CFRelease(d); CFRelease(prev); prev = cur;
    }
    printf("---- totals over %d s ----\n", secs);
    for (int i = 0; i < nsrc; i++) {
        if (!tot_c1[i] && !interesting(&srcs[i])) continue;
        double avg = tot_c2[i] ? tot_t[i] / tot_c2[i] : 0;
        printf("%-44.44s irq %7lld (%6.1f/s)  work %7lld  avg %6.0fus  busy %5.1f%%\n",
               srcs[i].name, tot_c1[i], (double)tot_c1[i] / secs, tot_c2[i], avg, tot_t[i] / (secs * 1e6) * 100.0);
    }
    return 0;
}
