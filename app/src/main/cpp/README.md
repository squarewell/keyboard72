# arm64 compatibility shim for the original engine

The keyboard's engine (`src/main/jniLibs/armeabi/libgl2jni.so`) is a 32-bit ARM library with no source. Current phones often cannot load 32-bit code at all. This directory builds a replacement `libgl2jni.so` for **arm64-v8a** (and **x86_64** for the Android emulator). The replacement runs the untouched original engine on an ARM32 CPU emulator. The CPU emulator is the [dynarmic](https://github.com/azahar-emu/dynarmic) JIT used by the Citra/Azahar emulators (`third_party/dynarmic`, with a Boost header subset in `third_party/boost-headers`).

`NINLib` still loads `gl2jni`, and the original 15 native methods exist unchanged (a few new ones serve the launch page). The original library is packaged as `assets/nintype/libgl2jni-armeabi.so` by the `copyNinEngine` Gradle task. On the first native call that needs it, the shim loads and links that file and runs its constructors. It then forwards every `NINLib` call into it. The engine draws Nintype's own interface through the phone's real OpenGL ES.

## How it works

| File | Role |
| --- | --- |
| `guest.{h,cpp}` | 4 GiB guest address space backed 1:1 by one host reservation (guest pointer + base = host pointer). Also: ELF loader and relocations (including GNU IFUNC resolvers), import stubs, re-entrant host→guest calls, Linux kuser helpers (plain guest code: one core needs no exclusives), the hardware floating point redirects. Imports, JNI tables and the end-of-call marker are `svc` traps. |
| `cpu.h`, `cpu_dynarmic.cpp` | The CPU backend. dynarmic reaches guest memory through a page table into the host reservation; nested calls run on a second JIT instance. |
| `bionic_hle.cpp` | The engine's libc/libm/liblog imports, served by the host libc. Includes conversions for structures whose 32-bit layout differs (`stat`, `dirent`, `tm`, `FILE`, open flags). File mappings are real host `mmap`s placed inside guest memory, so the engine's dictionary files behave exactly as on a device. |
| `gles_bridge.cpp` | All 76 GLES2 imports forwarded to the real driver, with pointer translation (client-side vertex arrays, textures, shader sources). |
| `jni_bridge.cpp` | Guest `JNIEnv`/`JavaVM` tables. Decodes `Call…Method`/`V`/`A` by method signature. Provides the `AAssetManager` imports. |
| `android_java.{h,cpp}` | The real Java side: handle tables between 32-bit guest handles and JNI references, cached classes, exceptions logged and cleared. |
| `ninlib_exports.cpp` | The `Java_com_jormy_nin_NINLib_*` natives, lazy boot, one lock around the engine, restart. |
| `backup.{h,cpp}` | Export and import through the engine's own backup code (launch page). |
| `engine_patch.{h,cpp}`, `diag_bypass.cpp`, `jni_sync_bypass.cpp`, `hot_dispatch.cpp`, `*_sites.inc` | Hash-checked instruction patches to the loaded engine (see Switches). |
| `fastlibc_blob.h` | Guest-native (ARM32) code built by `native/build_native.py`: `memcpy`/`strcmp`/`malloc`… from `native/fastlibc.c`, and the VFP floating point routines from `native/vfpfloat.c`. These run inside the emulator instead of crossing into the host. Other clang versions produce different (equivalent) code; the checked-in blob is the tested one. |

