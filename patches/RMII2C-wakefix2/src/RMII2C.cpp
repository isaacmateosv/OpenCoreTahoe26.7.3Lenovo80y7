/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021 Zhen
 * Ported to macOS from linux kernel, original source at
 * https://github.com/torvalds/linux/blob/master/drivers/hid/hid-rmi.c
 * https://github.com/torvalds/linux/blob/master/drivers/input/rmi4/rmi_i2c.c (depreciated?)
 * and code written by Alexandre, CoolStar and Kishor Prins
 * https://github.com/VoodooI2C/VoodooI2CSynaptics
 *
 * Copyright (c) 2011-2016 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 *
 * ---------------------------------------------------------------------------
 * Yoga 920 patch ("wake fix 1"), based on VoodooRMI 1.4.3 (83868f0).
 *
 * Measured on a Lenovo Yoga 920 (SYNA2B31, TPL1 on I2C1, GPIO pad GPP_B3,
 * GpioInt(Level, ActiveLow)): after lid sleep the trackpad line stays asserted
 * while reads return no report. Upstream then reads back-to-back (~610/s,
 * workloop 100% busy) and takes ~60,000 primary interrupts/s, so real touch
 * reports queue behind empty reads (post-wake lag).
 *
 * Root causes and fixes:
 *  1. VoodooGPIO reports the pad as edge-triggered until registerInterrupt()
 *     has run, so an IOInterruptEventSource never masks the level line.
 *     -> Register a primary handler directly and do level one-shot handling:
 *        mask in primary context, unmask from the workloop after the read.
 *  2. setPowerState() used attemptAction(), which silently does nothing while
 *     the workloop is busy, leaving the line armed through sleep (the sleep-
 *     entry storm of "Unable to read interrupt data" after I2C1 went to D3).
 *     -> Mask first without the gate, then runAction().
 *  3. No I2C-HID power management (Linux i2c-hid sends SET_POWER(SLEEP) on
 *     suspend and SET_POWER(ON) on resume).  -> Added.
 *  4. A stuck line was never detected.  -> After 64 reads in a row without a
 *     report: power-on + reconfigure, then I2C-HID RESET, then fall back to
 *     5 ms polling until the next wake.
 *  5. readBlock() called reset() with page_mutex held; reset() reconfigures
 *     through readBlock()/blockWrite() -> recursive lock.  -> Unlock first.
 *  6. Hardening: report length is validated before handleAttention(); input
 *     buffer freed with delete[]; workloop reference retained.
 * Counters are published in IORegistry as "RMII2C Stats".
 *
 * Wake fix 2: the first lid test showed the line still stuck after wake even
 * after three I2C-HID resets (reads return 00 00 = no report), so the pad, not
 * the touchpad, is suspect.
 *  7. Re-register the GPIO interrupt on every wake (and in recovery) so VoodooGPIO
 *     reprograms the pad (RXEVCFG/RXINV/PMODE + host ownership) like at boot.
 *  8. Read-only snapshots of the pad registers (Sunrise Point-LP community 0:
 *     PADCFG0/1, PAD_OWN, HOSTSW_OWN, PADCFGLOCK, GPI_IS/IE, raw RX level) at
 *     boot / sleep / wake / re-arm / stuck, published in "RMII2C Stats".
 *  9. I2C-HID SET_POWER(ON) is retried once on failure (as Linux does).
 * 10. Polling fallback is adaptive (2 ms while a finger is down, 10 ms idle)
 *     so gestures are far less jittery than with the fixed 5 ms poll.
 * ---------------------------------------------------------------------------
 */

#include "RMII2C.hpp"
#include "RMILogging.h"
#include "RMIConfiguration.hpp"
#include "RMIPowerStates.h"
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include <IOKit/IODeviceMemory.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSString.h>
#include <libkern/c++/OSDictionary.h>

OSDefineMetaClassAndStructors(RMII2C, RMITransport)

// mach absolute time is in nanoseconds on x86_64 (timebase 1/1).
UInt64 RMII2C::nowMs() {
    return mach_absolute_time() / 1000000ULL;
}

RMII2C *RMII2C::probe(IOService *provider, SInt32 *score) {
    int error = 0, attempts = 0;

    if (!super::probe(provider, score)) {
        IOLogError("%s Failed to probe provider", getName());
        return NULL;
    }

    name = provider->getName();
    IOLogDebug("%s::%s probing", getName(), name);

    device_nub = OSDynamicCast(VoodooI2CDeviceNub, provider);
    if (!device_nub) {
        IOLogError("%s::%s Could not cast nub", getName(), name);
        return NULL;
    }

    if (getHIDDescriptorAddress() != kIOReturnSuccess)
        IOLogInfo("%s::%s Could not get HID descriptor address\n", getName(), name);

    if (getHIDDescriptor() != kIOReturnSuccess) {
        IOLogError("%s::%s Could not get valid HID descriptor", getName(), name);
        return NULL;
    }

    if (hdesc.wVendorID != SYNAPTICS_VENDOR_ID) {
        IOLogDebug("%s::%s Skip vendor %x", getName(), name, hdesc.wVendorID);
        return NULL;
    }

    do {
        IOLogDebug("%s::%s Trying to set mode, attempt %d", getName(), name, attempts);
        error = rmi_set_mode(RMI_MODE_ATTN_REPORTS);
        IOSleep(500);
    } while (error < 0 && attempts++ < 5);

    if (error < 0) {
        IOLogError("%s::%s Failed to set mode", getName(), name);
        return NULL;
    }

    if (!page_mutex)
        page_mutex = IOLockAlloc();
    if (!page_mutex)
        return NULL;

    IOLockLock(page_mutex);
    /*
     * Setting the page to zero will (a) make sure the PSR is in a
     * known state, and (b) make sure we can talk to the device.
     */
    error = rmi_set_page(0);
    IOLockUnlock(page_mutex);
    if (error) {
        IOLogError("%s::%s Failed to set page select to 0", getName(), name);
        return NULL;
    }
    return this;
}

