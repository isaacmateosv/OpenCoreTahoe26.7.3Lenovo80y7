/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Zhen
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/rmi_i2c.c
 * https://github.com/torvalds/linux/blob/master/drivers/hid/hid-rmi.c
 * and code written by Alexandre, CoolStar and Kishor Prins
 * https://github.com/VoodooI2C/VoodooI2CSynaptics
 *
 * Copyright (c) 2011-2016 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 *
 * Patched for a Lenovo Yoga 920 (Synaptics SYNA2B31 on a level-triggered
 * GPIO line), based on VoodooRMI 1.4.3 (83868f0). See RMII2C.cpp.
 */

#ifndef RMII2C_h
#define RMII2C_h

#include "RMITransport.hpp"
#include "VoodooI2CDeviceNub.hpp"
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOMemoryDescriptor.h>

#define SYNAPTICS_VENDOR_ID         0x6cb

#define RMI_MOUSE_REPORT_ID         0x01 /* Mouse emulation Report */
#define RMI_WRITE_REPORT_ID         0x09 /* Output Report */
#define RMI_READ_ADDR_REPORT_ID     0x0a /* Output Report */
#define RMI_READ_DATA_REPORT_ID     0x0b /* Input Report */
#define RMI_ATTN_REPORT_ID          0x0c /* Input Report */
#define RMI_SET_RMI_MODE_REPORT_ID  0x0f /* Feature Report */

#define RMI_PAGE_SELECT_REGISTER    0xff
#define RMI_I2C_PAGE(addr) (((addr) >> 8) & 0xff)

// fallback when HID descriptor is not available
#define RMI_HID_DESC_REGISTER       0x20
#define RMI_HID_COMMAND_REGISTER    0x22
#define RMI_HID_DATA_REGISTER       0x23
#define RMI_HID_OUTPUT_REGISTER     0x25

#define HID_GENERIC_POINTER         0x01
#define HID_GENERIC_MOUSE           0x02

#define INTERRUPT_SIMULATOR_INTERVAL 200
#define INTERRUPT_SIMULATOR_TIMEOUT 5

// I2C-HID protocol (HID over I2C spec v1.0, section 7.2)
#define I2C_HID_OPCODE_RESET        0x01
#define I2C_HID_OPCODE_SET_POWER    0x08
#define I2C_HID_PWR_ON              0x00
#define I2C_HID_PWR_SLEEP           0x01

// Stuck interrupt line handling
#define RMI_STUCK_EMPTY_READS       64      // reads in a row with the line asserted but no report
#define RMI_FAIL_BACKOFF_MS         20      // re-arm delay after a failed read
#define RMI_FAIL_RECOVER_COUNT      50      // failed reads in a row before a recovery
#define RMI_RECOVERY_WINDOW_MS      30000
#define RMI_RECOVERY_MAX_IN_WINDOW  4       // then fall back to polling until the next wake
#define RMI_STATS_PUBLISH_MS        2000

enum rmi_mode_type {
    RMI_MODE_OFF = 0,
    RMI_MODE_ATTN_REPORTS = 1,
    RMI_MODE_NO_PACKED_ATTN_REPORTS = 2,
};

typedef struct __attribute__((__packed__)) {
    UInt16 wHIDDescLength;
    UInt16 bcdVersion;
    UInt16 wReportDescLength;
    UInt16 wReportDescRegister;
    UInt16 wInputRegister;
    UInt16 wMaxInputLength;
    UInt16 wOutputRegister;
    UInt16 wMaxOutputLength;
    UInt16 wCommandRegister;
    UInt16 wDataRegister;
    UInt16 wVendorID;
    UInt16 wProductID;
    UInt16 wVersionID;
    UInt32 reserved;
} i2c_hid_desc;

class RMII2C : public RMITransport {
    OSDeclareDefaultStructors(RMII2C);
    typedef RMITransport super;

public:
    const char* name;

    RMII2C *probe(IOService *provider, SInt32 *score) APPLE_KEXT_OVERRIDE;
    bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
    void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
    IOReturn setPowerState(unsigned long powerState, IOService *whatDevice) APPLE_KEXT_OVERRIDE;

