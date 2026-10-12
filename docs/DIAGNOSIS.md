# Diagnosis 2026-10-12 — import rejection + registerModule SIGSEGV

## 1. Import rejection (`error.jpg`)

Levi dialog: `Invalid ZIP Mod Package: must contain manifest.json and a .so file — riru.levipack`.

Root cause: our `.levipack` was a **TAR** (`tools/package_riru.py` used `tarfile`). `FileHandler.isValidZipModPackage` opens it with `ZipInputStream`, which throws on tar bytes → rejected. Fix (v1.0.3): `.levipack` is now a **ZIP** with `manifest.json` + `libRiru.so` at root (fast path: `manifestRoots.contains("")`), validated in packaging by reopening as zip and asserting exactly 1 `.so`. Verified in run `38211684536` artifact: `['manifest.json', 'libRiru.so']`.

Install note: delete every old `Riru`/`Riru(1)`/`riru` dir first — Levi dedups re-imports as `Riru(1)` with renamed `libRiru(1).so`, which confuses follow-up tombstones.

## 2. Native crash (`/emulated/crash/tombstone_...xcrash`, also first `crash.txt`)

Identical in v1.0.0 (736,840 B, static STL) and v1.0.1 (742,944 B, shared STL):

```text
signal 11 (SIGSEGV), SEGV_MAPERR, fault addr 0x10
#00 libpreloader.so registerModule+1484
#01 libRiru.so ModuleBuilder::registerModule+20
#02 libRiru.so RiruTest::enable+256
```

Disproven hypotheses:
- **Static STL**: v1.0.1 rebuilt with `-DANDROID_STL=c++_shared` (verified `libc++_shared.so x1` in binary) crashed identically → not the cause. Gate kept anyway (matches Levi `app/build.gradle`).
- **Preloader version skew**: device preloader == Levi submodule `5d108da`; `ModMenu.hpp` + `ModMenuBridge.cpp` byte-identical between tag `0.2.3` and `5d108da` (446/1443 lines, 0 diff) → `ModuleInfo` ABI matches.

Remaining suspects (all inside our `ModuleInfo` payload, since device code is fixed):
1. Explicit `.modId(self.getId())` — BedrockTools never sets it (owner fills it). Ours passes `Riru(1)`-style ids with parens.
2. Non-empty `onToggle` `std::function` (stack shows `__clone`; only non-default callable we pass).
3. Device-side bug with zero-config modules (BedrockTools modules always carry configs).

Bisection (v1.0.3, shipped): `enable()` registers a minimal module (no modId/description/onToggle/configs) and minimal button (no behavior/event overrides), logging `step1`/`step2` markers with results before/after each call. Next tombstone (if any) + `adb logcat -s RiruTest` pinpoints the exact call. Signature probing is compiled but not called (one variable at a time).

## 3. Crash-log paths

User-quoted `/storage/emulated/0/Android/media/org.levimc.launcher/crash_logs/` does not exist in this environment; actual tombstones were at `/emulated/crash/tombstone_00001791781968253799_1.5.27__org.levimc.launcher.native.xcrash` (xCrash 3.1.0, POCO F7, Android 16, launcher 1.5.27). Future tombstones: copy any new `tombstone_*` from that dir the same way.

## 4. Main-menu button (between Play and Settings, Bedrock 1.26.52)

Unchanged plan, see `docs/MAIN_MENU_BUTTON.md` + `docs/HOOKING.md`: menu is HBUI/OreUI data-driven (`/hbui/index.html` + hashed JS bundle, `MainMenuScreenModel/Controller`); no Levi SDK API for it; 7/8 BedrockTools signatures match 1.26.52 (`ClientInstanceUpdate` needs Ghidra re-find). v1.0.3 carries no game hooks. After the bisection result: re-add ModMenu fields one by one (v1.0.4+), then HBUI override (needs 1.26.52's exact `index-*.js` hash from `base.apk.levi`) or a confirmed hook.

## 5. Real root cause (v1.0.4, proven by build failure)

v1.0.4 (all callbacks incl. `.onKeybind`) **failed to compile** against `preloader@0.2.3`:

```text
RiruTest.cpp:71:14: error: no member named 'onKeybind' in 'pl::modmenu::ModuleBuilder'
```

So tag `0.2.3` LACKS `onKeybind`, while device libpreloader (Levi 1.5.27, submodule `5d108da`, which implements floating-button visibility) HAS it — i.e. its `ModuleInfo` is LARGER. Our 0.2.3-built struct missed the tail field(s); device `registerModule` read `onKeybind` out of bounds and cloned garbage → the `LDR x9,[x9,#0x10]` fault at `+1484` (verified by hand-decoding `/tmp/libpreloader.so` from the 1.5.27 APK). Earlier "headers identical" claim was wrong (diffed main against itself). Fix: build against `5d108da` + set all three callbacks (BedrockTools parity). Run `38212990180` green; `riru-v1.0.4` published.
