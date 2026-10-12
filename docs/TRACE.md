# TRACE — vanilla main menu in libminecraftpe.so 1.26.52 (static)

> No logcat, no device feedback: everything below was extracted locally with
> `python3` string/structure scans of `/emulated/libminecraftpe.so`
> (SHA256 `5f5baf8e…`, 328,419,680 B) + public sources. Heavy disassembly was
> deliberately avoided locally (policy); the one hand-decoded function so far
> is `registerModule+1484` in `/tmp/libpreloader.so` (1.5.27 APK), see
> `docs/DIAGNOSIS.md`.

## 1. Menu stack: C++ side (class map from RTTI/mangled names in `.rodata`)

| Class | Role (from method names) |
| ----- | ------------------------ |
| `SceneFactory::createStartMenuScreen(bool)` | Entry factory for the start-menu screen |
| `MainMenuScreenModel` | Data: realms/worlds navigation, featured worlds, skins, store catalog, trial, screenshots cache (`navigateToChooseRealmScreen`, `navigateToFeaturedWorld`, `clearScreenshotsCache`, …) |
| `MainMenuScreenController` | Behavior: `onOpen`, `attemptUserSignIn`, `startRealm`, `_startLocalWorldSubRoutine`, skin/modals |
| `StartMenuScreenController` / `PlayScreenController` / `PlayScreenModel` | Sibling screens (Play flow) |
| `ScreenView` | Renderer: ctor takes `shared_ptr<ScreenController>` — every screen renders through it |
| `ScreenController` / `IScreenController` | Base interfaces |

Render path candidates (BedrockTools patterns re-tested MATCH on this lib):
`ScreenViewRender` (.text+0x35c34a0), `MinecraftUIRenderContextDrawText`
(.text+0x4d8a3fc), `NormalTick` (.text+0x46018f8), `VersionString`,
`ContainerOpen`, `ChatOpen`, `GetFov`. `ClientInstanceUpdate` NO MATCH
(drifted in 1.26). See `docs/HOOKING.md`.

## 2. Menu stack: HBUI side (paths + loader, from `.rodata`/`.dynstr`)

- Paths: `/hbui/index.html`, `/hbui/gameplay.html`, `hbui/routes.json`,
  `gui/dist/hbui/fonts`, `ui://hbui/fonts/`. `routes.json` gives
  `/hbui/index.html` scope `in-game + out-of-game` (menu lives there).
- Full mirror (other version): `OpenMCBE/Bedrock-OreUI-Sources`
  (`Android/gui/dist/hbui/`). Its `index.html` (689 B) only bootstraps a
  hashed bundle (`index-<HASH>.js`) — NEVER ship another version's copy
  (wrong hash = broken menu). Runtime patching reads installed bytes.
- `CoHTML` x18657, `ResourceHandler` x28, `LoadURL` x3: Bedrock registers a
  custom CoHTML resource handler (-feeds views; ultimate file reads land in
  Android assets and/or plain files).
- Game `.dynstr` imports (exact): `AAssetManager_open` x2, `AAsset_read` x1,
  `AAsset_seek` x1, `AAsset_getLength` x2, `AAsset_getLength64` x1,
  `AAsset_close` x1, `AAssetManager_fromJava` x1 — i.e. streaming reads.
  `AAsset_getBuffer`/`AAsset_openFileDescriptor` are NOT imported by the
  game (0 hits); v1.0.6 still hooks libandroid's exports defensively
  (expected idle, zero risk).

## 3. Why NOT other paths (with sources)

- **Resource-pack UI override: DEAD for main menu.** bedrock.dev/bedrock-wiki:
  "Unlike JSON UI, Ore UI is hardcoded and cannot be modified by resource
  packs." OreUI assets live in install files (`Content/data/gui/dist/hbui`),
  not in any pack layer. (A MineNest author removing main-menu buttons must
  be on legacy/partial UI, not 1.26 OreUI.)
- **C++ model hook (add menu entry in `MainMenuScreenModel`): possible but
  needs struct layouts + vtable slots for 1.26.52 —Blind without runtime
  proof; deferred to v1.0.7+ with Ghidra vtable recovery in Actions.**
