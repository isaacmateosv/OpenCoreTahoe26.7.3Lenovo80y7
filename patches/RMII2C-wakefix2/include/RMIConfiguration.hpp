/* Declaration-only stand-in for VoodooRMI's Configuration helper.
 * Configuration::mapArrayToDict(OSArray*) is implemented and exported by VoodooRMI.kext. */

#ifndef RMIConfiguration_hpp
#define RMIConfiguration_hpp

#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSDictionary.h>

class Configuration {
public:
    static OSDictionary *mapArrayToDict(OSArray *arr);
};

#endif /* RMIConfiguration_hpp */
