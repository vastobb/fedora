# REA skill — what was used and how

Source: [`morluto/rea`](https://github.com/morluto/rea) — “Reverse Engineer Anything”, MCP for native/JS/.NET/web targets. Checked via `gh api repos/morluto/rea/...` (no local install, no heavy run).

## What was taken (methodology only)

- **Native workflow**: import binary into analysis engine (Ghidra/Hopper/IDA) → strings/symbols → XREFs/callers → pseudocode → signature/offset candidates → evidence + limitations → implement + test. Ref: `docs/native-investigation.md`, `docs/ghidra-provider.md`, `docs/cli.md` in `rea`.
- **Evidence obligation**: every offset needs `{pattern, module, version context, tool version, status}`. Anything without runtime confirmation stays `candidate`. This repo enforces it in `tools/validate_offsets.py` + `reverse.yml`.
- **Provider choice**: Ghidra headless in Actions (free, scriptable). Hopper/IDA need licenses; not used here. REA supports all three — we picked Ghidra for CI.
- **No local heavy run**: per repo policy, `rea` MCP/CLI and Ghidra run **only in `reverse.yml`**. Local work is limited to `python3` ELF triage + string scans (`tools/elf_triage.py`, `tools/sig_scan.py`).

## What was NOT done

- No `npx rea-agents setup`, no MCP registration, no `rea` binary install locally.
- No upload of the 314 MiB lib to any third-party service; it stays as a GitHub Release asset in this repo and is downloaded by Actions with `gh release download`.
- No blind copy of REA prompts/workflows — only the evidence-graded approach above, adapted to Levi `pl::memory::resolveSignature` + `HookHandle` testing.

## To reproduce the REA-equivalent in Actions

1. `reverse.yml` → Ghidra `analyzeHeadless` import + auto-analyze → `sig_scan.py` string contexts → `offsets.json` (candidates).
2. Human/Ghidra follow-up: open `reverse-report` artifact, find XREFs to `StartScreen`/`ScreenController` `.rodata` addresses, derive byte patterns, add to `offsets.json` with `status: candidate`.
3. Runtime gate: `validate_offsets.py` fails if anything claims `verified` without test. Real `verified` requires on-device/validator run + version-gate, then opt-in `RIRU_USE_HOOKS=1` rebuild via `build-riru.yml`.

Upstream docs: https://rea.tools/guides/native/ and `morluto/rea/docs/*`.
