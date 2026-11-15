// rotate: rotates the built-in display of this Yoga 920 Hackintosh. The touchscreen follows, because
// VoodooI2CHID maps touches through the framebuffer's IOFBTransform.
//
// Usage: rotate 0|90|180|270   set an orientation (degrees clockwise)
//        rotate toggle [deg]   landscape <-> portrait (default portrait = 90)
//        rotate next           step 0 -> 90 -> 180 -> 270 -> 0
//        rotate status         print the current orientation
// Build: clang -O2 -framework ApplicationServices -framework IOKit -o rotate rotate.c
#include <ApplicationServices/ApplicationServices.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/graphics/IOGraphicsLib.h>
#include <IOKit/graphics/IOGraphicsTypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define kIOFBSetTransform 0x00000400   // IOFramebuffer probe option: apply the transform in bits 16-31

// The built-in panel's framebuffer: the one with an AppleBacklightDisplay below it.
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

static int set_degrees(int deg) {
    IOOptionBits bits;
    switch (deg) {
    case 0:   bits = kIOScaleRotate0;   break;
    case 90:  bits = kIOScaleRotate90;  break;
    case 180: bits = kIOScaleRotate180; break;
    case 270: bits = kIOScaleRotate270; break;
    default:  fprintf(stderr, "rotate: angle must be 0, 90, 180 or 270\n"); return 2;
    }
    if (current_degrees() == deg) { printf("%d\n", deg); return 0; }
    io_service_t fb = builtin_framebuffer();
    if (!fb) { fprintf(stderr, "rotate: built-in display framebuffer not found\n"); return 1; }
    kern_return_t kr = IOServiceRequestProbe(fb, kIOFBSetTransform | (bits << 16));
    IOObjectRelease(fb);
    if (kr != KERN_SUCCESS) { fprintf(stderr, "rotate: request failed (0x%x)\n", kr); return 1; }
    for (int i = 0; i < 40 && current_degrees() != deg; i++) usleep(50000);   // wait up to 2 s for the mode change
    int now = current_degrees();
    printf("%d\n", now);
    return now == deg ? 0 : 1;
}

int main(int argc, char **argv) {
    const char *cmd = argc > 1 ? argv[1] : "status";
    int cur = current_degrees();
    if (cur < 0) { fprintf(stderr, "rotate: no built-in display found\n"); return 1; }
    if (!strcmp(cmd, "status")) { printf("%d\n", cur); return 0; }
    if (!strcmp(cmd, "toggle")) {
        int portrait = argc > 2 ? atoi(argv[2]) : 90;
        return set_degrees(cur == 0 ? portrait : 0);
    }
    if (!strcmp(cmd, "next")) return set_degrees((cur + 90) % 360);
    char *end;
    long deg = strtol(cmd, &end, 10);
    if (*end) { fprintf(stderr, "usage: rotate 0|90|180|270 | toggle [deg] | next | status\n"); return 2; }
    return set_degrees((int)deg);
}
