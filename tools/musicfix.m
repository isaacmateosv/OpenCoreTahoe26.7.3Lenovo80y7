// RETIRED 2026-11-20: with full SIP, launch constraints kill the sandboxed Music relaunch this tool relies on
// (CODESIGNING 4, Launch Constraint Violation). Apple Music is now fixed by patches/MusicGVA. Kept for reference.
// musicfix: keeps Apple Music playing on this Hackintosh (macOS 26 Tahoe, Intel UHD 620 on Apple's KBL driver).
//
// Why: Music's FairPlay first tries the Intel GPU's hardware key exchange (AppleIntelKBLGraphicsVADriver, PAVP via
// the Intel ME). On this machine that exchange fails ("Key request data generation FAILED ... err = -15842",
// ICError -7101), Music never falls back, and every song sits at 0:00 with no sound. When Music cannot read that
// driver, FairPlay uses its software path and playback works (verified 2026-11-15 on 26.7.3).
//
// What: this agent watches app launches. Any Music started the normal way (Dock, Spotlight, media keys, links) is
// replaced at once by the same app started through sandbox-exec with read access to that one driver denied.
// Nothing in macOS is modified; "uninstall" restores stock behaviour.
//
//   musicfix install     write and load the LaunchAgent (~/Library/LaunchAgents/org.yoga920.musicfix.plist)
//   musicfix uninstall   unload and delete it
//   musicfix status      is the agent loaded, and is the running Music protected?
//   musicfix launch      start Music the fixed way (works without the agent)
//   musicfix run         the agent itself (launchd runs this)
//
// Log: ~/.local/share/musicfix/musicfix.log
// Build: clang -fobjc-arc -O2 -Wall -framework AppKit -o ~/.local/bin/musicfix musicfix.m
#import <AppKit/AppKit.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

extern char **environ;
extern int sandbox_check(pid_t pid, const char *operation, int type, ...);
#define SANDBOX_FILTER_PATH 1
#define SANDBOX_CHECK_NO_REPORT 0x40000000

static NSString *const kLabel = @"org.yoga920.musicfix";
static NSString *const kMusicID = @"com.apple.Music";
static const char *kMusicBinary = "/System/Applications/Music.app/Contents/MacOS/Music";
static const char *kDriverBinary =
    "/System/Library/Extensions/AppleIntelKBLGraphicsVADriver.bundle/Contents/MacOS/AppleIntelKBLGraphicsVADriver";
static const char *kProfile =
    "(version 1)(allow default)"
    "(deny file-read* (subpath \"/System/Library/Extensions/AppleIntelKBLGraphicsVADriver.bundle\"))"
    "(deny file-read* (subpath \"/System/Library/Extensions/AppleIntelKBLGraphicsVAME.bundle\"))";

static void logmsg(NSString *fmt, ...) NS_FORMAT_FUNCTION(1, 2);
static void logmsg(NSString *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    NSString *s = [[NSString alloc] initWithFormat:fmt arguments:ap];
    va_end(ap);
    time_t now = time(NULL);
    char ts[32];
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", localtime(&now));
    fprintf(stderr, "%s %s\n", ts, s.UTF8String);
}

// Protected = this process may not read the Intel video driver (only our sandbox denies that).
static BOOL isProtected(pid_t pid) {
    return sandbox_check(pid, "file-read-data", SANDBOX_FILTER_PATH | SANDBOX_CHECK_NO_REPORT, kDriverBinary) == 1;
}

static pid_t spawnDetached(const char *path, char *const argv[]) {
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);
    // Make the child its own "responsible process" for privacy prompts, as if the Dock had launched it.
    int (*disclaim)(posix_spawnattr_t *, int) =
        (int (*)(posix_spawnattr_t *, int))dlsym(RTLD_DEFAULT, "responsibility_spawnattrs_setdisclaim");
    if (disclaim) disclaim(&attr, 1);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    pid_t pid = -1;
    int rc = posix_spawn(&pid, path, &fa, &attr, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&attr);
    return rc == 0 ? pid : -1;
}

static pid_t launchProtected(void) {
    char *argv[] = {(char *)"sandbox-exec", (char *)"-p", (char *)kProfile, (char *)kMusicBinary, NULL};
    return spawnDetached("/usr/bin/sandbox-exec", argv);
}

