/*
 * Minimal declaration of VoodooI2C's VoodooI2CDeviceNub (VoodooI2C 0271bbd).
 * RMII2C only uses the non-virtual I2C helpers below (resolved by symbol name
 * against the installed VoodooI2C.kext) and IOService virtuals (whose vtable
 * slots are defined by IOService), so this subset is ABI-compatible.
 */

#ifndef VoodooI2CDeviceNub_hpp
#define VoodooI2CDeviceNub_hpp

#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>

#define I2C_DSM_HIDG "3cdff6f7-4267-4555-ad05-b30a3d8938de"
#define I2C_DSM_REVISION 1
#define HIDG_DESC_INDEX 1

class __attribute__((visibility("default"))) VoodooI2CDeviceNub : public IOService {
    OSDeclareDefaultStructors(VoodooI2CDeviceNub);

 public:
    IOReturn readI2C(UInt8* values, UInt16 length);
    IOReturn writeI2C(UInt8* values, UInt16 length);
    IOReturn writeReadI2C(UInt8* write_buffer, UInt16 write_length, UInt8* read_buffer, UInt16 read_length);
    IOReturn evaluateDSM(const char *uuid, UInt32 index, OSObject **result);
};

#endif /* VoodooI2CDeviceNub_hpp */
