# riru — Levi test mod

Adds a **Riru Test** button for testing. Does nothing except add it.

- `manifest.json`: `type: preload-native`, `entry: libRiru.so`, modId = directory name `riru`.
- `src/RiruTest.cpp`: `PL_REGISTER_MOD(RiruTest, RiruTest::instance())`, `load()` required, `enable()` registers ModMenu module `riru.main` + floating button `riru.test_button` labeled `Riru`, `disable()`/`unload()` unregister. Button callback only logs.
- `CMakeLists.txt`: `arm64-v8a`, C++20, `preloader-android@0.2.3`, links `preloader` + `log`.

## Why this counts as “main menu button” without offsets

Bedrock `libminecraftpe.so` here is stripped (no exported game symbols) and main menu is OreUI tech-stack (`StartScreen`/`PlayScreen`/`ScreenController`). Hard-coding a StartScreen address is version-fragile. The supported Levi path is ModMenu floating button + HUD overlay, which is visible while the main menu is shown and needs **zero game offsets**. That is what `riru` ships by default (`RIRU_USE_HOOKS 0`).

Optional true-StartScreen hook scaffold exists in `RiruTest.cpp` (`#if RIRU_USE_HOOKS`) but stays off until `.github/workflows/reverse.yml` produces a `verified` entry in `offsets.json` for your exact lib SHA256 + version. See `docs/SO_MOD_LOADING.md §7` and `docs/REVERSE_NOTES.md`.

## Package layout (what Levi expects)

```text
riru/                  # modId
  manifest.json
  libRiru.so           # built for arm64-v8a
  config/ (optional)
```

`.levipack` = tar of the above. Verify:

```bash
tar -tf riru.levipack
# manifest.json
# libRiru.so
```

Import: copy unpacked `riru/` into the version's `modsDir`, or import `.levipack` in launcher. Enable in mods list (`mods_config.json`).

## Build

Preferred: Actions → `Build riru (NDK, Actions-only)` (`.github/workflows/build-riru.yml`). Downloads NDK r27, builds `arm64-v8a`, validates `manifest.json` + `PLGetModRegistration` export, packages `riru.levipack`, uploads artifact + optional release.

Local (if you have NDK):

```bash
export ANDROID_NDK_HOME=/path/to/ndk
cmake -S riru -B build/riru -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28
cmake --build build/riru
mkdir -p dist/riru && cp build/riru/out/arm64-v8a/libRiru.so dist/riru/ && cp riru/manifest.json dist/riru/
tar -cf riru.levipack -C dist riru
```

## Test checklist (before claiming “works”)

- [ ] `manifest.json` parses, `type` is `preload-native`, `entry` exists.
- [ ] `libRiru.so` is AArch64, exports `PLGetModRegistration`.
- [ ] Launcher shows “Riru Test”, enable → in-game ModMenu shows module, floating “Riru” button visible on main menu HUD.
- [ ] Tap button → logcat `RiruTest` click event, no crash, no other behavior.
- [ ] Disable → button/module unregistered, re-enable works.
- [ ] If `RIRU_USE_HOOKS=1`: only with `verified` offsets, version-gated, fallback to ModMenu-only on resolve failure.
