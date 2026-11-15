// LidWatch: lid-event watchdog for a Lenovo Yoga 920 (80Y7) running macOS via OpenCore.
//
// The embedded controller reports lid changes with ACPI query _Q0C, which updates LIDS and
// notifies LID0; AppleACPILid turns that into Clamshell Sleep. If that single EC event is lost,
// macOS never learns that the lid closed and the machine stays awake with the lid shut.
// LidWatch polls the EC's live lid bit every PollIntervalMs through SSDT-LIDW's LWRD method and
// compares it with both the state ACPI last reported (LIDS) and macOS's own clamshell state
// (IOPMrootDomain "AppleClamshellState"). When either disagrees for ConfirmPolls polls in a row,
// it calls LWFX, which replays exactly what _Q0C does (at most MaxReplays times per mismatch, so a
// replay that does not take can never turn into a notify loop). Polling pauses from "will sleep"
// until the system is back on, and resumes WakeDelayMs after each wake.
//
// Counters (no sudo needed):  ioreg -rc LidWatch -k "LidWatch Stats" | grep Stats
// Self-test (lid open):       set property SelfTest=true on LidWatch -> expect one "open" replay
#include <IOKit/IOService.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOMessage.h>
#include <IOKit/IOLib.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include <IOKit/pwr_mgt/RootDomain.h>
#include <kern/clock.h>

#define LOG(fmt, ...) IOLog("LidWatch: " fmt "\n", ##__VA_ARGS__)

class LidWatch : public IOService {
    OSDeclareDefaultStructors(LidWatch)

public:
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    IOReturn setProperties(OSObject *properties) override;

private:
    IOACPIPlatformDevice *acpi {nullptr};
    IOWorkLoop *loop {nullptr};
    IOTimerEventSource *timer {nullptr};
    IOCommandGate *gate {nullptr};
    IONotifier *pmNotifier {nullptr};

    UInt32 pollMs {2000}, confirmPolls {2}, startDelayMs {20000}, wakeDelayMs {5000}, maxReplays {3};
    bool asleep {false};
    UInt32 run {0};      // consecutive polls that saw a mismatch
    UInt32 streak {0};   // replays done for the current, still unresolved mismatch
    UInt32 live {0xFF};  // last live EC lid bit: 0 closed, 1 open, 0xFF unknown
    UInt32 osLid {0xFF}; // macOS clamshell state: 0 closed, 1 open, 0xFF unknown
    UInt64 polls {0}, unknownReads {0}, mismatchPolls {0}, replays {0}, replayedCloses {0}, gaveUp {0},
        selfTests {0}, wakes {0};
    char lastReplay[96] {"never"};

    void poll(IOTimerEventSource *sender);
    void publish();
    void teardown();
    UInt32 numberProperty(const char *key, UInt32 def, UInt32 lo, UInt32 hi);
    static IOReturn pmHandler(void *target, void *refCon, UInt32 messageType, IOService *provider,
                              void *messageArgument, vm_size_t argSize);
    static IOReturn gatedPower(OSObject *owner, void *goingToSleep, void *, void *, void *);
    static IOReturn gatedSelfTest(OSObject *owner, void *, void *, void *, void *);
};

OSDefineMetaClassAndStructors(LidWatch, IOService)

UInt32 LidWatch::numberProperty(const char *key, UInt32 def, UInt32 lo, UInt32 hi) {
    OSNumber *n = OSDynamicCast(OSNumber, getProperty(key));
    if (!n) return def;
    UInt32 v = n->unsigned32BitValue();
    return (v < lo || v > hi) ? def : v;
}