static void notify(NSString *msg) {
    NSString *script = [NSString stringWithFormat:@"display notification \"%@\" with title \"Apple Music fix\"", msg];
    char *argv[] = {(char *)"osascript", (char *)"-e", (char *)script.UTF8String, NULL};
    spawnDetached("/usr/bin/osascript", argv);
}

@interface Watcher : NSObject
@property NSMutableSet<NSNumber *> *seen;
@property NSMutableArray<NSDate *> *recent;
@property BOOL disabled;
@end

@implementation Watcher
- (instancetype)init {
    if ((self = [super init])) {
        _seen = [NSMutableSet set];
        _recent = [NSMutableArray array];
        NSWorkspace *ws = NSWorkspace.sharedWorkspace;
        // runningApplications changes as soon as a launch starts, before the app has run any of its own code.
        [ws addObserver:self forKeyPath:@"runningApplications" options:NSKeyValueObservingOptionNew context:NULL];
        [ws.notificationCenter addObserver:self selector:@selector(didLaunch:)
                                      name:NSWorkspaceDidLaunchApplicationNotification object:nil];
        for (NSRunningApplication *a in ws.runningApplications) [self consider:a tries:0];
    }
    return self;
}

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary *)change context:(void *)ctx {
    id added = change[NSKeyValueChangeNewKey];
    if ([added isKindOfClass:NSArray.class])
        for (NSRunningApplication *a in added) [self consider:a tries:0];
}

- (void)didLaunch:(NSNotification *)n {
    [self consider:n.userInfo[NSWorkspaceApplicationKey] tries:0];
}

- (void)consider:(NSRunningApplication *)a tries:(int)tries {
    if (self.disabled || ![a.bundleIdentifier isEqualToString:kMusicID]) return;
    pid_t pid = a.processIdentifier;
    if (pid <= 0) {   // launch still pending: look again shortly
        if (tries < 40)
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 50 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                [self consider:a tries:tries + 1];
            });
        return;
    }
    if ([self.seen containsObject:@(pid)]) return;
    [self.seen addObject:@(pid)];
    if (isProtected(pid)) {
        logmsg(@"Music pid %d is protected", pid);
        return;
    }
    // Loop guard: 3 replacements within 2 minutes means the protected launch is not sticking. Stop interfering.
    NSDate *now = NSDate.date;
    [self.recent filterUsingPredicate:[NSPredicate predicateWithBlock:^BOOL(NSDate *d, NSDictionary *b) {
        return [now timeIntervalSinceDate:d] < 120;
    }]];
    if (self.recent.count >= 3) {
        self.disabled = YES;
        logmsg(@"Music pid %d: too many replacements in 2 minutes, leaving Music alone until the agent restarts", pid);
        notify(@"Could not keep Music protected; it was left as is. See ~/.local/share/musicfix/musicfix.log");
        return;
    }
    [self.recent addObject:now];
    if (!a.finishedLaunching) {
        // Caught at launch, before Music has opened its library: stop it outright.
        kill(pid, SIGKILL);
        logmsg(@"Music pid %d started without the fix; stopped it during launch", pid);
    } else {
        [a terminate];   // normal quit, Music saves its state
        logmsg(@"Music pid %d was running without the fix; asked it to quit", pid);
    }
    [self replaceAfterExitOf:pid];
}

- (void)replaceAfterExitOf:(pid_t)pid {
    __block int ticks = 0;
    [NSTimer scheduledTimerWithTimeInterval:0.05 repeats:YES block:^(NSTimer *t) {
        BOOL gone = kill(pid, 0) != 0 && errno == ESRCH;
        if (!gone && ++ticks < 300) return;   // wait up to 15 s
        [t invalidate];
        if (!gone) {
            logmsg(@"Music pid %d did not quit; left it running", pid);
            return;
        }
        pid_t np = launchProtected();
        if (np > 0) logmsg(@"started protected Music pid %d", np);
        else logmsg(@"could not start protected Music (errno %d)", errno);
    }];
}
@end

static NSString *plistPath(void) {
    return [NSHomeDirectory() stringByAppendingFormat:@"/Library/LaunchAgents/%@.plist", kLabel];
}

static int sh(NSString *cmd) {
    return system(cmd.UTF8String);
}

