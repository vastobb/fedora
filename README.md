# fedora — LeviLaunchroid `.so` mod research

Research repo for [LiteLDev/LeviLaunchroid](https://github.com/LiteLDev/LeviLaunchroid) native (`.so`) mod loading.

> Scope: **only `.so` mods** (`type: preload-native`). No Java/Kotlin-only mods, no resource-pack/world manager docs here.

## Contents

| Path | Purpose |
| ---- | ------- |
| `docs/SO_MOD_LOADING.md` | How `.so` mod loading works, verified from source via `gh` API (no clone). |
| `docs/ARCHITECTURE.md` | Markdown architecture: components, sequence, filesystem layout. |
| `docs/REVERSE_NOTES.md` | Facts measured from `/emulated/libminecraftpe.so` (lightweight `python3` only, no heavy local RE). |
| `riru/` | Levi mod named `riru` — adds a **Riru Test** button for testing. Offset-free ModMenu implementation + optional hook scaffold. |
| `.github/workflows/reverse.yml` | Heavy RE (Ghidra headless + signature scan) runs **in GitHub Actions**, not locally. |
| `.github/workflows/build-riru.yml` | NDK `arm64-v8a` build + `.levipack` packaging + validation in Actions. |
| `tools/REA_SKILL.md` | What was taken from [`morluto/rea`](https://github.com/morluto/rea) and how it is used (Actions-side only). |
| `lib/README.md` | How `libminecraftpe.so` is stored (Release asset, not git blob) + verification. |

## Quick start

1. Read `docs/SO_MOD_LOADING.md`, then `docs/ARCHITECTURE.md`.
2. For the test mod: see `riru/README.md`, build via Actions (`build-riru`), or locally with NDK if you have it.
3. For offsets/functions: run Actions → `RE: libminecraftpe (Ghidra, Actions-only)` → download `reverse-report` artifact → copy verified values into `riru/src/RiruTest.cpp` hook section. **Do not run Ghidra locally** per this repo's policy.
4. Import `riru.levipack` into LeviLaunchroid: copy unpacked `riru/` dir into the version's `modsDir`, or import the `.levipack`.

## Verified bases

- Launcher: `LiteLDev/LeviLaunchroid@main`, release `v1.5.27` (checked 2026-10-12 via `gh api`).
- SDK: `LiteLDev/preloader-android`, release `0.2.3`, submodule at `app/src/main/cpp/preloader`.
- Game lib: `/emulated/libminecraftpe.so`, `328,419,680` bytes, ELF64-LE AArch64, `.text` ~196 MiB, stripped game symbols (see `docs/REVERSE_NOTES.md`).
- RE skill: [`morluto/rea`](https://github.com/morluto/rea) — used as methodology reference for Actions workflow, not executed locally.

## Policy

- No `git clone` of LeviLaunchroid was used for diagnosis (only `gh api` + `gh repo view`).
- No heavy RE tools run locally. Ghidra / signature scans run in `reverse.yml`.
- `libminecraftpe.so` is **not** committed as a git blob (314 MiB > 100 MiB GitHub file limit). It is published as a **Release asset** (`libminecraftpe.so`) + `sha256.txt`; workflows download it on demand. See `lib/README.md`.
