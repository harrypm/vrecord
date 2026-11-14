#!/bin/sh
# Builds a standalone vrecord.app bundle for macOS.
# Runs the existing Release_Mac.sh to fetch/compile dependencies, then
# packages the result as a proper .app. On failure of the full release
# script, falls back to a minimal bundle using brew-installed deps.
set -ex
set -x
cd "$(dirname "$0")"
RELEASE_DIR="$(pwd)"
ARTIFACTS="$RELEASE_DIR/artifacts"
rm -rf "$ARTIFACTS"
mkdir -p "$ARTIFACTS"

APP="$ARTIFACTS/vrecord.app"
CONTENTS="$APP/Contents"
MACOS="$CONTENTS/MacOS"
RESOURCES="$CONTENTS/Resources"
FRAMEWORKS="$CONTENTS/Frameworks"
LAUNCHER_SRC="$ARTIFACTS/vrecord-launcher.m"
mkdir -p "$MACOS" "$RESOURCES/bin" "$RESOURCES/share/vrecord/Resources" "$FRAMEWORKS"

VERSION="$(sed -nE 's/^VERSION="([0-9.-]+)"/\1/p' ../vrecord)"
ROOT="$RELEASE_DIR/vrecord_ROOT/usr/local"

# Target architecture (mirrors Release_Mac.sh): x86_64, arm64, or universal.
ARCH="${ARCH:-universal}"
case "${ARCH}" in
    x86_64|arm64|universal) ;;
    *) echo "ERROR: ARCH must be x86_64, arm64, or universal (got '${ARCH}')." >&2 ; exit 1 ;;
esac
if [ "${ARCH}" = "universal" ] ; then
    MACOS_ARCH_FLAGS="-arch x86_64 -arch arm64"
else
    MACOS_ARCH_FLAGS="-arch ${ARCH}"
fi
# same scheme as the release assets: vrecord_<version>_<platform>_<arch>
APP_ZIP_NAME="vrecord_${VERSION}_macos_${ARCH}.app.zip"

# --- Attempt the full release build (fetches/compiles all deps) ---
if [ -x "$ROOT/bin/vrecord" ] || ./Release_Mac.sh 2>&1; then
    echo "full release build OK"
    # copy the built files from vrecord_ROOT
    cp -a "$ROOT/bin/"* "$MACOS/" 2>/dev/null || true
    cp -a "$ROOT/share/vrecord/Resources/"* "$RESOURCES/share/vrecord/Resources/" 2>/dev/null || true
    # Mirror the pkg layout (bin/, lib/, app/, share/) under Resources so
    # relative references resolve: gtkdialog links its GTK dylibs via
    # @loader_path/../lib, bin/mpv is a symlink to ../app/mpv.app, and
    # cowsay looks for ../share/cowsay. (A previous copy into Frameworks/
    # left gtkdialog unable to load its libraries.)
    mkdir -p "$RESOURCES/lib" "$RESOURCES/app" "$RESOURCES/share"
    cp -a "$ROOT/lib/vrecord/bin/"* "$RESOURCES/bin/" 2>/dev/null || true
    cp -a "$ROOT/lib/vrecord/lib/"* "$RESOURCES/lib/" 2>/dev/null || true
    cp -a "$ROOT/lib/vrecord/app/"* "$RESOURCES/app/" 2>/dev/null || true
    cp -a "$ROOT/lib/vrecord/share/"* "$RESOURCES/share/" 2>/dev/null || true
