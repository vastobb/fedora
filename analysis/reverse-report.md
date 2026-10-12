# Reverse report — libminecraftpe.so

SHA256: `5f5baf8e9ded92432e4c2cc9ff58a9313245323e8ca53ce37c169cc720e757c4`

Size: 328419680 bytes

## Method (Actions-only, static)

- `elf_triage.py` section parse; `.rodata` scan (python); Ghidra headless import/auto-analyze.
- REA methodology ref: static strings → XREFs → signature candidates → validate before hook.

## Findings

- `OreUI`: 7798
- `StartScreen`: 15
- `PlayScreen`: 226
- `MainMenu`: 265
- `ScreenController`: 3852
- `StartMenu`: 45
- `createScreen`: 2
- `1.21.`: 20

## Version strings

- `1.21.80` x4
- `1.21.110` x4
- `1.21.90` x4
- `1.21.120` x3
- `1.21.60` x1
- `1.21.130` x1
- `1.21.0` x1
- `1.21.40` x1
- `1.21.70` x1

## Status

All entries are `candidate`. Runtime test (Actions validator + on-device ModMenu fallback) required before any hook ships. `riru` default stays ModMenu-only.
