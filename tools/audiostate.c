// audiostate: CoreAudio snapshot. Output devices (default, sample rate, volume, mute, source, jack,
// running, hog owner) and every audio client process: is it playing, and to which device.
// Build: clang -O2 -framework CoreAudio -framework CoreFoundation -o audiostate audiostate.c
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <libproc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static OSStatus get(AudioObjectID o, AudioObjectPropertySelector s, AudioObjectPropertyScope sc,
                    AudioObjectPropertyElement el, UInt32 *size, void *out) {
    AudioObjectPropertyAddress a = {s, sc, el};
    return AudioObjectGetPropertyData(o, &a, 0, NULL, size, out);
}

static int has(AudioObjectID o, AudioObjectPropertySelector s, AudioObjectPropertyScope sc, AudioObjectPropertyElement el) {
    AudioObjectPropertyAddress a = {s, sc, el};
    return AudioObjectHasProperty(o, &a);
}

static void cfs(CFStringRef s, char *buf, size_t n) {
    buf[0] = 0;
    if (s) CFStringGetCString(s, buf, (CFIndex)n, kCFStringEncodingUTF8);
}

static void obj_name(AudioObjectID o, char *buf, size_t n) {
    CFStringRef s = NULL;
    UInt32 sz = sizeof s;
    buf[0] = 0;
    if (get(o, kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain, &sz, &s) == 0 && s) {
        cfs(s, buf, n);
        CFRelease(s);
    }
}

static int channels(AudioObjectID d, AudioObjectPropertyScope scope) {
    AudioObjectPropertyAddress a = {kAudioDevicePropertyStreamConfiguration, scope, kAudioObjectPropertyElementMain};
    UInt32 sz = 0;
    if (AudioObjectGetPropertyDataSize(d, &a, 0, NULL, &sz) != 0 || !sz) return 0;
    AudioBufferList *bl = malloc(sz);
    int ch = 0;
    if (AudioObjectGetPropertyData(d, &a, 0, NULL, &sz, bl) == 0)
        for (UInt32 i = 0; i < bl->mNumberBuffers; i++) ch += (int)bl->mBuffers[i].mNumberChannels;
    free(bl);
    return ch;
}

static AudioObjectID *list(AudioObjectID o, AudioObjectPropertySelector s, AudioObjectPropertyScope sc, int *count) {
    AudioObjectPropertyAddress a = {s, sc, kAudioObjectPropertyElementMain};
    UInt32 sz = 0;
    *count = 0;
    if (AudioObjectGetPropertyDataSize(o, &a, 0, NULL, &sz) != 0 || !sz) return NULL;
    AudioObjectID *ids = malloc(sz);
    if (AudioObjectGetPropertyData(o, &a, 0, NULL, &sz, ids) != 0) { free(ids); return NULL; }
    *count = (int)(sz / sizeof(AudioObjectID));
    return ids;
}

