# Keyboard 72

Nintype on current phones. This is a fork of lurebat's
[Keyboard 71](https://github.com/lurebat/keyboard71).

Nintype's engine is a closed-source 32-bit ARM library, and a lot of newer
phones can't run 32-bit code at all anymore. Keyboard 72 includes a small ARM32
emulator (built on [dynarmic](https://github.com/azahar-emu/dynarmic)), so the
original engine still runs, unmodified, inside a normal arm64 app. Swiping,
peck mode, word learning and the keyboard's interface are all still Nintype's
own code.

It installs as its own app (`local.keyboard72`), so it can sit next to
Keyboard 71, and it doesn't ask for network access.

## Changes from Keyboard 71

- Runs on arm64-only phones. How the emulator works is described in
  [app/src/main/cpp/README.md](app/src/main/cpp/README.md).
- Uses much less CPU while idle or with the screen off, so leaving the keyboard
  open no longer drains the battery.
- Fixed the keyboard sometimes coming up blank when reopened right after being
  hidden.
- Password fields of every kind (including PINs) are private: nothing gets
  learned and no suggestions are shown. They also type one letter at a time,
  which fixes doubled characters in some browsers.
- Number and phone fields open the number pad, date fields the number board,
  and the enter key shows the field's action (go, search, send and so on).
- How each kind of field behaves can be changed in the app.
- Password manager suggestions (Bitwarden etc.) show up in the keyboard's top
  bar.
- Backup and restore for settings, shortcuts and learned words, using the same
  format as the original Nintype, so backups go both ways.
- Haptics can be set to off, light or strong.
- Newer emoji font, so the emoji and kaomoji pickers don't have blank spots.
- An experimental "fast memory access" option, and a button to restart the
  engine without restarting the app.
- While the engine starts, the keyboard shows what it's doing instead of a
  blank panel.
- New settings page and icon.
- No network permissions (they were only used by server mode, which stopped
  working years ago).

## Building

You need JDK 17 and an Android SDK with platform 33, build-tools 33.0.1,
NDK 27.2.12479018 and CMake 3.22.1.

```sh
./build-apk.sh assembleRelease
```

The APK ends up in `app/build/outputs/apk/`. `build-apk.sh` keeps the SDK and
Gradle's cache inside the project folder (`.android-sdk`, `.gradle-home`)
unless `ANDROID_HOME` or `GRADLE_USER_HOME` is set. Only arm64 is built.

dynarmic is included in `third_party/dynarmic`; `VENDORED.md` there says which
version it is and what was changed.

### Signing

Release builds are signed with the key listed in `keystore.properties` in the
project folder (it's in `.gitignore`). Without that file they get the debug
key. To make a key:

```sh
keytool -genkeypair -v -keystore ~/keys/keyboard72-release.jks \
    -alias keyboard72 -keyalg RSA -keysize 4096 -validity 10000
```

and `keystore.properties`:

```properties
storeFile=/path/to/keyboard72-release.jks
storePassword=...
keyAlias=keyboard72
keyPassword=...
```

Android won't update an app that was signed with a different key, so going
from a debug build to a release build (or the other way) means uninstalling
first. Export your backups before you do.

## Credits

Nintype was made by [jormy](https://github.com/jormy/nintype). Keyboard 71 is
lurebat's work, and this builds directly on it. Their original notes on the
code are below.

---

Architechture:

1. libgl2jni.so - The native library. Untouchable.
2. NINLib.kt -> interfaces from the app into the library.
3. Methods marked @Api (by me, you're welcome). Methods that the library calls as the API.
    DO NOT CHANGE THEIR SIGNATURE
4.Ninview - where the displaying logic happens
5.Softkeyboard - where the keyboard logic happens

-lurebat
