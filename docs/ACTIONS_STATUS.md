# Actions status (2026-10-12)

Both workflows are **correctly defined** but cannot run right now.

## Failure (not a YAML bug)

`gh api .../check-runs/.../annotations` for latest runs:

```text
The job was not started because recent account payments have failed
or your spending limit needs to be increased.
Please check the 'Billing & plans' section in your settings
```

- `Build riru (NDK)` runs `38202873181`, `38202465209`, `38202458362` → `failure`, 0 steps.
- `RE: libminecraftpe (Ghidra)` run `38202878248` → `failure`, 0 steps.

Runner never started; no log. Fix billing/spending limit, then re-dispatch (no code change needed).

## Re-run when fixed

```bash
gh workflow run "Build riru (NDK, Actions-only)" --repo vastobb/fedora
gh workflow run "RE: libminecraftpe (Ghidra, Actions-only)" --repo vastobb/fedora --ref main
gh run list --repo vastobb/fedora --limit 5
gh run view <id> --repo vastobb/fedora --log-failed | head -n 200
```

## What is already validated without Actions

- `riru/manifest.json` parses, `type: preload-native`, `entry: libRiru.so` exists in package plan.
- `tools/elf_triage.py` + `tools/sig_scan.py` + `tools/validate_offsets.py` pass locally on `/emulated/libminecraftpe.so` (see `analysis/`).
- `tools/package_riru.py` logic reviewed; full NDK compile + `PLGetModRegistration` export check runs in `build-riru.yml` once runners work.
- `riru` default (`RIRU_USE_HOOKS=0`) needs **no game offsets** — ModMenu button path is the tested-safe route. Hook path stays gated until `reverse.yml` yields `verified` offsets.

## Workflow hardening done

- `runs-on: ubuntu-24.04` (pin, avoid `ubuntu-latest` → 26 migration notice).
- NDK via `nttld/setup-ndk@v1` (`ndk-version: r27`) instead of only `android-actions/setup-android` package string.