bool RMII2C::start(IOService *provider) {
    UInt16 bufferSize;

    if(!super::start(provider))
        return false;

    work_loop = getWorkLoop();
    if (!work_loop) {
        IOLogError("%s::%s Could not get work loop", getName(), name);
        goto exit;
    }
    work_loop->retain();

    command_gate = IOCommandGate::commandGate(this);
    if (!command_gate || (work_loop->addEventSource(command_gate) != kIOReturnSuccess)) {
        IOLogError("%s::%s Could not open command gate", getName(), name);
        goto exit;
    }

    // Provider-less event source: primaryInterrupt() signals it after masking the line.
    work_source = IOInterruptEventSource::interruptEventSource(this, OSMemberFunctionCast(IOInterruptEventAction, this, &RMII2C::interruptOccured));
    if (!work_source || (work_loop->addEventSource(work_source) != kIOReturnSuccess)) {
        IOLogError("%s::%s Could not create interrupt work source", getName(), name);
        goto exit;
    }

    helper_timer = IOTimerEventSource::timerEventSource(this, OSMemberFunctionCast(IOTimerEventSource::Action, this, &RMII2C::helperTimerFired));
    if (!helper_timer || (work_loop->addEventSource(helper_timer) != kIOReturnSuccess)) {
        IOLogError("%s::%s Could not create helper timer", getName(), name);
        goto exit;
    }

    inputLength = hdesc.wMaxInputLength;
    if (inputLength < 4) {
        IOLogError("%s::%s Invalid wMaxInputLength %u, using 64", getName(), name, inputLength);
        inputLength = 64;
    }
    bufferSize = inputLength < 64 ? 64 : inputLength;
    inputBuffer = new UInt8[bufferSize];
    if (!inputBuffer)
        goto exit;
    memset(inputBuffer, 0, bufferSize);

    if (device_nub->registerInterrupt(0, this, OSMemberFunctionCast(IOInterruptAction, this, &RMII2C::primaryInterrupt), nullptr) == kIOReturnSuccess) {
        interrupt_registered = true;   // registered lines start disabled
        irq_masked = true;
    } else {
        IOLogInfo("%s::%s Could not get interrupt event source, falling back to polling", getName(), name);
        if (!switchToPolling("no interrupt available"))
            goto exit;
    }

    setupPadDiagnostics();
    startInterrupt();
    snapshotPad(kPadBoot);

    PMinit();
    provider->joinPMtree(this);
    registerPowerDriver(this, RMIPowerStates, 2);

    setProperty("Interrupt mode", interrupt_registered ? "Pinned" : "Polling");
    setProperty("VoodooI2CServices Supported", kOSBooleanTrue);
    setProperty(RMIBusSupported, kOSBooleanTrue);
    setProperty("RMII2C Patch", "Yoga 920 wake fix 2: level one-shot IRQ, I2C-HID power, pad re-arm on wake, stuck-line recovery, pad diagnostics");
    publishStats();
    registerService();
    return true;
exit:
    releaseResources();
    return false;
}

void RMII2C::releaseResources() {
    stopInterrupt();

    if (interrupt_registered && device_nub) {
        device_nub->unregisterInterrupt(0);
        interrupt_registered = false;
    }

    if (work_loop) {
        if (command_gate)
            work_loop->removeEventSource(command_gate);
        if (work_source)
            work_loop->removeEventSource(work_source);
        if (helper_timer) {
            helper_timer->cancelTimeout();
            work_loop->removeEventSource(helper_timer);
        }
        if (interrupt_simulator) {
            interrupt_simulator->cancelTimeout();
            work_loop->removeEventSource(interrupt_simulator);
        }
    }

    if (device_nub) {
        if (device_nub->isOpen(this))
            device_nub->close(this);
        device_nub = nullptr;
    }

    if (inputBuffer) {
        delete[] inputBuffer;
        inputBuffer = nullptr;
    }

    pad_base = nullptr;
    pad_pin = -1;
    OSSafeReleaseNULL(pad_map);

    OSSafeReleaseNULL(command_gate);
    OSSafeReleaseNULL(work_source);
    OSSafeReleaseNULL(helper_timer);
    OSSafeReleaseNULL(interrupt_simulator);
    OSSafeReleaseNULL(work_loop);

    if (page_mutex) {
        IOLockFree(page_mutex);
        page_mutex = nullptr;
    }
}

void RMII2C::stop(IOService *provider) {
    PMstop();
    releaseResources();
    super::stop(provider);
}

