# LeviLaunchroid `.so` mod loading — how it works

> Diagnosed **without cloning**, via `gh api` on `LiteLDev/LeviLaunchroid@main` (release `v1.5.27`).
> Only `.so` mods (`type: preload-native`) are covered.

## 1. Terms

- **Mod package**: a directory `<modsDir>/<modId>/` containing `manifest.json` + entry `.so` (+ optional `config/`, `data/`, `resources/`).
- **modId**: the package directory name. Stable — used for paths, ModMenu ownership, persisted state.
- **modsDir**: per-game-version directory (`GameVersion.modsDir`), selected via `ModManager.setCurrentVersion(version)`.
- **Cache dir**: `cacheDir/mods/<modId>/` — read-only copy that is actually `System.load()`ed.
- **Preloader**: `libpreloader.so` (`System.loadLibrary("preloader")`), from submodule `app/src/main/cpp/preloader` → SDK repo `LiteLDev/preloader-android`. Owns `PL_REGISTER_MOD` lifecycle + ModMenu + Hook/Signature APIs.

## 2. Manifest (`manifest.json`)

Minimal verified shape (`examples/full-cpp-mod/manifest.json`):

```json
{
  "type": "preload-native",
  "name": "Full C++ Lifecycle Mod Example",
  "author": "LiteLDev",
  "version": "1.0.0",
  "entry": "libfull_cpp_mod.so",
  "minecraft_versions": []
}
```

Full developer doc (`docs/guide/developer.md`) allows also:

```json
{
  "type": "preload-native",
  "name": "...",
  "author": "...",
  "version": "1.0.0",
  "entry": "libxxx.so",
  "icon": "icon.png",
  "minecraft_versions": [],
  "overwrite_files": [],
  "overwrite_folders": []
}
```

| Field | Rule (verified) |
| ----- | --------------- |
| `type` | Must be exactly `preload-native`. Anything else is not a native `.so` mod. |
| `entry` | Relative path to the Android `.so` **inside** the mod dir. `Mod.getEntryPath()` → resolved to `cacheModsDir/<modId>/<entry>`. |
| `name` / `author` / `version` / `icon` | Display metadata → `Mod` object → `NativeMod::getName()` etc. |
| `minecraft_versions` | Empty/missing = all versions. Else exact match, `*`/`x` wildcards, `>=` compare. See §5. |
| `overwrite_files` / `overwrite_folders` | Import-time replace allowlist. |

Discovery code: `ModManager.discoverMods()` → lists subdirs of `modsDir`, `parseDirectoryMod()` reads `manifest.json`. Top-level loose `.so` files are migrated (`migrateTopLevelSoMods()`), but the supported layout is **directory-per-mod**.

Config: `modsDir/mods_config.json` = `[{name, enabled, order}]`. `getMods()` returns ordered `Mod` list.

## 3. Load path (Java side)

Files (verified via `gh api .../contents/...`):

- `app/src/main/java/org/levimc/launcher/core/mods/ModManager.java`
- `app/src/main/java/org/levimc/launcher/core/mods/ModNativeLoader.java`
- `app/src/main/java/org/levimc/launcher/core/mods/Mod.java`
- `app/src/main/java/org/levimc/launcher/core/minecraft/MinecraftRuntimePreparer.kt`
- `app/src/main/java/org/levimc/launcher/core/minecraft/MinecraftActivity.kt`

### 3.1 Prepare (`MinecraftRuntimePreparer.prepare()`)

1. `resolveGameVersion(intent)` → `GameVersion`.
2. `ModManager.getInstance().setCurrentVersion(version)` → sets `modsDir`, loads `mods_config.json`, starts `FileObserver`.
3. `ModManager.ensurePreloaderLoaded()` → `System.loadLibrary("preloader")` (once, `preloaderLoadAttempted` guard).
4. `PreloaderSignatureRulesManager` + `PreloaderInput.configureSignatureRules(...)`.
5. `loadMinecraftLibraries(...)` → loads `c++_shared`, `HttpClient.Android`, `PlayFabMultiplayer` bundle etc. per version (1.21.110+ layout handled).
6. `loadNativeMods(...)` → calls `ModNativeLoader.loadEnabledSoMods(modManager, cacheDir, listener)` with progress 78% → “Loading enabled mods”.
7. `BundledResourcePackInstaller.sync(...)` for mod-bundled packs.
8. Returns `PreparedRuntime`. `MinecraftActivity.onCreate()` then calls `ModManager.enableLoadedMods()` → `nativeEnableLoadedMods()` **before** `super.onCreate()` (Mojang `MainActivity`).