    bool open(IOService *client, IOOptionBits options, RMIAttentionAction action) APPLE_KEXT_OVERRIDE;
    int reset() APPLE_KEXT_OVERRIDE;
    int readBlock(UInt16 rmiaddr, UInt8 *databuff, size_t len) APPLE_KEXT_OVERRIDE;
    int blockWrite(UInt16 rmiaddr, UInt8 *buf, size_t len) APPLE_KEXT_OVERRIDE;
    virtual OSDictionary *createConfig() APPLE_KEXT_OVERRIDE;

private:
    enum HelperAction { kHelperNone = 0, kHelperUnmask, kHelperRecover };

    volatile bool ready {false};
    unsigned long currentPowerState {1};
    int page {0};
    UInt8 *inputBuffer {nullptr};
    UInt16 inputLength {0};
    VoodooI2CDeviceNub *device_nub {nullptr};
    IOLock *page_mutex {nullptr};

    IOWorkLoop* work_loop {nullptr};
    IOCommandGate* command_gate {nullptr};
    IOTimerEventSource* interrupt_simulator {nullptr};
    IOInterruptEventSource* work_source {nullptr};   // provider-less, signalled by primaryInterrupt
    IOTimerEventSource* helper_timer {nullptr};

    bool interrupt_registered {false};
    bool polling_mode {false};
    volatile bool irq_masked {true};
    volatile UInt64 irq_timestamp {0};
    bool recovery_scheduled {false};
    bool in_reset {false};
    HelperAction helper_action {kHelperNone};
    const char *recovery_reason {""};

    // statistics (published as "RMII2C Stats")
    volatile UInt64 stat_irqs {0};
    UInt64 stat_reads {0}, stat_attn {0}, stat_empty {0}, stat_other {0}, stat_fail {0};
    UInt64 stat_recoveries {0}, stat_polling_fallbacks {0}, stat_sleeps {0}, stat_wakes {0};
    UInt32 consecutive_empty {0}, consecutive_fail {0};
    UInt32 peak_attn_per_sec {0}, peak_reads_per_sec {0};
    UInt32 window_attn {0}, window_reads {0};
    UInt64 window_start_ms {0}, last_publish_ms {0};
    UInt64 recovery_window_start_ms {0};
    UInt32 recovery_count_in_window {0};
    UInt8 last_other_id {0};
    char last_empty_bytes[48] {0};
    char last_recovery[96] {0};

    // GPIO pad diagnostics (Sunrise Point-LP community 0, read-only MMIO)
    enum { kPadBoot = 0, kPadSleep, kPadWakePre, kPadRearm, kPadWakePost, kPadStuck, kPadSlots };
    IOMemoryMap *pad_map {nullptr};
    volatile UInt8 *pad_base {nullptr};
    int pad_pin {-1};
    char pad_snap[kPadSlots][176] {};
    UInt64 wake_done_ms {0};
    UInt64 stuck_after_wake_ms {0};
    bool stuck_seen_since_wake {false};
    UInt64 stat_hid_cmd_fail {0};
    UInt64 stat_rearms {0}, stat_rearm_fail {0};
    UInt64 last_attn_ms {0};
    UInt32 poll_interval_ms {INTERRUPT_SIMULATOR_TIMEOUT};

    void setupPadDiagnostics();
    void snapshotPad(int slot);
    bool rearmPad();

    void primaryInterrupt(void *refcon, IOService *nub, int source);
    void interruptOccured(OSObject* owner, IOInterruptEventSource* src, int intCount);
    void simulateInterrupt(OSObject* owner, IOTimerEventSource* timer);
    void helperTimerFired(OSObject* owner, IOTimerEventSource* timer);
    IOReturn setPowerStateGated();

    bool processInput(bool *keepMasked);
    void maskInterrupt();
    void unmaskInterrupt();
    void scheduleUnmask(UInt32 ms);
    void scheduleRecovery(const char *reason);
    void performRecovery();
    bool switchToPolling(const char *why);
    void leavePolling();
    void drainInput(int maxReads);
    int i2c_hid_command(UInt8 opcode, UInt8 arg);
    void publishStats();
    static UInt64 nowMs();

    UInt16 wHIDDescRegister {RMI_HID_DESC_REGISTER};
    i2c_hid_desc hdesc;
    IOReturn getHIDDescriptorAddress();
    IOReturn getHIDDescriptor();
    int rmi_set_page(UInt8 page);
    int rmi_set_mode(UInt8 mode);
    int rmi_set_mode_retry(int attempts, UInt32 delayMs);
    void releaseResources();
    void startInterrupt();
    void stopInterrupt();
};

#endif /* RMII2C_h */