int RMII2C::rmi_set_page(UInt8 page) {
    /*
     * simplified version of rmi_write_report, hid_hw_output_report, i2c_hid_output_report,
     * i2c_hid_output_raw_report, i2c_hid_set_or_send_report and __i2c_hid_command
     */
    UInt8 writeReport[] = {
        (UInt8) (hdesc.wOutputRegister & 0xFF),
        (UInt8) (hdesc.wOutputRegister >> 8),
        0x06,  // size & 0xFF
        0x00,  // size >> 8
        RMI_WRITE_REPORT_ID,
        0x01,
        RMI_PAGE_SELECT_REGISTER,
        page
    };

    if (device_nub->writeI2C(writeReport, sizeof(writeReport)) != kIOReturnSuccess) {
        IOLogError("%s::%s failed to write request output report", getName(), name);
        return -1;
    }

    this->page = page;
    return 0;
}

IOReturn RMII2C::getHIDDescriptorAddress() {
    IOReturn ret;
    OSObject *obj = nullptr;

    ret = device_nub->evaluateDSM(I2C_DSM_HIDG, HIDG_DESC_INDEX, &obj);
    if (ret == kIOReturnSuccess) {
        OSNumber *number = OSDynamicCast(OSNumber, obj);
        if (number != nullptr) {
            wHIDDescRegister = number->unsigned16BitValue();
            setProperty("HIDDescriptorAddress", wHIDDescRegister, 16);
        } else {
            IOLogInfo("%s::%s HID descriptor address invalid\n", getName(), name);
            ret = kIOReturnInvalid;
        }
    } else {
        IOLogInfo("%s::%s unable to parse HID descriptor address\n", getName(), name);
        ret = kIOReturnNotFound;
    }
    if (obj) obj->release();
    return ret;
}

IOReturn RMII2C::getHIDDescriptor() {
    UInt8 command[] = {
        (UInt8) (wHIDDescRegister & 0xFF),
        (UInt8) (wHIDDescRegister >> 8) };

    if (device_nub->writeReadI2C(command, sizeof(command), (UInt8 *)&hdesc, sizeof(i2c_hid_desc)) != kIOReturnSuccess) {
        IOLogError("%s::%s Read descriptor from 0x%02x failed", getName(), name, wHIDDescRegister);
        return kIOReturnError;
    }

    if (hdesc.bcdVersion != 0x0100) {
        IOLogError("%s::%s BCD version %d mismatch", getName(), name, hdesc.bcdVersion);
        return kIOReturnInvalid;
    }

    if (hdesc.wHIDDescLength != sizeof(i2c_hid_desc)) {
        IOLogError("%s::%s descriptor length %d mismatch", getName(), name, hdesc.wHIDDescLength);
        return kIOReturnInvalid;
    }

    setProperty("VendorID", hdesc.wVendorID, 16);
    setProperty("ProductID", hdesc.wProductID, 16);
    setProperty("VersionID", hdesc.wVersionID, 16);

    return kIOReturnSuccess;
}

int RMII2C::rmi_set_mode(UInt8 mode) {
    UInt8 command[] = {
        (UInt8) (hdesc.wCommandRegister & 0xFF),
        (UInt8) (hdesc.wCommandRegister >> 8),
        RMI_SET_RMI_MODE_REPORT_ID + (0x3 << 4), // reportID | reportType << 4;
              // reportType: 0x03 for HID_FEATURE_REPORT (kIOHIDReportTypeFeature) ; 0x02 for HID_OUTPUT_REPORT (kIOHIDReportTypeOutput)
        0x03, // hid_set_report_cmd =    { I2C_HID_CMD(0x03) };
        RMI_SET_RMI_MODE_REPORT_ID, // report_id
        (UInt8) (hdesc.wDataRegister & 0xFF),
        (UInt8) (hdesc.wDataRegister >> 8),
        0x04, // size & 0xFF; 2 + reportID + buf (reportID excluded)
        0x00, // size >> 8;
        RMI_SET_RMI_MODE_REPORT_ID, // report_id = buf[0];
        mode
    };

    if (device_nub->writeI2C(command, sizeof(command)) != kIOReturnSuccess)
        return -1;

    IOLogDebug("%s::%s mode set", getName(), name);

    return 1;
}

int RMII2C::rmi_set_mode_retry(int attempts, UInt32 delayMs) {
    int error = -1;
    for (int i = 0; i < attempts; i++) {
        error = rmi_set_mode(RMI_MODE_ATTN_REPORTS);
        if (error >= 0)
            break;
        IOSleep(delayMs);
    }
    return error;
}

// I2C-HID command register write: [reg lo, reg hi, arg, opcode]
int RMII2C::i2c_hid_command(UInt8 opcode, UInt8 arg) {
    UInt8 command[] = {
        (UInt8) (hdesc.wCommandRegister & 0xFF),
        (UInt8) (hdesc.wCommandRegister >> 8),
        arg,
        opcode
    };

    IOReturn ret = device_nub->writeI2C(command, sizeof(command));
    if (ret != kIOReturnSuccess && opcode == I2C_HID_OPCODE_SET_POWER && arg == I2C_HID_PWR_ON) {
        // Some devices NACK the first transfer while waking from deep sleep (Linux retries too).
        stat_hid_cmd_fail++;
        IOSleep(1);
        ret = device_nub->writeI2C(command, sizeof(command));
    }
    if (ret != kIOReturnSuccess) {
        stat_hid_cmd_fail++;
        IOLogError("%s::%s I2C-HID command 0x%x/%u failed (0x%x)", getName(), name, opcode, arg, ret);
        return -1;
    }
    return 0;
}

