# Crash fix v1.0.1 — SIGSEGV in `registerModule`

## Symptom (device: POCO F7, Android 16, launcher 1.5.27)

`xCrash 3.1.0`, `signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr 0x10`:

```text
#00 libpreloader.so (registerModule+1484)
#01 libRiru.so (ModuleBuilder::registerModule+20)
#02 libRiru.so (RiruTest::enable+256)
```

`RiruTest::enable()` → `ModuleBuilder(...).registerModule()` crashes before any button appears. Mod installed as `.../mods/Riru/libRiru.so` (note capital `R`; package dir `riru` vs installed `Riru` — case differs but id handling is case-preserving, not the crash cause).

## Root cause: `c++_static` vs `c++_shared` ABI mismatch

- Levi mandates shared STL: `app/build.gradle` → `arguments "-DANDROID_STL=c++_shared"`, NDK `28.2.13676358`.
- `fedora` v1.0.0 workflow used NDK `r27` default = **`c++_static`** (verified: built `libRiru.so` contains **zero** references to `libc++_shared.so`, 736,840 B, static libc++ baked in).
- `ModuleInfo`/`ButtonInfo` (header-only builders from `preloader-android@0.2.3`) carry `std::string` + `std::function` across the `.so` boundary into `libpreloader.so` (shared libc++). Static-vs-shared `std::string`/`std::function` layouts differ → preloader reads garbage ptr/size → null+0x10 deref in `registerModule`.

## Fix (v1.0.1)

1. `build-riru.yml`: NDK `r27` → `r28`, add `-DANDROID_STL=c++_shared` to configure (with comment + `CMakeCache.txt` grep proof).
2. `tools/package_riru.py`: **fail the build** if `libc++_shared.so` absent from the built `.so` (gates regression).
3. `riru/manifest.json`: version `1.0.0` → `1.0.1` (so launcher cache fingerprint changes and stale `Riru`/`riru` cache dirs are pruned).
4. Source unchanged (same `ModuleBuilder` calls) — the calls were correct; only the STL was wrong.

## Verify after install

- No tombstone on launch; `logcat -s RiruTest` shows `Riru enable: Riru Test button added`.
- Launcher mods list shows `Riru Test 1.0.1`; enable → in-game Levi overlay shows module; floating `Riru` button appears (Levi overlay, draggable).
- `.../cache/native_mods/.../mods/riru/` (lowercase, matching package dir — do **not** rename to `Riru`; delete any old `Riru` dir first).

## Note on main-menu button

This fix stops the crash. The **true Minecraft main-menu button** (between Play and Settings) is a separate task — see `docs/MAIN_MENU_BUTTON.md`. v1.0.1 keeps the safe Levi-overlay button as baseline while main-menu injection is researched (HBUI/data-driven screens, no verified offsets yet).