Behavior notes:
- `syncTiming` arrives on the main thread before the GL thread exists. It is deferred and replayed with the current time at the first engine call, which keeps the Java-millis/engine-clock anchor consistent.
- `/proc` is hidden from the engine. It only reads it for memory statistics and crash reports, and the 64-bit host's values do not fit its 32-bit parsing.
- The engine runs single-threaded (it never started a thread in the paths tested). Mutexes are no-ops, and `pthread_create` is refused with a log message.
- **GNU indirect functions.** `__atomic_fetch_add_4` (which the engine's own code uses for every `std::string` reference count, ~79,000 call sites) is an IFUNC. The symbol is a resolver that returns the real implementation. The loader calls it once, as Android's linker does, and links the result. Before this was done, every reference-count update was a no-op. No string was ever freed (the heap grew ~1.7 MB/s even with the keyboard idle, and the app died after a few minutes), and a copied string that was later modified changed all its copies.
- **Frame pacing.** The Java render loop relies on `eglSwapBuffers` to wait for the display. The original app paused rendering while the screen was locked. This fork removed that so the keyboard works on the lock screen. With the screen off, or while the engine is idle and draws nothing, the swap returns at once and the loop spun at ~5,000 steps/s. `SoftKeyboard` now pauses the render thread while the screen is off; hiding the keyboard already stopped it, because the surface is destroyed. As a safety net, `step` keeps at least 4 ms between frames. That is shorter than any display's refresh interval, so normal frames never wait.
- **Engine assertions.** The engine's assertion handler prints a stack trace built from `/proc/<pid>/maps`. The shim serves a synthetic maps file listing the guest mappings; other `/proc` files stay hidden. One known assertion is the engine's own: its infinite-loop guard in `PathSearch::gatherRoughEstimatedPlanarCoverages` fires after about 28 swipes are merged into one word without a space, and then the engine aborts deliberately. When the shim stops on an error, it logs the guest `pc`/`lr` and likely return addresses found on the guest stack.
- **Version label.** The engine labels itself with its 2017 build date ("v. Sep  9 2017" in the spacebar menu, "NIN v. …" on the spacebar after a restart, the options page). Gradle passes the app's `versionName` as `NIN_VERSION_LABEL` (at most 11 characters), and those strings are rewritten in guest memory at load.
- **Fallback font.** The engine draws text with its own FreeType, one-color glyph outlines and no shaping. A character missing from the current font goes to backup fonts, and `assets/notoemoji.ttf` is the last resort for every character. It is now the current monochrome Noto Emoji plus Noto text glyphs for the kaomoji board, kana, CJK punctuation, fullwidth forms, box drawing and geometric shapes (`third_party/notoemoji`). The 2014 font it replaces left blank cells in the emoji picker (185 of 1033 emoji) and in the kaomoji board (117 of 144 entries). Android's color system emoji font can't be used this way.
- **Hardware floating point.** The engine was built for soft-float armeabi, so every float operation called a libgcc routine of 20–60 integer instructions. At load, the first instruction of 46 of these routines (`__aeabi_fadd`, `__divsf3`, `__cmpsf2`, conversions, the double versions…) becomes a branch to a VFP version in `vfpfloat.c` that keeps the soft-float calling convention. The results are the same bits as the originals' (NaN payloads aside). That libgcc is not correctly rounded for additions involving denormals, nor for some double subtractions (exponent gap of exactly 33). The VFP code sends those inputs to the original routine through a trampoline, so the engine computes exactly what it did on a phone. A differential test checked this (2.9 M comparisons, 0 mismatches). `NIN_SOFT_FLOAT=1` keeps the original routines.
- **Diagnostics I/O.** While typing, the engine polls `/proc/self/stat` about 50 times per frame for memory statistics (answered "not found" without a system call). It rewrites a crash breadcrumb (`memtracklatest.trackey`) several times per frame and rotates a memory log (`shinglogs/mninmem.logs`) every few frames. Those two go to `/dev/null` and never exist on disk. They only feed the engine's own crash reports. Learning, dictionary and settings files are untouched. Engine log lines are no longer copied to logcat. Turn that on with `adb shell setprop debug.nin.enginelogs 1` and restart the keyboard.
- **Engine patches.** A few hash-checked instruction patches cut work the engine does on every frame: its memory tracking and diagnostic reports, rebuilding its Java class/method cache, float-call veneers and three single-threaded atomic helpers. Each one checks the engine's SHA-256 and the original instructions before writing, and keeps results, allocation order and random number use identical. They are on by default; see Switches.
- **Restart.** `NINLib.restartEngine` (the launch page's Restart engine button) shuts the engine down on the GL thread, closing its host files and descriptors and deleting its GL objects and Java global references, and the next call boots a fresh one. Boot-time settings such as fast memory access then apply without restarting the app.

## Building

A normal Gradle build (see the top-level README) needs the SDK, NDK r27 (override with `-Pnin.ndkVersion=…`) and CMake 3.22.1. Native code is always built optimized (`CMAKE_BUILD_TYPE` is forced to Release; Gradle would otherwise build debug APKs' native code at `-O0`).

### dynarmic patches

Local changes to dynarmic are kept as patches in `patches/dynarmic/`, applied at configure time to build-tree copies of the files they touch (listed in `NIN_DYNARMIC_PATCHED_FILES` in `CMakeLists.txt`). The copies replace the originals in the dynarmic target and come first on its include path, so the submodule stays at its upstream commit. Applying them needs `patch`. `0001-arm64-compact-block-bookkeeping.patch` cuts the arm64 backend's per-block bookkeeping: on a test phone after typing, the keyboard's native heap fell from 289 to 103 MB (total PSS 517 to 330 MB) with unchanged typing times.

### Backups

`backup.cpp` serves the launch page's export and import buttons. It calls the engine's own exporters (`Moopad::getSettingsExport`, `getWordExport`, `getReadableWordExport`, `SpaceCompleter::getShortcutsExport`) and hands file text to `ImportationModule::importText`, the path behind the engine's "import from clipboard". It also patches the two import prompts to open at a real point rather than `RoenGL::pagemid()`, whose page size is never set on Android.

## Switches

These are read when the engine boots. The first five are on by default; turn one off with `adb shell setprop debug.nin.<name> 0` and restart the keyboard (Restart engine on the launch page). `adb logcat -s nin-shim` shows what is active.

| Property | Effect |
| --- | --- |
| `diag_bypass` | Skips the engine's memory tracking and diagnostic reports. |
| `jni_sync_bypass` | Keeps the engine's Java class/method cache instead of rebuilding it every frame. With `diag_bypass`: about 62% fewer emulated instructions per typed word and 79% per idle frame. |
| `float_dispatch`, `byte_cas`, `refcount_add` | Shorter float-call dispatch and synchronization helpers, a further 5.6%. |
| `jit_fastmem` | Off by default; also the launch page's experimental "fast memory access" setting (either turns it on). Direct JIT access to emulated memory: on a test phone it cut touch handling from 7.4 to 6.2 ms, frame time unchanged. It installs dynarmic's SIGSEGV handler next to ART's, so set it back to off if the keyboard crashes. |
| `enginelogs` | Copies the engine's own log lines to logcat (off by default). |

On a device, `adb logcat -s nin-shim` prints the boot time and, every 2 s while the keyboard is shown, `perf: N frames/s, step avg … | touch avg …` (engine time per frame and per touch event).