int RMII2C::reset() {
    int retval = rmi_set_mode(RMI_MODE_ATTN_REPORTS);

    if (retval < 0)
        return retval;

    if (currentPowerState)
        ready = true;

    // Tell driver to reconfigure
    retval = messageClient(kIOMessageRMI4ResetHandler, bus);

    IOLogInfo("%s::%s reset completed", getName(), name);
    return retval;
};

bool RMII2C::open(IOService *client, IOOptionBits options, RMIAttentionAction action) {
    if (!super::open(client, options, action)) {
        return false;
    }

    startInterrupt();
    return true;
}

int RMII2C::readBlock(UInt16 rmiaddr, UInt8 *databuff, size_t len) {
    int retval = 0;
    bool needReset = false;

    if (hdesc.wMaxInputLength && (len > hdesc.wMaxInputLength))
        len = hdesc.wMaxInputLength;

    UInt8 writeReport[] = {
        (UInt8) (hdesc.wOutputRegister & 0xFF),
        (UInt8) (hdesc.wOutputRegister >> 8),
        0x08,  // size & 0xFF; 2 + reportID + buf (reportID excluded)
        0x00,  // size >> 8;
        RMI_READ_ADDR_REPORT_ID,
        0x00,  // old 1 byte read count
        (UInt8) (rmiaddr & 0xFF),
        (UInt8) (rmiaddr >> 8),
        (UInt8) (len & 0xFF),
        (UInt8) (len >> 8) };

    UInt8 *i2cInput = new UInt8[len+4];
    memset(databuff, 0, len);

    IOLockLock(page_mutex);
    if (RMI_I2C_PAGE(rmiaddr) != page) {
        retval = rmi_set_page(RMI_I2C_PAGE(rmiaddr));
        if (retval < 0)
            goto exit;
    }

    if (device_nub->writeReadI2C(writeReport, sizeof(writeReport), i2cInput, len+4) != kIOReturnSuccess) {
        IOLogError("%s::%s failed to read I2C input", getName(), name);
        retval = -1;
        goto exit;
    }

    if (i2cInput[2] != RMI_READ_DATA_REPORT_ID) {
        IOLogError("%s::%s RMI_READ_DATA_REPORT_ID mismatch %d", getName(), name, i2cInput[2]);
        if (i2cInput[2] == HID_GENERIC_MOUSE ||
            i2cInput[2] == HID_GENERIC_POINTER) {
            needReset = true;
        }
        retval = -1;
        goto exit;
    }

    memcpy(databuff, i2cInput+4, len);
exit:
    delete[] i2cInput;
    IOLockUnlock(page_mutex);

    // reset() reconfigures every function through readBlock()/blockWrite(),
    // so it must run without page_mutex held (upstream deadlocked here).
    if (needReset && !in_reset) {
        in_reset = true;
        if (reset() < 0)
            IOLogError("Failed to reset trackpad after report id mismatch!");
        in_reset = false;
    }
    return retval;
}

int RMII2C::blockWrite(UInt16 rmiaddr, UInt8 *buf, size_t len) {
    int retval = 0;

    if (hdesc.wMaxOutputLength && (len + 6 > hdesc.wMaxOutputLength))
        setProperty("InputLength exceed", len);

    UInt8 *writeReport = new UInt8[len+8] {
        (UInt8) (hdesc.wOutputRegister & 0xFF),
        (UInt8) (hdesc.wOutputRegister >> 8),
        (UInt8) ((len + 6) & 0xFF),  // size & 0xFF; 2 + reportID + buf (reportID excluded)
        (UInt8) ((len + 6) >> 8),  // size >> 8;
        RMI_WRITE_REPORT_ID,
        (UInt8) len,
        (UInt8) (rmiaddr & 0xFF),
        (UInt8) (rmiaddr >> 8) };

    IOLockLock(page_mutex);
    if (RMI_I2C_PAGE(rmiaddr) != page) {
        retval = rmi_set_page(RMI_I2C_PAGE(rmiaddr));
        if (retval < 0)
            goto exit;
    }

    memcpy(writeReport+8, buf, len);

    if (device_nub->writeI2C(writeReport, len+8) != kIOReturnSuccess) {
        IOLogError("%s::%s failed to write request output report", getName(), name);
        retval = -1;
        goto exit;
    }
    retval = 0;

exit:
    delete [] writeReport;
    IOLockUnlock(page_mutex);
    return retval;
}

// ---------------------------------------------------------------------------
// Interrupt handling
// ---------------------------------------------------------------------------

// Primary interrupt context. Mask the level-triggered line until the workloop
// has read the pending report (one-shot), then hand off to the workloop.
void RMII2C::primaryInterrupt(void *refcon, IOService *nub, int source) {
    VoodooI2CDeviceNub *nubp = device_nub;
    if (nubp)
        nubp->disableInterrupt(0);
    irq_masked = true;
    irq_timestamp = mach_absolute_time();
    stat_irqs++;
    if (work_source)
        work_source->interruptOccurred(nullptr, nullptr, 0);
}

