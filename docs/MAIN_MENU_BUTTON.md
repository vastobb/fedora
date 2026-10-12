# Main-menu button (between Play and Settings) — research + plan

User requirement: a `Riru` test button **inside Minecraft's own main menu**, visually between the `Play` button (below it) and the `Settings` button (above it) — see `ss.jpg` (Bedrock `1.26.52`, menu: Play / Settings / Realms / Marketplace). It does nothing (test only). Explicitly **not** the Levi floating mod-menu button.

## What the screenshot + lib tell us (verified)

- Menu is **not** legacy JSONUI. Bedrock `1.26.52` main menu is **HBUI/OreUI data-driven**: lib `.rodata` contains `/hbui/index.html`, `/hbui/gameplay.html`, `hbui/routes.json`, `ui://hbui/fonts/`, `OreUI::EntryPoints::PreStartScreen`, `/__bedrock__/start_screen`, `MainMenuScreenModel` / `MainMenuScreenController` / `StartMenuScreenController` / `PlayScreenController`, `GameplayUI::DataDrivenScreenAPI`, `DEV_NEW_MAIN_MENU_SCREEN_OVERRIDE`.
- Legacy markers still exist (`MainMenuScreen_*_JSONUI`, `menu_how_to_play_button`, `main_menu_screen`, `/legacy-main-menu`) but the visible 1.26 menu is the new stack.
- No exported game symbols for these controllers in `.dynstr` (stripped) — any C++ hook needs a **signature-resolved address + version gate**, currently **unverified** (`analysis/offsets.json` = candidates only).
- Levi SDK (`preloader-android@0.2.3`) exposes **overlay-only** UI (`ModuleBuilder`/`ButtonBuilder`/draw commands). There is **no SDK API** for injecting into Bedrock's StartScreen button list. `overwrite_files`/`overwrite_folders` in `FileHandler.java` govern **mod-to-mod import overwrites**, not game files. Bundled resource packs (`resources/minecraft_resource_packs/`, auto-synced by `BundledResourcePackInstaller` into `resource_packs/` + `global_resource_packs.json`) override **resource-pack content**, but whether HBUI (`hbui/`) files are resource-pack-overridable on 1.26 is **unconfirmed**.

## Why v1.0.0/1.0.1 cannot yet do it

- v1.0.0 crashed before any UI (STL bug, see `CRASH_FIX.md`). v1.0.1 fixes the crash and restores the safe Levi-overlay button as baseline.
- A true Bedrock button needs one of:
  1. **HBUI override**: patched `hbui` route/page adding a `<button>` between Play and Settings + JS handler (no-op). Requires the exact 1.26.52 HBUI file (from `base.apk.levi` assets) + confirmation that resource packs can override `hbui/`.
  2. **Native hook**: hook `MainMenuScreenModel`/`Controller` button-list construction to insert an entry. Requires Ghidra-recovered function + tested signature for **1.26.52 / SHA256 `5f5baf8e...`**, plus safe fallback. Current `reverse.yml` only does string triage, not full function recovery (196 MiB `.text` needs hours + targeted scripts).
  3. **Overlay mimicry** (interim): Levi HUD draw + positioned touch handling that *looks* like a menu button. Visually close on one device, but resolution-dependent and still overlay-level, not Bedrock-level. Not shipped as "done".

## Plan (needs device-side iteration)

1. **Ship v1.0.1 crash fix first**; user confirms no tombstone and baseline overlay button works.
2. User provides (via `/emulated/`): `base.apk.levi` asset list for `hbui/` (or the `hbui/routes.json` + main-menu page), and a fresh `ss.jpg` with screen size. Commands to run on a rooted/file-manager device will be given (list `assets/hbui/`, pull `index.html`/route for start screen).
3. With the real HBUI file: craft override candidate (button element between Play and Settings, no-op handler), test via resource-pack bundle in `riru/resources/minecraft_resource_packs/riru_ui/`; if HBUI is not overridable, pivot to hook with a **tested** signature + `RIRU_USE_HOOKS=1` Actions build + on-device validation.
4. Only mark done after user screenshot shows the Bedrock-level button in place with no crash.

## Status

- [x] Crash root-caused (STL) and fixed in build (v1.0.1, retained in v1.0.2).
- [x] Hooking mechanism mapped end-to-end (loader + GlossHook + BedrockTools patterns) — see `docs/HOOKING.md`.
- [x] 7/8 BedrockTools signatures re-tested MATCH on 1.26.52 (`NormalTick`, `VersionString`, `ScreenViewRender`, `DrawText`, `ContainerOpen`, `ChatOpen`, `GetFov`); `ClientInstanceUpdate` needs Ghidra re-find.
- [x] riru v1.0.2 probes the 7 on-device and logs `RIRU_SIG` lines, installs zero hooks (test-before-use).
- [ ] User runs v1.0.2, returns `adb logcat -s RiruTest | grep RIRU_SIG` (confirms device addresses).
- [ ] HBUI main-menu file acquired from 1.26.52 APK (`hbui/index.html` JS hash + start-screen route).
- [ ] Override-vs-hook decision with evidence.
- [ ] `riru` shows Bedrock-level button between Play and Settings, no-op, no crash (user-confirmed via screenshot).