bool LidWatch::start(IOService *provider) {
    if (!IOService::start(provider)) return false;
    acpi = OSDynamicCast(IOACPIPlatformDevice, provider);
    if (!acpi) {
        LOG("provider is not an ACPI device");
        return false;
    }
    if (acpi->validateObject("LWRD") != kIOReturnSuccess || acpi->validateObject("LWFX") != kIOReturnSuccess) {
        LOG("SSDT-LIDW methods LWRD/LWFX not found under LID0, not starting");
        return false;
    }
    pollMs = numberProperty("PollIntervalMs", 2000, 500, 60000);
    confirmPolls = numberProperty("ConfirmPolls", 2, 1, 10);
    startDelayMs = numberProperty("StartDelayMs", 20000, 1000, 600000);
    wakeDelayMs = numberProperty("WakeDelayMs", 5000, 500, 600000);
    maxReplays = numberProperty("MaxReplays", 3, 1, 20);

    loop = IOWorkLoop::workLoop();
    timer = IOTimerEventSource::timerEventSource(
        this, OSMemberFunctionCast(IOTimerEventSource::Action, this, &LidWatch::poll));
    gate = IOCommandGate::commandGate(this);
    if (!loop || !timer || !gate || loop->addEventSource(timer) != kIOReturnSuccess ||
        loop->addEventSource(gate) != kIOReturnSuccess) {
        LOG("cannot set up the work loop");
        teardown();
        return false;
    }
    pmNotifier = registerPrioritySleepWakeInterest(&LidWatch::pmHandler, this, nullptr);
    if (!pmNotifier) {
        LOG("cannot register for sleep/wake notifications");
        teardown();
        return false;
    }
    publish();
    timer->setTimeoutMS(startDelayMs);
    registerService();
    LOG("started: poll every %u ms, replay after %u mismatching polls", pollMs, confirmPolls);
    return true;
}

void LidWatch::teardown() {
    if (pmNotifier) {
        pmNotifier->remove();
        pmNotifier = nullptr;
    }
    if (timer) {
        timer->cancelTimeout();
        if (loop) loop->removeEventSource(timer);
        timer->release();
        timer = nullptr;
    }
    if (gate) {
        if (loop) loop->removeEventSource(gate);
        gate->release();
        gate = nullptr;
    }
    if (loop) {
        loop->release();
        loop = nullptr;
    }
}

void LidWatch::stop(IOService *provider) {
    teardown();
    IOService::stop(provider);
}

void LidWatch::poll(IOTimerEventSource *) {
    if (asleep) return;
    bool changed = false;
    UInt32 v = 0xFF;
    polls++;

    // macOS's own view of the lid, as AppleACPILid last reported it to the power manager.
    UInt32 os = 0xFF;
    IOPMrootDomain *rd = getPMRootDomain();
    OSObject *prop = rd ? rd->copyProperty("AppleClamshellState") : nullptr;
    OSBoolean *cs = OSDynamicCast(OSBoolean, prop);
    if (cs) os = cs->isTrue() ? 0 : 1;
    if (prop) prop->release();
    if (os != osLid) changed = true;
    osLid = os;

    if (acpi->evaluateInteger("LWRD", &v) != kIOReturnSuccess || v == 0xFF) {
        unknownReads++;
        run = 0;
        changed = true;
    } else {
        if ((v & 1) != live) changed = true;
        live = v & 1;
        bool mismatch = (v & 2) || (os != 0xFF && os != live);
        if (mismatch) {
            mismatchPolls++;
            changed = true;
            if (++run >= confirmPolls) {
                run = 0;
                if (streak < maxReplays) {
                    streak++;
                    UInt32 s = 0xFF;
                    IOReturn r = acpi->evaluateInteger("LWFX", &s);
                    replays++;
                    if (r == kIOReturnSuccess && s == 0) replayedCloses++;
                    clock_sec_t secs = 0;
                    clock_usec_t usecs = 0;
                    clock_get_calendar_microtime(&secs, &usecs);
                    snprintf(lastReplay, sizeof lastReplay, "%s at unix time %lu (macOS had %s)%s",
                             s == 0 ? "closed" : s == 1 ? "open" : "unknown", (unsigned long)secs,
                             os == 0 ? "closed" : os == 1 ? "open" : "unknown",
                             r == kIOReturnSuccess ? "" : ", LWFX failed");
                    LOG("missed lid event: EC says %s, macOS had %s; replayed it", s == 0 ? "closed" : "open",
                        os == 0 ? "closed" : os == 1 ? "open" : "unknown");
                } else if (streak == maxReplays) {
                    streak++;
                    gaveUp++;
                    LOG("lid mismatch persists after %u replays, leaving it alone until it clears", maxReplays);
                }
            }
        } else {
            run = 0;
            streak = 0;
        }
    }
    if (changed || polls % 30 == 0) publish();
    if (!asleep) timer->setTimeoutMS(pollMs);
}