void RMII2C::maskInterrupt() {
    if (!interrupt_registered || !device_nub)
        return;
    irq_masked = true;
    device_nub->disableInterrupt(0);
}

void RMII2C::unmaskInterrupt() {
    if (!interrupt_registered || !device_nub || polling_mode)
        return;
    irq_masked = false;
    device_nub->enableInterrupt(0);
}

// We are in the workloop (not interrupt context), it's OK to use IOLog, messageClient, etc
void RMII2C::interruptOccured(OSObject *owner, IOInterruptEventSource *src, int intCount) {
    // While not ready (sleep, early start) or while a recovery is pending the line
    // stays masked; startInterrupt() / performRecovery() re-arm it.
    if (!ready || !bus || recovery_scheduled || polling_mode)
        return;

    bool keepMasked = false;
    processInput(&keepMasked);
    if (!keepMasked)
        unmaskInterrupt();
}

// Polling (fallback) mode: 2 ms while reports are flowing, 10 ms when idle, so the
// poll-time timestamps jitter far less than with upstream's fixed 5 ms period.
void RMII2C::simulateInterrupt(OSObject* owner, IOTimerEventSource* timer) {
    UInt32 next = INTERRUPT_SIMULATOR_TIMEOUT;
    if (ready && bus) {
        bool keepMasked = false;
        if (processInput(&keepMasked))
            last_attn_ms = nowMs();
        next = (nowMs() - last_attn_ms < 200) ? 2 : 10;
    }
    poll_interval_ms = next;
    if (polling_mode && interrupt_simulator)
        interrupt_simulator->setTimeoutMS(next);
}

// Reads one input report. Returns true if an attention report was delivered.
bool RMII2C::processInput(bool *keepMasked) {
    UInt64 now = nowMs();
    if (now - window_start_ms >= 1000) {
        if (window_attn > peak_attn_per_sec)
            peak_attn_per_sec = window_attn;
        if (window_reads > peak_reads_per_sec)
            peak_reads_per_sec = window_reads;
        window_attn = window_reads = 0;
        window_start_ms = now;
    }
    if (now - last_publish_ms >= RMI_STATS_PUBLISH_MS)
        publishStats();

    stat_reads++;
    window_reads++;

    IOReturn ret = device_nub->readI2C(inputBuffer, inputLength);
    if (ret != kIOReturnSuccess) {
        stat_fail++;
        consecutive_fail++;
        if (consecutive_fail <= 3 || (consecutive_fail % 500) == 0)
            IOLogError("%s::%s Unable to read interrupt data (0x%x, %u in a row)", getName(), name, ret, consecutive_fail);
        if (!polling_mode) {
            // Don't re-arm straight away on a dead bus; that is how a storm starts.
            *keepMasked = true;
            if (consecutive_fail >= RMI_FAIL_RECOVER_COUNT)
                scheduleRecovery("read failures");
            else
                scheduleUnmask(RMI_FAIL_BACKOFF_MS);
        }
        return false;
    }
    consecutive_fail = 0;

    UInt16 size = inputBuffer[0] | (inputBuffer[1] << 8);
    UInt8 reportId = inputBuffer[2];

    if (size < 4 || reportId != RMI_ATTN_REPORT_ID) {
        if (size == 0) {
            stat_empty++;
        } else {
            stat_other++;
            last_other_id = reportId;
        }
        snprintf(last_empty_bytes, sizeof(last_empty_bytes), "%02x %02x %02x %02x %02x %02x %02x %02x",
                 inputBuffer[0], inputBuffer[1], inputBuffer[2], inputBuffer[3],
                 inputBuffer[4], inputBuffer[5], inputBuffer[6], inputBuffer[7]);
        if (!polling_mode) {
            // With level one-shot handling the line only fires when the device
            // claims to have data, so a run of empty reads means it is stuck.
            consecutive_empty++;
            if (consecutive_empty >= RMI_STUCK_EMPTY_READS) {
                *keepMasked = true;
                scheduleRecovery("interrupt line stuck without data");
            }
        }
        return false;
    }

    consecutive_empty = 0;
    stat_attn++;
    window_attn++;

    // Never hand a length past what was actually read to the RMI bus.
    if (size > inputLength)
        size = inputLength;

    AbsoluteTime timestamp = (polling_mode || !irq_timestamp) ? mach_absolute_time() : irq_timestamp;
    handleAttention(timestamp, &inputBuffer[3], size - 3);
    return true;
}

void RMII2C::drainInput(int maxReads) {
    for (int i = 0; i < maxReads; i++) {
        if (device_nub->readI2C(inputBuffer, inputLength) != kIOReturnSuccess)
            break;
        UInt16 size = inputBuffer[0] | (inputBuffer[1] << 8);
        if (size == 0 || size == 0xffff)
            break;
    }
}

void RMII2C::scheduleUnmask(UInt32 ms) {
    if (recovery_scheduled || !helper_timer)
        return;
    helper_action = kHelperUnmask;
    helper_timer->setTimeoutMS(ms);
}

void RMII2C::scheduleRecovery(const char *reason) {
    if (recovery_scheduled || !helper_timer)
        return;
    if (!stuck_seen_since_wake) {
        stuck_seen_since_wake = true;
        stuck_after_wake_ms = wake_done_ms ? nowMs() - wake_done_ms : 0;
        snapshotPad(kPadStuck);
    }
    recovery_scheduled = true;
    recovery_reason = reason;
    helper_action = kHelperRecover;
    helper_timer->setTimeoutMS(10);
}

