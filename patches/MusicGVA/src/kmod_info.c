#include <mach/mach_types.h>
#include <mach/kmod.h>

extern kern_return_t _start(kmod_info_t *ki, void *data);
extern kern_return_t _stop(kmod_info_t *ki, void *data);
extern kern_return_t MusicGVA_kern_start(kmod_info_t *ki, void *data);
extern kern_return_t MusicGVA_kern_stop(kmod_info_t *ki, void *data);

__attribute__((visibility("default"))) KMOD_EXPLICIT_DECL(org.yoga920.MusicGVA, "1.0.0", _start, _stop)
__private_extern__ kmod_start_func_t *_realmain = MusicGVA_kern_start;
__private_extern__ kmod_stop_func_t *_antimain = MusicGVA_kern_stop;
__private_extern__ int _kext_apple_cc = __APPLE_CC__;
