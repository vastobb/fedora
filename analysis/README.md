# analysis/ — lightweight local results (no Ghidra)

Generated with `python3` only (no heavy RE locally), from `/emulated/libminecraftpe.so`:

- `triage.json` — `tools/elf_triage.py` output (ELF64 AArch64, sections).
- `offsets.json` — `tools/sig_scan.py` output: **candidates only**, `status: candidate`. Must not be used for hooking until tested.
- `reverse-report.md` — human summary of the same scan.

Heavy Ghidra headless analysis runs in `.github/workflows/reverse.yml` (Actions-only).
That job is currently blocked by account billing (see `docs/ACTIONS_STATUS.md`); re-run it when billing is fixed to get XREFs + function recovery.