void RMII2C::helperTimerFired(OSObject* owner, IOTimerEventSource* timer) {
    HelperAction action = helper_action;
    helper_action = kHelperNone;

    switch (action) {
        case kHelperUnmask:
            if (ready && bus && !recovery_scheduled && !polling_mode)
                unmaskInterrupt();
            break;
        case kHelperRecover:
            performRecovery();
            break;
        default:
            break;
    }
}

void RMII2C::performRecovery() {
    if (!recovery_scheduled)
        return;

    if (!ready || !bus || currentPowerState == 0) {
        // The sleep/wake path re-initialises the device.
        recovery_scheduled = false;
        return;
    }

    UInt64 now = nowMs();
    if (!recovery_window_start_ms || now - recovery_window_start_ms > RMI_RECOVERY_WINDOW_MS) {
        recovery_window_start_ms = now;
        recovery_count_in_window = 0;
    }
    recovery_count_in_window++;
    stat_recoveries++;

    if (recovery_count_in_window > RMI_RECOVERY_MAX_IN_WINDOW) {
        snprintf(last_recovery, sizeof(last_recovery), "%s: still stuck after %d resets, polling until next wake",
                 recovery_reason, RMI_RECOVERY_MAX_IN_WINDOW);
        IOLogError("%s::%s %s", getName(), name, last_recovery);
        recovery_scheduled = false;
        switchToPolling("interrupt line stuck");
        publishStats();
        return;
    }

    bool hardReset = recovery_count_in_window > 1;
    snprintf(last_recovery, sizeof(last_recovery), "%s (%u empty, %u failed in a row) -> %s",
             recovery_reason, consecutive_empty, consecutive_fail,
             hardReset ? "I2C-HID reset" : "power on + reconfigure");
    IOLogInfo("%s::%s Recovering trackpad: %s", getName(), name, last_recovery);

    if (hardReset) {
        i2c_hid_command(I2C_HID_OPCODE_RESET, 0);
        IOSleep(300);
    }
    i2c_hid_command(I2C_HID_OPCODE_SET_POWER, I2C_HID_PWR_ON);
    IOSleep(hardReset ? 100 : 20);
    drainInput(8);

    if (rmi_set_mode_retry(5, 100) < 0)
        IOLogError("%s::%s Failed to set RMI mode during recovery", getName(), name);

    IOLockLock(page_mutex);
    page = 0xff;   // force a real page-select write
    rmi_set_page(0);
    IOLockUnlock(page_mutex);

    messageClient(kIOMessageRMI4ResetHandler, bus);
    drainInput(4);

    consecutive_empty = 0;
    consecutive_fail = 0;
    recovery_scheduled = false;
    if (!rearmPad() && polling_mode) {   // re-register failed -> already polling
        publishStats();
        return;
    }
    publishStats();
    unmaskInterrupt();
}

bool RMII2C::switchToPolling(const char *why) {
    maskInterrupt();

    if (!interrupt_simulator) {
        interrupt_simulator = IOTimerEventSource::timerEventSource(this, OSMemberFunctionCast(IOTimerEventSource::Action, this, &RMII2C::simulateInterrupt));
        if (!interrupt_simulator || (work_loop->addEventSource(interrupt_simulator) != kIOReturnSuccess)) {
            IOLogError("%s::%s Could not get timer event source", getName(), name);
            OSSafeReleaseNULL(interrupt_simulator);
            return false;
        }
    }

    polling_mode = true;
    if (interrupt_registered)
        stat_polling_fallbacks++;
    setProperty("Interrupt mode", interrupt_registered ? "Polling (stuck interrupt fallback)" : "Polling");
    IOLogInfo("%s::%s Using polling (%s)", getName(), name, why);

    if (ready) {
        interrupt_simulator->setTimeoutMS(INTERRUPT_SIMULATOR_TIMEOUT);
        interrupt_simulator->enable();
    }
    return true;
}

void RMII2C::leavePolling() {
    if (!polling_mode || !interrupt_registered)
        return;
    polling_mode = false;
    if (interrupt_simulator)
        interrupt_simulator->disable();
    setProperty("Interrupt mode", "Pinned");
}

void RMII2C::startInterrupt() {
    ready = true;
    if (polling_mode) {
        if (interrupt_simulator) {
            interrupt_simulator->setTimeoutMS(INTERRUPT_SIMULATOR_INTERVAL);
            interrupt_simulator->enable();
        }
    } else {
        unmaskInterrupt();
    }
}

void RMII2C::stopInterrupt() {
    ready = false;
    if (interrupt_simulator)
        interrupt_simulator->disable();
    maskInterrupt();
}

// ---------------------------------------------------------------------------
// Power management
// ---------------------------------------------------------------------------

