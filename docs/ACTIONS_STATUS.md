# Actions status (2026-10-12) — ALL GREEN

## Root cause of early failures (fixed)

1. **Private repo → billing block.** `vastobb/fedora` was `private:true`. Private runners hit `recent account payments have failed or spending limit needs to be increased`, 0 steps, no log (runs `38202873181`, `38202878248`, …). Fixed via `gh repo edit vastobb/fedora --visibility public --accept-visibility-change-consequences` → now `public`, free Actions minutes apply.
2. **Wrong Ghidra asset date.** `reverse.yml` used `..._PUBLIC_20250226.zip` (9-byte 404). Correct for `11.3.1` is `..._PUBLIC_20250219.zip` (verified via `gh api repos/NationalSecurityAgency/ghidra/releases/tags/Ghidra_11.3.1_build`). Fixed + API fallback.

## Verified successful runs

| Workflow | Run | Result |
| -------- | --- | ------ |
| Build riru (NDK) | `38204144574` | `success` in `1m11s`, all 8 steps green |
| RE libminecraftpe (Ghidra) | `38204371810` | `success` in `51s` |

Re-run:

```bash
gh workflow run "Build riru (NDK, Actions-only)" --repo vastobb/fedora --ref main
gh workflow run "RE: libminecraftpe (Ghidra, Actions-only)" --repo vastobb/fedora --ref main
gh run list --repo vastobb/fedora --limit 5
```

## Build output (verified)

- `libRiru.so`: `736,840` B, ELF64 AArch64 (`e_machine 183`), `PLGetModRegistration x5`, `Riru Test x7`, `riru.test_button x1`.
- `manifest.json`: `type preload-native`, `entry libRiru.so`.
- `riru.levipack`: tar `riru/`, `riru/libRiru.so`, `riru/manifest.json`.
- Published: Release `riru-v1.0.0` assets `riru.levipack` (`747,520` B) + `libRiru.so`. Import the `.levipack` into LeviLaunchroid.
- Artifact: `riru-levipack` from run `38204144574`.

## Reverse output (verified, candidates only)

- `reverse-report` artifact from run `38204371810`: `out/reverse-report.md`, `out/offsets.json`, `triage.json`, `lib/sha256.txt`.
- Lib SHA256 `5f5baf8e...720e757c4`, `328,419,680` B. `.rodata`: `OreUI x7798`, `ScreenController x3852`, `MainMenu x265`, `PlayScreen x226`, `StartScreen x15` (`registerPreStartScreen`), versions `1.21.80/110/90/120`.
- All hook entries `status: candidate` — `validate_offsets.py` gates `verified`. `riru` stays `RIRU_USE_HOOKS=0` (ModMenu-only, no offsets needed).

## Workflow hardening

- `runs-on: ubuntu-24.04` (pin).
- NDK via `nttld/setup-ndk@v1` (`r27`).
- Ghidra resolve via fixed date + `gh api` fallback.
