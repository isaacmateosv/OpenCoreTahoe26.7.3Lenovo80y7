//
//  MusicGVA: per-process AppleGVA hardware identity for Apple Music (Lilu plugin).
//
//  Problem (Yoga 920, UHD 620, macOS 26): Music's FairPlay tries the Intel GPU key exchange, which fails here, and
//  never falls back; songs stay at 0:00. WhateverGreen's unfairgva=4 fixes Music by patching AppleGVA (in the shared
//  cache, for every process) to read the IODeviceTree root property "hwgva-id" instead of "board-id", and setting
//  hwgva-id to the iMacPro1,1 board id. AppleGVA then avoids the Intel paths everywhere: Music works, but no app gets
//  hardware video decoding any more.
//
//  This plugin keeps WhateverGreen's AppleGVA patch and answers reads of "hwgva-id" on the device tree root per
//  process: Music gets the iMacPro1,1 id (software FairPlay, as with unfairgva=4), every other process gets the real
//  board-id (stock behaviour, hardware decoding). Two kernel functions are routed, both only acting on the device
//  tree root: IORegistryEntry::copyProperty(const char *) (every single-key read from user space, e.g.
//  IORegistryEntryCreateCFProperty / SearchCFProperty) and IORegistryEntry::serializeProperties (whole-table reads,
//  IORegistryEntryCreateCFProperties, ioreg -l).
//
//  Boot args: -musicgvaoff (disable), -musicgvadbg (debug log, DEBUG builds).
//  Copyright (c) 2026 isaacmateosv. MIT License.
//
#include <Headers/plugin_start.hpp>
#include <Headers/kern_api.hpp>
#include <Headers/kern_util.hpp>
#include <IOKit/IORegistryEntry.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSSerialize.h>
#include <sys/proc.h>

static const char *bootargOff[] {"-musicgvaoff"};
static const char *bootargDebug[] {"-musicgvadbg"};

static const char kHwGvaKey[] = "hwgva-id";
static const char kBoardIdKey[] = "board-id";
static const char kMacProBoardId[] = "Mac-7BA5B2D9E42DDD94";   // iMacPro1,1, same value WhateverGreen uses
static const char *const kTargets[] {"Music"};                  // p_comm of the processes that get the iMacPro id

static IORegistryEntry *dtRoot {nullptr};
static mach_vm_address_t orgCopyProperty {0};
static mach_vm_address_t orgSerializeProperties {0};
static uint32_t servedTarget {0}, servedOther {0};

static bool isTargetProcess() {
	char name[MAXCOMLEN + 1] {};
	proc_selfname(name, sizeof(name));
	for (auto t : kTargets)
		if (strcmp(name, t) == 0)
			return true;
	return false;
}

static OSObject *callOrgCopyProperty(const IORegistryEntry *that, const char *aKey) {
	if (orgCopyProperty == 0)   // copyProperty not routed (partial routing failure): the normal call is unhooked
		return that->copyProperty(aKey);
	return reinterpret_cast<OSObject *(*)(const IORegistryEntry *, const char *)>(orgCopyProperty)(that, aKey);
}

// The value this process should see for hwgva-id (retained; caller releases). nullptr if board-id is missing.
static OSObject *hwGvaValueForCaller(const IORegistryEntry *root) {
	if (isTargetProcess()) {
		if (servedTarget < 8) { servedTarget++; SYSLOG("mgva", "hwgva-id -> iMacPro1,1 for target process"); }
		return OSData::withBytes(kMacProBoardId, sizeof(kMacProBoardId));
	}
	if (servedOther < 8) { servedOther++; DBGLOG("mgva", "hwgva-id -> real board-id"); }
	return callOrgCopyProperty(root, kBoardIdKey);
}

static OSObject *wrapCopyProperty(const IORegistryEntry *that, const char *aKey) {
	if (that != dtRoot || aKey == nullptr || aKey[0] != 'h' || strcmp(aKey, kHwGvaKey) != 0)
		return callOrgCopyProperty(that, aKey);
	return hwGvaValueForCaller(that);
}

static bool wrapSerializeProperties(const IORegistryEntry *that, OSSerialize *s) {
	auto org = reinterpret_cast<bool (*)(const IORegistryEntry *, OSSerialize *)>(orgSerializeProperties);
	if (that != dtRoot)
		return org(that, s);
	OSDictionary *dict = that->dictionaryWithProperties();   // a private copy of the property table
	if (dict == nullptr)
		return org(that, s);
	OSObject *value = hwGvaValueForCaller(that);
	if (value) {
		dict->setObject(kHwGvaKey, value);
		value->release();
	}
	bool ok = dict->serialize(s);
	dict->release();
	return ok;
}

static void onPatcherLoad(void *, KernelPatcher &patcher) {
	dtRoot = IORegistryEntry::fromPath("/", gIODTPlane);   // retained for the lifetime of the kernel
	if (dtRoot == nullptr) {
		SYSLOG("mgva", "no device tree root, not routing");
		return;
	}
	KernelPatcher::RouteRequest requests[] {
		{"__ZNK15IORegistryEntry12copyPropertyEPKc", wrapCopyProperty, orgCopyProperty},
		{"__ZNK15IORegistryEntry19serializePropertiesEP11OSSerialize", wrapSerializeProperties, orgSerializeProperties},
	};
	if (patcher.routeMultiple(KernelPatcher::KernelID, requests))
		SYSLOG("mgva", "active: hwgva-id is per process (Music -> iMacPro1,1, others -> board-id)");
	else
		SYSLOG("mgva", "failed to route registry reads (%d)", patcher.getError());
	patcher.clearError();
}

static void pluginStart() {
	SYSLOG("mgva", "starting");
	lilu.onPatcherLoadForce(onPatcherLoad);
}

PluginConfiguration ADDPR(config) {
	xStringify(PRODUCT_NAME),
	parseModuleVersion(xStringify(MODULE_VERSION)),
	LiluAPI::AllowNormal,
	bootargOff, arrsize(bootargOff),
	bootargDebug, arrsize(bootargDebug),
	nullptr, 0,
	KernelVersion::BigSur,
	KernelVersion::Tahoe,
	pluginStart
};