IOReturn RMII2C::setPowerStateGated() {
    if (currentPowerState == 0) {
        snapshotPad(kPadSleep);
        stopInterrupt();
        if (helper_timer)
            helper_timer->cancelTimeout();
        helper_action = kHelperNone;
        recovery_scheduled = false;
        stat_sleeps++;

        messageClient(kIOMessageRMI4Sleep, bus);
        i2c_hid_command(I2C_HID_OPCODE_SET_POWER, I2C_HID_PWR_SLEEP);

        consecutive_empty = 0;
        consecutive_fail = 0;
        publishStats();
    } else {
        stat_wakes++;
        snapshotPad(kPadWakePre);
        leavePolling();   // give the interrupt line a fresh chance after every wake

        i2c_hid_command(I2C_HID_OPCODE_SET_POWER, I2C_HID_PWR_ON);

        // Upstream FIXME: the device needs ~1 s after resume before it accepts
        // reconfiguration. The line stays masked meanwhile, so nothing can storm.
        IOSleep(1000);
        drainInput(8);

        if (rmi_set_mode_retry(5, 200) < 0) {
            IOLogError("Failed to config trackpad!");
        }

        IOLockLock(page_mutex);
        page = 0xff;   // the device may have reset its page register
        rmi_set_page(0);
        IOLockUnlock(page_mutex);

        consecutive_empty = 0;
        consecutive_fail = 0;
        recovery_window_start_ms = 0;
        recovery_count_in_window = 0;

        IOReturn ret = messageClient(kIOMessageRMI4Resume, bus);
        if (ret != kIOReturnSuccess) {
            IOLogError("Failed to resume trackpad!");
        }

        drainInput(4);

        // Have VoodooGPIO reprogram the pad from scratch (type, polarity, ownership)
        // before the line is armed again.
        rearmPad();
        snapshotPad(kPadRearm);

        stuck_seen_since_wake = false;
        startInterrupt();
        IOSleep(2);
        snapshotPad(kPadWakePost);
        wake_done_ms = nowMs();
        publishStats();
    }

    return kIOPMAckImplied;
}

IOReturn RMII2C::setPowerState(unsigned long newPowerState, IOService *whatDevice) {
    IOLogDebug("%s::%s powerState %ld : %s", getName(), name, newPowerState, newPowerState ? "on" : "off");

    if (whatDevice != this)
        return kIOReturnInvalid;

    if (!bus || currentPowerState == newPowerState)
        return kIOPMAckImplied;

    currentPowerState = newPowerState;

    if (newPowerState == 0) {
        // Disarm first, without the gate, so a busy workloop can never leave
        // the line armed through sleep.
        ready = false;
        maskInterrupt();
    }

    // runAction() waits for the workloop; upstream's attemptAction() gave up.
    IOReturn ret = command_gate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &RMII2C::setPowerStateGated));

    if (ret) IOLogDebug("%s::%s powerState ret 0x%x", getName(), name, ret);
    return kIOPMAckImplied;
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

void RMII2C::publishStats() {
    last_publish_ms = nowMs();

    OSDictionary *d = OSDictionary::withCapacity(20);
    if (!d)
        return;

#define PUT_NUM(k, v) do { OSNumber *n_ = OSNumber::withNumber((unsigned long long)(v), 64); if (n_) { d->setObject(k, n_); n_->release(); } } while (0)
#define PUT_STR(k, s) do { OSString *s_ = OSString::withCString(s); if (s_) { d->setObject(k, s_); s_->release(); } } while (0)

    PUT_STR("Mode", polling_mode ? "polling" : (interrupt_registered ? "interrupt (level one-shot)" : "none"));
    PUT_NUM("Interrupts", stat_irqs);
    PUT_NUM("Reads", stat_reads);
    PUT_NUM("Attention reports", stat_attn);
    PUT_NUM("Empty reads", stat_empty);
    PUT_NUM("Other reports", stat_other);
    PUT_NUM("Last other report ID", last_other_id);
    PUT_NUM("Read failures", stat_fail);
    PUT_NUM("Recoveries", stat_recoveries);
    PUT_NUM("Polling fallbacks", stat_polling_fallbacks);
    PUT_NUM("Sleeps", stat_sleeps);
    PUT_NUM("Wakes", stat_wakes);
    PUT_NUM("Peak attention per second", peak_attn_per_sec);
    PUT_NUM("Peak reads per second", peak_reads_per_sec);
    PUT_NUM("Max input length", inputLength);
    PUT_STR("Last empty read", last_empty_bytes);
    PUT_STR("Last recovery", last_recovery);
    PUT_NUM("HID command failures", stat_hid_cmd_fail);
    PUT_NUM("Pad re-arms", stat_rearms);
    PUT_NUM("Pad re-arm failures", stat_rearm_fail);
    PUT_NUM("Stuck after wake ms", stuck_after_wake_ms);
    PUT_NUM("Polling interval ms", poll_interval_ms);
    PUT_NUM("HID wMaxInputLength", hdesc.wMaxInputLength);
    PUT_NUM("HID wCommandRegister", hdesc.wCommandRegister);
    PUT_NUM("GPIO pin", pad_pin);

    static const char * const padNames[kPadSlots] = {
        "Pad 1 boot", "Pad 2 sleep", "Pad 3 wake before re-arm",
        "Pad 4 after re-arm", "Pad 5 wake armed", "Pad 6 first stuck"
    };
    for (int i = 0; i < kPadSlots; i++) {
        if (pad_snap[i][0])
            PUT_STR(padNames[i], pad_snap[i]);
    }

#undef PUT_NUM
#undef PUT_STR

    setProperty("RMII2C Stats", d);
    d->release();
}