- **Chosen: serve patched `/hbui/index.html` through AAsset hooks**
  (precedent: `QPC-github/assethook` replaces APK assets via this exact C
  API; nixware-style mods inject menu JS at HTML-panel load; oreUIShim
  documents the `engine.js`-first route patch). Stable `libandroid.so`
  symbols via `dlopen+dlsym` — zero game offsets. Injected script clones the
  Play DOM node (identical UI/texture by construction).

## 4. Local RE workstation (/ghidra, authorized 2026-10-12)

- `/ghidra/libminecraftpe.so` (copy of `/emulated/`, SHA256 `5f5baf8e…`).
- Tools (all local): Capstone 5.0.9 wheel (`/ghidra/tools/capstone_py`),
  hand-rolled ELF/ADRP/PLT/xref scanners (`/tmp/opencode/re/*.py`), Ghidra
  12.1.4 + system JDK 25 (`/usr/lib/jvm/java-openjdk`; Temurin 17/21 JREs
  were rejected by Ghidra's launcher).
- PLT lesson (AArch64 LLD): `rela[i]` ↔ stub at `plt+16*(i+2)` (NOT `i+1`:
  proven by disassembling stubs and matching each stub's GOT load against
  `r_offset`). Earlier xref tables built with the `i+1` rule were WRONG
  (they pointed at a libyuv row function, not file IO).

## 5. Verified import slots (stub-disassembly ↔ rela ↔ dynstr cross-checked)

| Import | TRUE PLT slot | BL sites |
| ------ | ------------- | -------- |
| `AAssetManager_open` | `0x12912d80` | 4 |
| `AAssetDir_getNextFileName` | `0x12912860` | 13 |
| `AAsset_read` | `0x12913c60` | 0 (reads go through wrappers/virtuals) |
| `AAsset_close` / `getLength` / `getLength64` | resolved | — |
| `fopen`/`open`/`openat`/`LoadURL`(direct) | NOT imported | — (CoHTML calls are virtual = BLR) |
| `AAssetManager_openDir`/`getBuffer`/`openFileDescriptor`/`seek64`/`fromJava` | NOT imported | — |

All 4 open callers build filenames dynamically on-stack (no literals);
nearby constants are unrelated (crash keys, tick settings, client IDs).
`getNextFileName` callers enumerate APK asset dirs at runtime. Full C++ decompilation of the 4 open callers is running in Ghidra
(`/ghidra/riru_trace.py`); results land in `/ghidra/out/`.
- `.rela.dyn` proof: `AAssetManager_openDir` (5x) + `AAsset_seek64` (18x)
  use `R_AARCH64_ABS64` (type 257) = address-taken, called INDIRECTLY
  (vtable/interface). So dir enumeration is real; v1.0.10 hooks it.
- `fopen`/`open`/`LoadURL`-direct: NOT imported anywhere (exact-name
  search). File IO = AAsset family + raw SVC syscalls only.

## 6. v1.0.6 coverage matrix

| Load path | Covered | How |
| --------- | ------- | --- |
| `AAsset_read` streaming | yes | drain-then-serve from patched buffer |
| `AAsset_getLength` first | yes | forces drain, returns patched size |
| `AAsset_getBuffer` zero-copy | yes | drains, returns patched pointer |
| `AAsset_openFileDescriptor` (uncompressed) | yes | serves `memfd_create` fd with patched bytes |
| `fopen` disk files (`gui/dist/...` on disk, not APK) | YES (v1.0.7) | `fopen`/`fclose` hooked via `dlsym(RTLD_DEFAULT)`, original fully read, patched bytes served through `memfd_create`+`fdopen` (real fd: fstat/seek/mmap correct). Write/append modes pass through untouched. |
| raw `open`/`openat` fds, C++ fstreams | YES (v1.0.11) | `__open_2` IS imported (exact); `open`/`openat` hooked process-wide for other libs. Drain via raw syscalls (no recursion), serve via memfd (real-fd semantics), variadic-correct forwarding. |
| IDA Free pkg in /emulated (x86_64 Arch build) | N/A here | wrong arch for this aarch64 host; Ghidra 12.1.4 + Capstone cover the work |
| CoHTML custom-scheme handler internals | N/A (lands in one of the above) | — |

Every hook guarded; any failure or non-HTML content → original bytes (menu
intact, button absent, ModMenu baseline unaffected).
