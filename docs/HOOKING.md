# Hooking mechanism — full architecture (loader + preloader + game)

> User correction accepted: LeviLaunchroid itself ships **no game-content API** — it is a **`.so` mod loader** (discovers `preload-native` packages, `System.load()`s the cached copy, drives `PLGetModRegistration` lifecycle). All game interaction comes from the **`preloader-android` SDK** linked into each mod + **byte signatures** resolved at runtime. This doc traces the whole chain with verified sources.

## 1. Loader (LeviLaunchroid, Java/Kotlin) — what it expects from a `.so` mod

Verified via `gh api` (no clone), see `docs/SO_MOD_LOADING.md`:

- Package: `<modsDir>/<modId>/manifest.json` (`type: preload-native`, `entry: libX.so`) + `.so` (`arm64-v8a`, **`ANDROID_STL=c++_shared`** — static STL crashes, see `docs/CRASH_FIX.md`).
- `ModNativeLoader`: fingerprint (`loader=4`), copy to `cacheDir/mods/<modId>/` (skips top-level `data/`+`config/`), `setReadOnly`, `System.load(cached)`, then `nativeLoadMod(libPath, modRoot, modObj)` in `libpreloader.so`.
- `MinecraftRuntimePreparer.prepare()` → loads `c++_shared`/game libs → `loadEnabledSoMods` → `MinecraftActivity.onCreate()` calls `nativeEnableLoadedMods()` before Mojang `MainActivity`.
- Mod must export `PLGetModRegistration` (via `PL_REGISTER_MOD`) with `load()` (required) + `enable/disable/unload()` (optional). Instance must stay alive; never throw across boundaries.

## 2. SDK (preloader-android 0.2.3, linked into the mod)

| Header | Mechanism |
| ------ | --------- |
| `pl/Mod.hpp` | `PL_REGISTER_MOD`, `NativeMod::current()`, lifecycle dispatch (`ModRegistration`) |
| `pl/ModMenu.hpp` | Overlay-only UI (`ModuleBuilder`/`ButtonBuilder`/draw境外 commands) bridged to launcher Java (`ExternalModBridge`) — **not** Bedrock UI |
| `pl/memory/Signature.hpp` | `resolveSignature(pat, "libminecraftpe.so")` / `resolveSignatures()` — byte-pattern scan (`?` wildcards) over the loaded game lib |
| `pl/memory/Hook.hpp` | `hook(target, detour, &orig, priority)` / `unhook()` / `HookHandle` RAII — inline-hook chain |
| `pl/memory/Patch.hpp` | Byte patching; `pl/Input.hpp`, `pl/Config.hpp` for input/typed config |

`src/pl/memory/Hook.cpp` (verified): `GlossInit(true)` once, then per-target `GlossHook(target, detour, &origin)` + priority chain (`rebuildChain()`), mutex-guarded map. Detour **must** match the target's exact C++ signature and call `original` to continue the chain; mismatched signatures recurse or corrupt registers → SIGSEGV. Hence: **resolve → log → verify on device → hook**, never blind-hook.

## 3. Proven pattern source: BedrockTools (Unknown3111/BedrockTools v1.4.1)

Open-source Levi mod (33 modules, NDK r28c) that wraps exactly the two APIs above:

- `include/bedrocktools/memory/Signatures.hpp` + `src/core/memory/Signatures.cpp`: ~90 `SignatureDefinition{id, pattern}` entries, resolved in bulk via `pl::memory::resolveSignatures`.
- `src/core/memory/Hooks.hpp`: thin wrapper over `pl::memory::hook/unhook` (+ `dlopen`/`dlsym` for `libEGL.so`).
- `src/core/GameHooks.cpp`: `hookSignature(id, detour, &orig)` returns false on unresolved (graceful); `install()` succeeds only if `NormalTick` + `ClientInstanceUpdate` resolve; detours publish typed events (`LocalPlayerTickEvent`, `ScreenStateEvent` for Container/Chat, `FrameEvent` via `eglSwapBuffers`).
- Offsets in `include/bedrocktools/sdk/offsets/*.hpp` (UI/World/Render/Network) are version-specific and the main thing needing updates per game build.

## 4. Re-tested against 1.26.52 (`/emulated/libminecraftpe.so`, SHA256 `5f5baf8e…`)

Lightweight python scan of `.text` (205,828,044 B @ file off 105669856) with BedrockTools patterns:

| Signature | Result on 1.26.52 | .text-rel first match |
| --------- | ----------------- | --------------------- |
| NormalTick | MATCH | 0x46018f8 |
| VersionString | MATCH | 0x1c49ba4 |
| ScreenViewRender | MATCH | 0x35c34a0 |
| DrawText (`MinecraftUIRenderContextDrawText`) | MATCH | 0x4d8a3fc |
| ContainerOpen | MATCH | 0xb1de68 |
| ChatOpen | MATCH | 0x3548b68 |
| GetFov | MATCH | 0x4ddf504 |
| ClientInstanceUpdate | **NO MATCH** | needs Ghidra re-find for 1.26 |

7/8 hit → BedrockTools methodology transfers to 1.26.52; `ClientInstanceUpdate` drifted and must be re-derived (targeted Ghidra xref from `ClientInstance` callers, then `resolveSignatures` re-test). `riru` v1.0.2 embeds the 7 matched patterns as **probe-only** (logs `RIRU_SIG <name> = 0x… FOUND/NOT_FOUND`, installs nothing).

## 5. What riru does with this (v1.0.2)

- `load()`: `probeSignatures()` → `pl::memory::resolveSignatures` over `libminecraftpe.so` → logcat (`adb logcat -s RiruTest | grep RIRU_SIG`) + Levi logger. Always returns true.
- `enable()`: same crash-fixed ModMenu baseline as v1.0.1 (no game calls).
- No `hook()` calls ship until the device log confirms the 7 addresses on the user's exact lib build. Next: hook `NormalTick` (counter-only detour) as the first end-to-end hook proof, then screen-state detection for the main-menu work (`docs/MAIN_MENU_BUTTON.md`).

## 6. References

- `LiteLDev/preloader-android`: `include/pl/memory/{Hook,Signature}.hpp`, `src/pl/memory/Hook.cpp` (GlossHook chain), `src/pl/runtime/ModMenuBridge.cpp`, `src/pl/runtime/GameHooks.*`
- `Unknown3111/BedrockTools`: `Signatures.{hpp,cpp}`, `Hooks.hpp`, `GameHooks.cpp`, `Version.hpp` (1.4.1), xmake + NDK r28c
- `OpenMCBE/Bedrock-OreUI-Sources` (`Android/gui/dist/hbui/`), `Luminoso-256/oreUIShim` (HBUI route patch method), bedrock.dev JSON-UI merge docs
- Internet: Horizon/KafkaLauncher/MCPELauncher prior art (forwarder + `/proc/self/maps` + ShadowHook/Substrate) — same loader+hook shape, different launchers