static BOOL agentLoaded(void) {
    return sh([NSString stringWithFormat:@"/bin/launchctl print gui/%d/%@ >/dev/null 2>&1", getuid(), kLabel]) == 0;
}

static int install(const char *self) {
    NSString *home = NSHomeDirectory();
    NSString *dir = [home stringByAppendingPathComponent:@".local/share/musicfix"];
    [NSFileManager.defaultManager createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    [NSFileManager.defaultManager createDirectoryAtPath:[home stringByAppendingPathComponent:@"Library/LaunchAgents"]
                            withIntermediateDirectories:YES attributes:nil error:nil];
    char real[PATH_MAX];
    if (!realpath(self, real)) strlcpy(real, self, sizeof real);
    NSDictionary *job = @{
        @"Label" : kLabel,
        @"ProgramArguments" : @[ @(real), @"run" ],
        @"RunAtLoad" : @YES,
        @"KeepAlive" : @YES,
        @"ProcessType" : @"Interactive",
        @"AbandonProcessGroup" : @YES,
        @"LimitLoadToSessionType" : @"Aqua",
        @"StandardErrorPath" : [dir stringByAppendingPathComponent:@"musicfix.log"],
    };
    if (![job writeToURL:[NSURL fileURLWithPath:plistPath()] error:nil]) {
        fprintf(stderr, "musicfix: cannot write %s\n", plistPath().UTF8String);
        return 1;
    }
    if (agentLoaded()) sh([NSString stringWithFormat:@"/bin/launchctl bootout gui/%d/%@ 2>/dev/null", getuid(), kLabel]);
    int rc = sh([NSString stringWithFormat:@"/bin/launchctl bootstrap gui/%d '%@'", getuid(), plistPath()]);
    printf(rc == 0 ? "installed\n" : "failed to load the agent\n");
    return rc == 0 ? 0 : 1;
}

static int uninstall(void) {
    if (agentLoaded()) sh([NSString stringWithFormat:@"/bin/launchctl bootout gui/%d/%@ 2>/dev/null", getuid(), kLabel]);
    [NSFileManager.defaultManager removeItemAtPath:plistPath() error:nil];
    printf("uninstalled (a Music that is open now keeps running as it is)\n");
    return 0;
}

static int status(void) {
    printf("agent: %s\n", agentLoaded() ? "loaded" : "not loaded");
    BOOL any = NO;
    for (NSRunningApplication *a in [NSRunningApplication runningApplicationsWithBundleIdentifier:kMusicID]) {
        any = YES;
        printf("Music pid %d: %s\n", a.processIdentifier,
               isProtected(a.processIdentifier) ? "protected (plays)" : "NOT protected (songs will stay at 0:00)");
    }
    if (!any) printf("Music: not running\n");
    return 0;
}

static int launchNow(void) {
    NSArray *running = [NSRunningApplication runningApplicationsWithBundleIdentifier:kMusicID];
    for (NSRunningApplication *a in running) {
        if (isProtected(a.processIdentifier)) {
            [a activateWithOptions:0];
            printf("Music already running, protected (pid %d)\n", a.processIdentifier);
            return 0;
        }
        fprintf(stderr, "Music is running without the fix (pid %d); quit it first\n", a.processIdentifier);
        return 1;
    }
    pid_t pid = launchProtected();
    printf(pid > 0 ? "started protected Music pid %d\n" : "failed (%d)\n", pid);
    return pid > 0 ? 0 : 1;
}

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        NSString *cmd = argc > 1 ? @(argv[1]) : @"status";
        if ([cmd isEqualToString:@"run"]) {
            signal(SIGCHLD, SIG_IGN);   // reap the Music processes we start once they quit
            setvbuf(stderr, NULL, _IOLBF, 0);
            [NSApplication sharedApplication];
            [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
            Watcher *w = [Watcher new];
            logmsg(@"agent started");
            (void)w;
            [NSApp run];
            return 0;
        }
        if ([cmd isEqualToString:@"install"]) return install(argv[0]);
        if ([cmd isEqualToString:@"uninstall"]) return uninstall();
        if ([cmd isEqualToString:@"status"]) return status();
        if ([cmd isEqualToString:@"launch"]) return launchNow();
        fprintf(stderr, "usage: musicfix install | uninstall | status | launch | run\n");
        return 2;
    }
}