void LidWatch::publish() {
    OSDictionary *d = OSDictionary::withCapacity(10);
    if (!d) return;
    const struct { const char *key; UInt64 value; } counters[] = {
        {"Polls", polls},
        {"Unknown reads", unknownReads},
        {"Mismatch polls", mismatchPolls},
        {"Replayed events", replays},
        {"Replayed closes", replayedCloses},
        {"Gave up", gaveUp},
        {"Self tests", selfTests},
        {"Wakes", wakes},
    };
    for (const auto &c : counters) {
        OSNumber *n = OSNumber::withNumber(c.value, 64);
        if (n) {
            d->setObject(c.key, n);
            n->release();
        }
    }
    OSString *s = OSString::withCString(live == 0 ? "closed" : live == 1 ? "open" : "unknown");
    if (s) {
        d->setObject("EC lid", s);
        s->release();
    }
    s = OSString::withCString(osLid == 0 ? "closed" : osLid == 1 ? "open" : "unknown");
    if (s) {
        d->setObject("macOS lid", s);
        s->release();
    }
    s = OSString::withCString(lastReplay);
    if (s) {
        d->setObject("Last replay", s);
        s->release();
    }
    setProperty("LidWatch Stats", d);
    d->release();
}

IOReturn LidWatch::pmHandler(void *target, void *, UInt32 messageType, IOService *, void *messageArgument,
                             vm_size_t) {
    LidWatch *self = static_cast<LidWatch *>(target);
    IOPowerStateChangeNotification *n = static_cast<IOPowerStateChangeNotification *>(messageArgument);
    switch (messageType) {
    case kIOMessageSystemWillSleep:
        if (self && self->gate) self->gate->runAction(&LidWatch::gatedPower, (void *)1);
        if (n) n->returnValue = 0;
        break;
    case kIOMessageSystemWillNotSleep:
    case kIOMessageSystemHasPoweredOn:
        if (self && self->gate) self->gate->runAction(&LidWatch::gatedPower, (void *)0);
        if (n) n->returnValue = 0;
        break;
    case kIOMessageCanSystemSleep:
    case kIOMessageSystemWillPowerOn:
        if (n) n->returnValue = 0;
        break;
    default:
        break;
    }
    return kIOReturnSuccess;
}

IOReturn LidWatch::gatedPower(OSObject *owner, void *goingToSleep, void *, void *, void *) {
    LidWatch *self = OSDynamicCast(LidWatch, owner);
    if (!self || !self->timer) return kIOReturnBadArgument;
    self->run = 0;
    self->streak = 0;
    if (goingToSleep) {
        self->asleep = true;
        self->timer->cancelTimeout();
    } else {
        if (self->asleep) self->wakes++;
        self->asleep = false;
        self->timer->setTimeoutMS(self->wakeDelayMs);
    }
    return kIOReturnSuccess;
}

IOReturn LidWatch::setProperties(OSObject *properties) {
    OSDictionary *d = OSDynamicCast(OSDictionary, properties);
    if (!d) return kIOReturnBadArgument;
    if (d->getObject("SelfTest") == kOSBooleanTrue) {
        return gate ? gate->runAction(&LidWatch::gatedSelfTest) : kIOReturnNotReady;
    }
    return kIOReturnUnsupported;
}

IOReturn LidWatch::gatedSelfTest(OSObject *owner, void *, void *, void *, void *) {
    LidWatch *self = OSDynamicCast(LidWatch, owner);
    if (!self || self->asleep) return kIOReturnNotReady;
    UInt32 armed = 0;
    IOReturn r = self->acpi->evaluateInteger("LWTS", &armed);
    if (r != kIOReturnSuccess) return r;
    if (!armed) return kIOReturnNotPermitted;  // lid closed or EC not ready
    self->selfTests++;
    self->publish();
    LOG("self-test armed: reported lid state set to closed, expecting an open replay");
    return kIOReturnSuccess;
}
