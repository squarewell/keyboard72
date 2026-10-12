# Keyboard 72

Nintype (or keyboard 69 on android) ported to arm64. Tested on a pixel 10. Forked from lurebat's
[Keyboard 71](https://github.com/lurebat/keyboard71), which is itself a (very good!) attempt at fixing up and modernizing nintype.

This packages the original nintype engine inside dynarmic (the emulator/recompiler from yuzu and related) with a bunch of compatibility fixes and patches to strip dynarmic down to only the bare minimum needed. Memory usage is up a bit from the original (extra 100MB or so, ish) but all the original memory leaks are fixed. Network access/servermode removed so there isn't a weird permission popping up. 

This took a mind boggling amount of LLM usage (probably 1-5B tokens split between opus 5.5 and astra? mostly documentation/research) but most of the actual high level decisions and design choices were made by me. Feel free to fork/download/whatever, I may not be very available to fix things. 

It's very hard (impossible?) to make extensive changes to the binary as one person without LLMs, so I might clean up and publish the messy research repo documenting the decompiled version at some point, also working on a native port but no clue if that'll work.

Thanks for a great keyboard jormy!

#nintype4lyfe
## Changes

- Runs on modern arm64-only phones. Some extra documentation here:
  [app/src/main/cpp/README.md](app/src/main/cpp/README.md)
- Multiple memory leaks in the original nintype binary fixed, now should be stable for 30+ min
  open time (indefinitely, really, but not tested for that long)
- Lots of optimizations in the binary, faster frametime and drawing, unnecessary 
  logging/diagnostics/etc. removed, frames no longer get rendered while not visible, 
  rendering paused when not typing to save battery, etc., 
- Should open properly on any input field now without having to close/reopen 
- Password fields of every kind (including PINs) are now detected correctly and characters aren't
  doubled up sometimes (hopefully)
- Number and phone fields open the number pad, date fields the number board,
  and the enter key shows the field's action (go, search, send, etc)
- Input field behavior directly exposed and customizable
- Password manager suggestions (Bitwarden etc.) show up in the keyboard's top
  bar
- Backup and restore for settings, shortcuts and learned words, using the same
  format as the original Nintype, so backups go both ways
- Variable haptics
- Newer emoji and kaomoji fonts, so the emoji and kaomoji pickers don't have blank spots.
- A separate button to restart the engine in the app without having to open the nintype menu, so that
  if it freezes (which it shouldn't) you can reset easily
- New settings page and icon

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