### 3.2 `ModNativeLoader.loadEnabledSoMods()` (exact steps)

```java
loadEnabledSoMods(modManager, cacheDir, listener)
  mods = modManager.getMods()              // ordered, enabled flag from config
  minecraftVersion = modManager.getCurrentVersion().versionCode
  cacheModsDir = new File(cacheDir, "mods"); mkdirs
  for mod in mods where mod.isEnabled():
    if !isCompatibleWithMinecraftVersion(mod, minecraftVersion): skip + onModLoadSkipped
    else:
      cached = prepareCachedEntry(modManager, cacheModsDir, mod)
      // cached = { sourceDirectory: modsDir/modId, targetFile: cacheModsDir/modId/entry }
      ensureReadOnly(targetFile)
      System.load(targetFile.getAbsolutePath())   // dlopen the cached copy
      ModManager.initializeLoadedMod(targetFile, sourceDirectory, mod)
        -> nativeLoadMod(libPath, modRootPath, modObj)  // JNI in libpreloader.so
```

`prepareCachedEntry()` details (important for mod authors):

- `sourceDirectory = modsDir/modId` must be a directory.
- Fingerprint = `loader=4\nid=...\nentry=...\nfile=<rel>|<len>|<mtime>\n...` over all files **excluding** top-level `data/` and `config/` (runtime dirs, `isRuntimeDirectory()`).
- If `targetDir/.mod_cache_manifest` matches fingerprint **and** target entry exists → reuse (no copy).
- Else: copy `source → cacheModsDir/modId.tmp` via `copyPackageDirectory()` (same exclusion), write manifest, `renameTo(targetDir)` atomically, prune stale (`pruneStaleCachedMods()` deletes cache dirs whose modId no longer installed + `*.tmp`).
- `ensureReadOnly()`: must be readable, then `setReadOnly()`. Failure → `IOException`.

Failure paths: `IOException` / `UnsatisfiedLinkError` → `Log.e` + `listener.onModLoadFailed`. `initializeLoadedMod` returning `false` → “Failed to finish native initialization”.

## 4. Native lifecycle (`libpreloader.so` + `PL_REGISTER_MOD`)

SDK headers (`preloader-android/include/pl/`): `Mod.hpp`, `ModMenu.hpp`, `Config.hpp`, `Input.hpp`, `memory/Hook.hpp`, `memory/Patch.hpp`, `memory/Signature.hpp`.

Author shape (verified `docs/api/mod.md` + `examples/full-cpp-mod/src/FullCppMod.cpp`):

```cpp
#include <pl/Mod.hpp>

class MyMod {
public:
  static MyMod &instance();
  MyMod(); // : mSelf(*ll::mod::NativeMod::current()) {}
  [[nodiscard]] ll::mod::NativeMod &getSelf() const { return mSelf; }
  bool load();    // REQUIRED
  bool enable();  // optional, default success
  bool disable(); // optional
  bool unload();  // optional
private:
  ll::mod::NativeMod &mSelf;
};

PL_REGISTER_MOD(MyMod, MyMod::instance())
```

- `PL_REGISTER_MOD` emits `PLGetModRegistration` symbol. Preloader `nativeLoadMod()` finds it after `System.load()`.
- Keep the instance alive for process lifetime. Do not throw across boundaries; return `false` on failure.
- `NativeMod` getters: `getId()` (== modId dir name), `getName()`, `getAuthor()`, `getVersion()`, `getEntryPath()`, `getModDir()`, `getDataDir()` (`<root>/data`), `getConfigDir()` (`<root>/config`), `getResourceDir()`, `getManifestPath()`, `getLibraryPath()`, `getJavaVM()`, `getLogger()`, `getState()` / `isLoaded()` / `isEnabled()`.

Recommended phase work:

| Phase | Do |
| ----- | -- |
| `load()` | Read config, `create_directories(getConfigDir())`, prepare state. No hooks/UI yet. |
| `enable()` | Register hooks, input callbacks, ModMenu modules/buttons/HUD. |
| `disable()` | Unregister hooks/UI, undo game-facing work. |
| `unload()` | Release remaining C++ state. |

`enableLoadedMods()` / `disableAndUnloadLoadedMods()` are called from `MinecraftActivity` lifecycle.

## 5. Version gate

`ModNativeLoader.isCompatibleWithMinecraftVersion()`:

- `null`/empty list → compatible.
- Each pattern trimmed; empty ignored. If no valid pattern → compatible.
- `>=X.Y.Z` → numeric compare (`compareMinecraftVersions`, `MIN_VALUE` on non-numeric → no match).
- Else if no `*`/`x`/`X` → exact string equals.
- Else wildcard → regex: `*` → `.*`, whole-component `x` → `\d+`, partial `x` → `\d`, other chars quoted. Full-match.

`versionCode` example: `1.21.110` etc. (see `REVERSE_NOTES.md` for strings found in lib).

## 6. UI without offsets: ModMenu API (the `riru` way)

For a test button **do not** hardcode Bedrock function addresses. Use:

```cpp
#include <pl/ModMenu.hpp>

pl::modmenu::ModuleBuilder("riru.main", "Riru Test")
  .modId(self.getId())
  .description("Test module. Does nothing except add a button.")
  .defaultEnabled(true)
  .registerModule();

pl::modmenu::ButtonBuilder("riru.test_button", "Riru Test Button")
  .moduleId("riru.main")
  .modId(self.getId())
  .label("Riru")
  .behavior(pl::modmenu::ButtonBehavior::Click)
  .onEvent([](std::string_view, pl::modmenu::ButtonEvent e, float){})
  .registerButton();
```

Full API: `docs/api/mod-menu.md` in LeviLaunchroid — modules, `ButtonBehavior::{Click,Hold,Toggle}`, `ButtonEvent::{Click,Down,Up,StateChanged,Scroll}`, `pngIcon()/webpIcon()/svgIcon()`, `submitDrawCommands()`, `registerImage()/registerFont()`, `unregisterModule()/unregisterButton()` in `disable()`.

Java bridge (`ExternalModBridge.java`, `ExternalModuleProvider.java`) exposes the same modules/buttons to the launcher overlay (`InbuiltOverlayManager`).

## 7. True StartScreen patching (optional, offset-dependent)

Bedrock `libminecraftpe.so` here is **stripped** for game symbols (no `ClientInstance`/`MinecraftGame` in `.dynstr`; OreUI strings only in `.rodata`). So:

- Resolve via `pl::memory::resolveSignature("pattern", "libminecraftpe.so")`, **not** `dlsym`.
- Hook via `pl::memory::hook(target, detour, &original, Priority)` or `HookHandle` RAII; test return (`0` = ok), unhook in `disable()`.
- Candidate offsets/functions must come from **Actions-side Ghidra** (`reverse.yml`), be recorded in `offsets.json`, and be **tested before use** (null-check, version-gate, safe fallback to ModMenu-only mode). Never ship a hard-coded absolute address without a signature + version check.

See `docs/REVERSE_NOTES.md` (what we measured) and `riru/src/RiruTest.cpp` (scaffold with `RIRU_USE_HOOKS 0` default).

## 8. Build & package (verified)

- ABI: `arm64-v8a` only.
- `CMakeLists.txt` pattern (`examples/full-cpp-mod/CMakeLists.txt`): `FetchContent preloader-android GIT_TAG 0.2.3`, `target_link_libraries(my_mod PRIVATE preloader)`, `c++20`, `-fvisibility=hidden`, link `log`.
- Output: `lib<name>.so`.
- Package dir name == modId, e.g. `riru/`:
  `riru/manifest.json`, `riru/libRiru.so` (entry), `riru/config/...` optional.
- `.levipack` = tar of the package dir (`tar -tf ...levipack` should list `manifest.json`, `.so`, `config/...`). Import via launcher or copy dir into `modsDir`.

## 9. Source pointers (for re-verification via `gh`)

- `gh api repos/LiteLDev/LeviLaunchroid/contents/app/src/main/java/org/levimc/launcher/core/mods/ModNativeLoader.java`
- `.../ModManager.java`, `.../Mod.java`
- `.../core/minecraft/MinecraftRuntimePreparer.kt`, `.../MinecraftActivity.kt`, `.../MinecraftLauncher.java`
- `.../core/mods/inbuilt/ExternalModBridge.java`
- `docs/guide/developer.md`, `docs/api/mod.md`, `docs/api/mod-menu.md`, `docs/api/hook.md`, `docs/api/signature.md`
- `examples/full-cpp-mod/{CMakeLists.txt,manifest.json,src/FullCppMod.cpp}`, `examples/mod-menu-api/src/cpp_lifecycle_mod.cpp`
- SDK: `gh api repos/LiteLDev/preloader-android/contents/include/pl --jq`