else
    echo "::warning::Release_Mac.sh failed - building minimal .app from brew deps"
    # fallback: local repo scripts/resources + brew-installed runtime deps
    if ! command -v ffmpeg-ma >/dev/null 2>&1; then
        brew tap mediaarea/mediaarea
        brew tap amiaopensource/amiaos
        brew trust mediaarea/mediaarea
        brew trust amiaopensource/amiaos
        brew install ffmpeg-ma gtkdialog mpv mediaconch mkvtoolnix gnuplot xmlstarlet mediainfo dvrescue decklinksdk cowsay
    fi
    cp ../vrecord "$MACOS/vrecord"
    cp ../vtest "$MACOS/vtest"
    cp ../vplay "$MACOS/vplay"
    cp -a ../Resources/* "$RESOURCES/share/vrecord/Resources/"
    # runtime binaries from brew
    for BIN in ffmpeg-ma ffplay-ma ffprobe-ma gtkdialog mpv mediaconch mediainfo mkvextract mkvinfo mkvmerge mkvpropedit xmlstarlet cowsay gnuplot dvrescue; do
        BINPATH="$(command -v "$BIN" 2>/dev/null || true)"
        [ -n "$BINPATH" ] && cp "$BINPATH" "$RESOURCES/bin/"
    done
fi

# Always build the launcher (Info.plist sets CFBundleExecutable to it):
# it prepends the bundled Resources/bin to PATH and runs the vrecord script
# as a child while hosting the app's NSApplication, so the Dock lifecycle
# works: Dock-Quit terminates the wrapped process tree, and clicking the
# Dock icon while the window is minimised restores it through the
# Accessibility API. (The previous execv-based launcher made the bundle's
# main process a shell, which ignores Dock Apple Events, so the app could
# not be quit from the Dock nor reopened from its Dock icon when minimised.)
cat > "$LAUNCHER_SRC" << 'LAUNCHER'
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <libgen.h>
#import <mach-o/dyld.h>
#import <signal.h>
#import <stdio.h>
#import <stdlib.h>
#import <string.h>
#import <unistd.h>
#import <sys/wait.h>

static pid_t child_pid = 0;
static int child_status = 0;
static int shutting_down = 0;

static void kill_child_tree(int sig) {
    if (child_pid <= 0) return;
    kill(-child_pid, sig);   // the child's whole process group
    kill(child_pid, sig);
}

static void forward_signal(int sig) {
    if (shutting_down) return;
    shutting_down = 1;
    kill_child_tree(sig);
    _exit(128 + sig);
}

static void restore_child_windows(void) {
    AXUIElementRef app = AXUIElementCreateApplication((pid_t)child_pid);
    if (!app) return;
    CFTypeRef windows_ref = NULL;
    AXError err = AXUIElementCopyAttributeValue(app, kAXWindowsAttribute, &windows_ref);
    if (err == kAXErrorSuccess && windows_ref) {
        CFArrayRef windows = (CFArrayRef)windows_ref;
        for (CFIndex i = 0; i < CFArrayGetCount(windows); ++i) {
            AXUIElementRef win = (AXUIElementRef)CFArrayGetValueAtIndex(windows, i);
            CFTypeRef mini = NULL;
            if (AXUIElementCopyAttributeValue(win, kAXMinimizedAttribute, &mini) == kAXErrorSuccess && mini) {
                if (CFBooleanGetValue((CFBooleanRef)mini)) {
                    AXUIElementSetAttributeValue(win, kAXMinimizedAttribute, kCFBooleanFalse);
                    AXUIElementPerformAction(win, kAXRaiseAction);
                }
                CFRelease(mini);
            }
        }
        CFRelease(windows_ref);
    } else if (err == kAXErrorAPIDisabled || err == kAXErrorCannotComplete) {
        fprintf(stderr, "vrecord: allow Accessibility permission once to restore the minimised window from the Dock\n");
    }
    if (windows_ref) CFRelease(windows_ref);
    CFRelease(app);
}

@interface VrecordDelegate : NSObject
@end
@implementation VrecordDelegate
- (BOOL)applicationShouldHandleReopen:(NSApplication *)theApplication hasVisibleWindows:(BOOL)hasVisible {
    (void)theApplication;
    if (child_pid > 0) {
        restore_child_windows();
        // make sure nothing is stuck stopped
        kill(child_pid, SIGCONT);
        [[NSRunningApplication currentApplication] activateIgnoringOtherApps:YES];
    }
    return YES;
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    (void)sender;
    shutting_down = 1;
    if (child_pid > 0) {
        kill_child_tree(SIGTERM);
        for (int i = 0; i < 30; ++i) {
            int st = 0;
            if (waitpid(child_pid, &st, WNOHANG) == child_pid) { child_status = st; break; }
            usleep(100000);
        }
        kill_child_tree(SIGKILL);
    }
    return NSTerminateNow;
}
@end

int main(int argc, char *argv[]) {
    char exe_path[PATH_MAX];
    uint32_t size = sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &size) != 0) {
        fprintf(stderr, "Failed to resolve executable path\n");
        return 1;
    }
    char resolved_path[PATH_MAX];
    if (realpath(exe_path, resolved_path) == NULL) {
        perror("realpath");
        return 1;
    }
    char macos_dir[PATH_MAX];
    strncpy(macos_dir, resolved_path, sizeof(macos_dir) - 1);
    macos_dir[sizeof(macos_dir) - 1] = '\0';
    char *dir = dirname(macos_dir);

    char script_path[PATH_MAX];
    char bin_path[PATH_MAX];
    snprintf(script_path, sizeof(script_path), "%s/vrecord", dir);
    snprintf(bin_path, sizeof(bin_path), "%s/../Resources/bin", dir);

    const char *old_path = getenv("PATH");
    char new_path[PATH_MAX * 2];
    snprintf(new_path, sizeof(new_path), "%s:%s", bin_path, old_path ? old_path : "");
    setenv("PATH", new_path, 1);

    char **child_argv = calloc((size_t)argc + 2, sizeof(char *));
    if (!child_argv) {
        perror("calloc");
        return 1;
    }
    child_argv[0] = "bash";
    child_argv[1] = script_path;
    for (int i = 1; i < argc; ++i) child_argv[i + 1] = argv[i];
    child_argv[argc + 1] = NULL;

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }
    if (pid == 0) {
        // the vrecord process tree leads its own group so a Dock-Quit can
        // clean up every descendant (ffmpeg readers, test signal, ...)
        setpgid(0, 0);
        execv("/bin/bash", child_argv);
        perror("execv");
        _exit(127);
    }
    child_pid = pid;

    signal(SIGINT, forward_signal);
    signal(SIGTERM, forward_signal);
    signal(SIGHUP, forward_signal);
    signal(SIGQUIT, forward_signal);

    [NSApplication sharedApplication];
    VrecordDelegate *delegate = [[VrecordDelegate alloc] init];
    [NSApp setDelegate:delegate];

    // when the script's window closes the script exits: end the app too,
    // with the child's exit status
    NSTimer *poll = [NSTimer timerWithTimeInterval:0.5 repeats:YES block:^(NSTimer *timer) {
        (void)timer;
        if (child_pid <= 0) return;
        int st = 0;
        pid_t r = waitpid(child_pid, &st, WNOHANG);
        if (r == child_pid) {
            child_status = st;
            child_pid = 0;
            shutting_down = 1;
            [NSApp terminate:nil];
        }
    }];
    [[NSRunLoop mainRunLoop] addTimer:poll forMode:NSDefaultRunLoopMode];

    [NSApp run];
    kill_child_tree(SIGKILL);
    if (WIFEXITED(child_status)) return WEXITSTATUS(child_status);
    return 0;
}
LAUNCHER
clang -Os -mmacosx-version-min=11.0 $MACOS_ARCH_FLAGS -framework Foundation -framework AppKit -framework ApplicationServices -o "$MACOS/vrecord-launcher" "$LAUNCHER_SRC"

ln -snf ../Resources/share/vrecord/Resources "$MACOS/Resources"
chmod +x "$MACOS/vrecord" "$MACOS/vplay" "$MACOS/vtest"
if [ -f "$MACOS/vrecord-launcher" ]; then
    chmod +x "$MACOS/vrecord-launcher"
fi

# --- Info.plist ---
cat > "$CONTENTS/Info.plist" << EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>
    <string>vrecord</string>
    <key>CFBundleDisplayName</key>
    <string>vrecord</string>
    <key>CFBundleIdentifier</key>
    <string>com.harrypm.vrecord</string>
    <key>CFBundleVersion</key>
    <string>${VERSION}</string>
    <key>CFBundleShortVersionString</key>
    <string>${VERSION}</string>
    <key>CFBundleExecutable</key>
    <string>vrecord-launcher</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleIconFile</key>
    <string>AppIcon</string>
    <key>LSMinimumSystemVersion</key>
    <string>11.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSCameraUsageDescription</key>
    <string>vrecord needs camera access to capture video from DeckLink devices.</string>
</dict>
</plist>
EOF

# --- icon (if available) ---
if [ -f "$RESOURCES/share/vrecord/Resources/vrecord-logo.icns" ]; then
    cp "$RESOURCES/share/vrecord/Resources/vrecord-logo.icns" "$RESOURCES/AppIcon.icns"
elif [ -f "$RESOURCES/share/vrecord/Resources/vrecord-icon.svg" ]; then
    ICONSET="$ARTIFACTS/AppIcon.iconset"
    SVG_SOURCE="$RESOURCES/share/vrecord/Resources/vrecord-icon.svg"
    SVG_MASTER_DIR="$ARTIFACTS/svg-master"
    SVG_MASTER="$SVG_MASTER_DIR/vrecord-icon-1024.svg"
    SVG_MASTER_PNG="$SVG_MASTER_DIR/vrecord-icon-1024.png"
    rm -rf "$ICONSET" "$SVG_MASTER_DIR"
    mkdir -p "$ICONSET" "$SVG_MASTER_DIR"
    # sips rasterizes SVGs at their intrinsic size, so render a copy scaled to
    # 1024px for a crisp master, keeping the SVG's transparent background
    sed 's/<svg /<svg width="1024" height="1024" /' "$SVG_SOURCE" > "$SVG_MASTER"
    if sips -s format png "$SVG_MASTER" --out "$SVG_MASTER_PNG" >/dev/null 2>&1 && [ -f "$SVG_MASTER_PNG" ]; then
        sips -z 16 16     "$SVG_MASTER_PNG" --out "$ICONSET/icon_16x16.png" >/dev/null
        sips -z 32 32     "$SVG_MASTER_PNG" --out "$ICONSET/icon_16x16@2x.png" >/dev/null
        sips -z 32 32     "$SVG_MASTER_PNG" --out "$ICONSET/icon_32x32.png" >/dev/null
        sips -z 64 64     "$SVG_MASTER_PNG" --out "$ICONSET/icon_32x32@2x.png" >/dev/null
        sips -z 128 128   "$SVG_MASTER_PNG" --out "$ICONSET/icon_128x128.png" >/dev/null
        sips -z 256 256   "$SVG_MASTER_PNG" --out "$ICONSET/icon_128x128@2x.png" >/dev/null
        sips -z 256 256   "$SVG_MASTER_PNG" --out "$ICONSET/icon_256x256.png" >/dev/null
        sips -z 512 512   "$SVG_MASTER_PNG" --out "$ICONSET/icon_256x256@2x.png" >/dev/null
        sips -z 512 512   "$SVG_MASTER_PNG" --out "$ICONSET/icon_512x512.png" >/dev/null
        cp "$SVG_MASTER_PNG" "$ICONSET/icon_512x512@2x.png"
        iconutil -c icns "$ICONSET" -o "$RESOURCES/AppIcon.icns"
    fi
elif [ -f "$RESOURCES/share/vrecord/Resources/vrecord_logo.png" ]; then
    ICONSET="$ARTIFACTS/AppIcon.iconset"
    mkdir -p "$ICONSET"
    sips -z 16 16     "$RESOURCES/share/vrecord/Resources/vrecord_logo.png" --out "$ICONSET/icon_16x16.png" >/dev/null
    sips -z 32 32     "$RESOURCES/share/vrecord/Resources/vrecord_logo.png" --out "$ICONSET/icon_16x16@2x.png" >/dev/null
    sips -z 32 32     "$RESOURCES/share/vrecord/Resources/vrecord_logo.png" --out "$ICONSET/icon_32x32.png" >/dev/null
    sips -z 64 64     "$RESOURCES/share/vrecord/Resources/vrecord_logo.png" --out "$ICONSET/icon_32x32@2x.png" >/dev/null
    sips -z 128 128   "$RESOURCES/share/vrecord/Resources/vrecord_logo.png" --out "$ICONSET/icon_128x128.png" >/dev/null
    sips -z 256 256   "$RESOURCES/share/vrecord/Resources/vrecord_logo.png" --out "$ICONSET/icon_128x128@2x.png" >/dev/null
    sips -z 256 256   "$RESOURCES/share/vrecord/Resources/vrecord_logo.png" --out "$ICONSET/icon_256x256.png" >/dev/null
    sips -z 512 512   "$RESOURCES/share/vrecord/Resources/vrecord_logo.png" --out "$ICONSET/icon_256x256@2x.png" >/dev/null
    sips -z 512 512   "$RESOURCES/share/vrecord/Resources/vrecord_logo.png" --out "$ICONSET/icon_512x512.png" >/dev/null
    cp "$RESOURCES/share/vrecord/Resources/vrecord_logo.png" "$ICONSET/icon_512x512@2x.png"
    iconutil -c icns "$ICONSET" -o "$RESOURCES/AppIcon.icns"
fi

echo "APPL????" > "$CONTENTS/PkgInfo"

codesign --force --deep -s - "$APP" >/dev/null 2>&1 || true

# --- zip for upload ---
cd "$ARTIFACTS"
zip -qry "$APP_ZIP_NAME" vrecord.app
echo "Built: $ARTIFACTS/vrecord.app and $APP_ZIP_NAME"