// ---------------------------------------------------------------------------
// GPIO pad handling
// ---------------------------------------------------------------------------

// Re-register the GPIO interrupt: VoodooI2CDeviceNub::registerInterrupt() calls
// VoodooGPIO::setInterruptTypeForPin(), which rewrites RXEVCFG/RXINV/PMODE in the
// pad config and takes host ownership, exactly as at boot.
bool RMII2C::rearmPad() {
    if (!interrupt_registered || !device_nub)
        return false;

    stat_rearms++;
    maskInterrupt();
    device_nub->unregisterInterrupt(0);
    interrupt_registered = false;

    IOReturn ret = device_nub->registerInterrupt(0, this, OSMemberFunctionCast(IOInterruptAction, this, &RMII2C::primaryInterrupt), nullptr);
    if (ret != kIOReturnSuccess) {
        stat_rearm_fail++;
        IOLogError("%s::%s Could not re-register interrupt (0x%x)", getName(), name, ret);
        switchToPolling("interrupt re-register failed");
        return false;
    }

    interrupt_registered = true;   // registered lines start disabled
    irq_masked = true;
    return true;
}

// Map GPI0 community 0 (Sunrise Point-LP: GPP_A/GPP_B, pads 0-47) read-only so the
// trackpad pad can be inspected. Only enabled for the layout this was written for.
void RMII2C::setupPadDiagnostics() {
    OSNumber *pinNumber = OSDynamicCast(OSNumber, device_nub->getProperty("gpioPin"));
    if (!pinNumber)
        return;
    int pin = (int) pinNumber->unsigned32BitValue();
    if (pin < 0 || pin > 47)
        return;

    OSDictionary *match = IOService::serviceMatching("VoodooGPIOSunrisePointLP");
    if (!match)
        return;
    IOService *gpio = IOService::waitForMatchingService(match, 100000000ULL);   // retained; VoodooGPIO is long up by now
    match->release();
    if (!gpio)
        return;

    IOService *acpi = gpio->getProvider();
    IODeviceMemory *mem = acpi ? acpi->getDeviceMemoryWithIndex(0) : nullptr;
    if (mem && mem->getLength() >= 0x1000) {
        pad_map = mem->map(kIOMapInhibitCache);
        if (pad_map) {
            pad_base = (volatile UInt8 *) pad_map->getVirtualAddress();
            pad_pin = pin;
        }
    }
    gpio->release();
}

void RMII2C::snapshotPad(int slot) {
    if (!pad_base || pad_pin < 0 || slot < 0 || slot >= kPadSlots)
        return;

    auto rd = [this](UInt32 off) -> UInt32 { return *(volatile UInt32 *)(pad_base + off); };

    UInt32 padbar = rd(0x00c) & 0xffff;   // PADBAR: offset of the pad config block
    if (padbar < 0x100 || padbar > 0xf000)
        padbar = 0x400;
    int grp = pad_pin / 24, bit = pad_pin % 24;   // SPT-LP pad groups are 24 wide

    UInt32 cfg0   = rd(padbar + pad_pin * 8);
    UInt32 cfg1   = rd(padbar + pad_pin * 8 + 4);
    UInt32 own    = (rd(0x020 + (pad_pin / 8) * 4) >> ((pad_pin % 8) * 4)) & 0xf;
    UInt32 lock   = (rd(0x0a0 + grp * 8) >> bit) & 1;
    UInt32 locktx = (rd(0x0a4 + grp * 8) >> bit) & 1;
    UInt32 hostsw = (rd(0x0d0 + grp * 4) >> bit) & 1;
    UInt32 is     = (rd(0x100 + grp * 4) >> bit) & 1;
    UInt32 ie     = (rd(0x120 + grp * 4) >> bit) & 1;

    snprintf(pad_snap[slot], sizeof(pad_snap[slot]),
             "cfg0=%08x cfg1=%08x rxline=%u inv=%u ev=%u raw1=%u pmode=%u rxdis=%u route=%x own=%u hostsw=%u lock=%u/%u is=%u ie=%u t=%llu",
             cfg0, cfg1, (cfg0 >> 1) & 1, (cfg0 >> 23) & 1, (cfg0 >> 25) & 3, (cfg0 >> 28) & 1,
             (cfg0 >> 10) & 7, (cfg0 >> 9) & 1, (cfg0 >> 17) & 0xf, own, hostsw, lock, locktx, is, ie,
             (unsigned long long) nowMs());
}

OSDictionary *RMII2C::createConfig() {
    OSDictionary *config = nullptr;
    OSArray *acpiArray = nullptr;
    OSObject *acpiReturn = nullptr;

    IOACPIPlatformDevice *acpi_device = OSDynamicCast(IOACPIPlatformDevice, device_nub->getProperty("acpi-device"));
    if (!acpi_device) {
        IOLogError("%s::%s Could not retrieve acpi device", getName(), name);
        return nullptr;
    };

    if (acpi_device->evaluateObject("RCFG", &acpiReturn) != kIOReturnSuccess) {
        return nullptr;
    }

    acpiArray = OSDynamicCast(OSArray, acpiReturn);
    config = Configuration::mapArrayToDict(acpiArray);
    OSSafeReleaseNULL(acpiReturn);

    return config;
}
