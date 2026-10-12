# Reverse notes — `/emulated/libminecraftpe.so`

> Measured with **lightweight `python3` only** (ELF header + section table + string scans). No Ghidra/IDA/Hopper run locally. Heavy analysis lives in `.github/workflows/reverse.yml`.

## Identity

| Fact | Value |
| ---- | ----- |
| Path | `/emulated/libminecraftpe.so` |
| Size | `328,419,680` bytes (`313.2` MiB) |
| SHA256 | `5f5baf8e9ded92432e4c2cc9ff58a9313245323e8ca53ce37c169cc720e757c4` |
| ELF | `7f 45 4c 46`, class `2` (64-bit), endian `1` (LE), type `3` (shared), machine `183` (AArch64) |
| PH / SH | `e_phnum=13`, `e_shnum=28`, `e_shstrndx=27` |

## Sections (off / size)

| # | Name | Off | Size |
| - | ---- | --- | ---- |
| 1 | `.note.android.ident` | 324927488 | 152 |
| 2 | `.note.gnu.build-id` | 324927640 | 36 |
| 3 | `.dynsym` | 324927680 | 2,200,224 (2.1 MiB) |
| 4 | `.gnu.version` | 327127904 | 183,352 |
| 5 | `.gnu.version_r` | 327311264 | 128 |
| 6 | `.gnu.hash` | 2384440 | 715,424 |
| 7 | `.dynstr` | 3099864 | 8,385,475 (8.0 MiB) |
| 8 | `.rela.dyn` | 11485344 | 24,185,592 (23.1 MiB) |
| 9 | `.rela.plt` | 327311392 | 1,100,928 |
| 10 | `.rodata` | 36771840 | 20,433,952 (19.5 MiB) |
| 11 | `.gcc_except_table` | 57205792 | 9,292,748 |
| 12 | `.eh_frame_hdr` | 66498540 | 6,033,892 |
| 13 | `.eh_frame` | 72532432 | 33,137,420 |
| 14 | `.text` | 105669856 | 205,828,044 (196.3 MiB) |
| 15 | `malloc_hook` | 311497900 | 768 |
| 16 | `.plt` | 311498672 | 733,968 |
| 17 | `.data.rel.ro` | 312232640 | 9,079,696 |
| 18–25 | `.fini_array` … `.data` / `.bss` | — | small |

## Key observations

1. **Stripped game symbols.** `.dynstr` (`8.0` MiB, `73,340` `_ZN` mangled) contains CoHTML/system exports (`cohtml::`, `GameActivity_*`, `Xal`, `renoir`) but **no** `ClientInstance` / `MinecraftGame` / `Screen` game classes. `Screen` hits in `.dynstr` (51) are all CoHTML/DOM (`cohtml::dom::Screen`, `GamepadButton`, `GlyphOnScreen`), not Bedrock UI. → Offsets **cannot** come from `dlsym`; must come from **signature scan** (`pl::memory::resolveSignature`) + Ghidra.
2. **Version strings in `.rodata`.** `1.21.80×4`, `1.21.110×4`, `1.21.90×4`, `1.21.120×3`, plus `1.21.60/130/0/40/70`. Use `versionCode` gating in manifest (`minecraft_versions`) and verify at runtime; do not assume single version.
3. **UI strings in `.rodata`.** `Screen×9281`, `Button×1179`, `Menu×975`, `OreUI×7798`, `ScreenController×3852`, `MainMenu×265`, `PlayScreen×226`, `StartScreen×15`, `StartMenu×45`, `oreui×15`, `createScreen×2`. `StartScreen` context shows `registerPreStartScreen` / `ScreenTechStackSelector` — Bedrock main menu is **OreUI tech-stack**, not legacy `Gui`. → A “main menu button” via raw hook means hooking OreUI/ScreenController creation, which is version-fragile. The supported test path is ModMenu floating button (works on main-menu HUD layer too).
4. **No `riru`/`ModMenu` strings** in lib (`0` hits) — as expected; those live in `libpreloader.so`/launcher, not the game.
5. **First-5-MB scan** (ELF headers + `.dynstr` start) shows `libGLESv2.so`, `libHttpClient.Android.so`, `libminecraftpe.so` references — normal Android game linkage.

## What `reverse.yml` must produce (Actions-side)

- Ghidra headless analyze → `strings.json` (OreUI/PlayScreen/StartScreen refs), `exports.json` (dynamic symbols), `xrefs.json` (callers of `registerPreStartScreen`-adjacent + `ScreenController` factories).
- Signature candidates for: (a) Screen factory / push-screen, (b) button/widget creation reachable from StartScreen, (c) stable `JNI_OnLoad`-adjacent anchors. Each with **pattern + module + version string context**.
- `offsets.json` with **only tested** entries: `{symbol, pattern, offset_or_rva, tested_on_version, status: verified|candidate|rejected, evidence}`. `riru` hook scaffold consumes this file; anything `candidate`/`rejected` must **not** be used for hooking (fall back to ModMenu-only).
- Report (`reverse-report.md`) stating tool versions (Ghidra, JDK), exact lib SHA256, Ghidra scripts used, and limitations (stripped, OreUI, CoHTML).

## Reproduce (lightweight, local-safe)

```bash
python3 - <<'PY'
import struct, re
p='/emulated/libminecraftpe.so'
with open(p,'rb') as f:
    h=f.read(64)
    print(struct.unpack('<H',h[18:20])[0])  # 183 = AArch64
PY
```

Full heavy steps are in `reverse.yml` — do not run them here.