int main(void) {
    AudioObjectID def = 0, sysdef = 0;
    UInt32 sz = sizeof def;
    get(kAudioObjectSystemObject, kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain, &sz, &def);
    sz = sizeof sysdef;
    get(kAudioObjectSystemObject, kAudioHardwarePropertyDefaultSystemOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain, &sz, &sysdef);

    int nd = 0;
    AudioObjectID *devs = list(kAudioObjectSystemObject, kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, &nd);
    printf("== output devices\n");
    for (int i = 0; i < nd; i++) {
        AudioObjectID d = devs[i];
        int och = channels(d, kAudioObjectPropertyScopeOutput);
        if (och == 0) continue;
        char name[256];
        obj_name(d, name, sizeof name);
        Float64 rate = 0;
        sz = sizeof rate;
        get(d, kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain, &sz, &rate);
        UInt32 running = 0;
        sz = sizeof running;
        get(d, kAudioDevicePropertyDeviceIsRunningSomewhere, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain, &sz, &running);
        pid_t hog = -1;
        sz = sizeof hog;
        get(d, kAudioDevicePropertyHogMode, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain, &sz, &hog);
        printf("[%u] %s%s%s  out=%dch  rate=%.0f  running=%u  hog=%d\n", d, name, d == def ? "  (DEFAULT OUTPUT)" : "",
               d == sysdef ? " (SYSTEM SOUNDS)" : "", och, rate, running, (int)hog);

        // available rates
        AudioObjectPropertyAddress ra = {kAudioDevicePropertyAvailableNominalSampleRates, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
        UInt32 rsz = 0;
        if (AudioObjectGetPropertyDataSize(d, &ra, 0, NULL, &rsz) == 0 && rsz) {
            AudioValueRange *r = malloc(rsz);
            if (AudioObjectGetPropertyData(d, &ra, 0, NULL, &rsz, r) == 0) {
                printf("     rates:");
                for (UInt32 k = 0; k < rsz / sizeof *r; k++) printf(" %.0f", r[k].mMinimum);
                printf("\n");
            }
            free(r);
        }
        // volume and mute per element (0 = main, 1..n = channels)
        printf("     volume:");
        for (UInt32 el = 0; el <= (UInt32)och && el <= 8; el++) {
            if (!has(d, kAudioDevicePropertyVolumeScalar, kAudioObjectPropertyScopeOutput, el)) continue;
            Float32 v = -1;
            sz = sizeof v;
            get(d, kAudioDevicePropertyVolumeScalar, kAudioObjectPropertyScopeOutput, el, &sz, &v);
            printf(" el%u=%.2f", el, v);
        }
        printf("   mute:");
        for (UInt32 el = 0; el <= (UInt32)och && el <= 8; el++) {
            if (!has(d, kAudioDevicePropertyMute, kAudioObjectPropertyScopeOutput, el)) continue;
            UInt32 m = 9;
            sz = sizeof m;
            get(d, kAudioDevicePropertyMute, kAudioObjectPropertyScopeOutput, el, &sz, &m);
            printf(" el%u=%u", el, m);
        }
        printf("\n");
        // data source (speakers / headphones) and jack
        if (has(d, kAudioDevicePropertyDataSource, kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMain)) {
            UInt32 src = 0;
            sz = sizeof src;
            get(d, kAudioDevicePropertyDataSource, kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMain, &sz, &src);
            CFStringRef sn = NULL;
            AudioValueTranslation t = {&src, sizeof src, &sn, sizeof sn};
            sz = sizeof t;
            char sname[128] = "?";
            if (get(d, kAudioDevicePropertyDataSourceNameForIDCFString, kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMain, &sz, &t) == 0 && sn) {
                cfs(sn, sname, sizeof sname);
                CFRelease(sn);
            }
            printf("     source: %s ('%c%c%c%c')", sname, (char)(src >> 24), (char)(src >> 16), (char)(src >> 8), (char)src);
            if (has(d, kAudioDevicePropertyJackIsConnected, kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMain)) {
                UInt32 jack = 0;
                sz = sizeof jack;
                get(d, kAudioDevicePropertyJackIsConnected, kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMain, &sz, &jack);
                printf("  jack-connected=%u", jack);
            }
            printf("\n");
        }
    }

    int np = 0;
    AudioObjectID *procs = list(kAudioObjectSystemObject, kAudioHardwarePropertyProcessObjectList, kAudioObjectPropertyScopeGlobal, &np);
    printf("== audio client processes (%d)\n", np);
    for (int i = 0; i < np; i++) {
        AudioObjectID p = procs[i];
        pid_t pid = -1;
        sz = sizeof pid;
        get(p, kAudioProcessPropertyPID, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain, &sz, &pid);
        CFStringRef bid = NULL;
        sz = sizeof bid;
        char bundle[256] = "";
        if (get(p, kAudioProcessPropertyBundleID, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain, &sz, &bid) == 0 && bid) {
            cfs(bid, bundle, sizeof bundle);
            CFRelease(bid);
        }
        char path[PROC_PIDPATHINFO_MAXSIZE] = "";
        proc_pidpath(pid, path, sizeof path);
        const char *base = strrchr(path, '/');
        UInt32 out = 0, in = 0;
        sz = sizeof out;
        get(p, kAudioProcessPropertyIsRunningOutput, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain, &sz, &out);
        sz = sizeof in;
        get(p, kAudioProcessPropertyIsRunningInput, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain, &sz, &in);
        int ndv = 0;
        AudioObjectID *pd = list(p, kAudioProcessPropertyDevices, kAudioObjectPropertyScopeOutput, &ndv);
        printf("pid %-6d %-40s %-22s playing=%u recording=%u  output devices:", (int)pid, bundle[0] ? bundle : "-",
               base ? base + 1 : "?", out, in);
        for (int k = 0; k < ndv; k++) {
            char dn[256];
            obj_name(pd[k], dn, sizeof dn);
            printf(" [%u] %s", pd[k], dn);
        }
        printf("\n");
        free(pd);
    }
    free(procs);
    free(devs);
    return 0;
}
