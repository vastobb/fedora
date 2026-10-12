#!/usr/bin/env python3
"""Static string/signature scan → offsets.json (candidates) + reverse-report.md.

REA-inspired methodology (static only): collect OreUI/Screen strings from
.rodata, record file offsets + surrounding context, propose byte-pattern
candidates for Ghidra follow-up. NOT runtime-verified — hook use requires
further testing (see validate_offsets.py + reverse.yml).
"""
import argparse, json, re, hashlib, struct, os
from collections import Counter

RODATA_OFF, RODATA_SIZE = 36771840, 20433952
DYNSTR_OFF, DYNSTR_SIZE = 3099864, 8385475

INTEREST = [b'OreUI', b'StartScreen', b'PlayScreen', b'MainMenu', b'ScreenController',
            b'StartMenu', b'createScreen', b'1.21.']

def sha256(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for c in iter(lambda: f.read(8 << 20), b''):
            h.update(c)
    return h.hexdigest()

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("lib")
    ap.add_argument("--out", required=True)
    ap.add_argument("--report", required=True)
    a = ap.parse_args()
    size = os.path.getsize(a.lib)
    digest = sha256(a.lib)
    with open(a.lib, 'rb') as f:
        f.seek(RODATA_OFF)
        ro = f.read(RODATA_SIZE)
    counts = {k.decode(): ro.count(k) for k in INTEREST}
    vers = Counter(re.findall(rb'1\.21\.[0-9]+(?:\.[0-9]+)?', ro)).most_common(10)
    # sample contexts for StartScreen
    ctxs = []
    for m in re.finditer(b'StartScreen', ro):
        s = max(0, m.start() - 64)
        ctx = ro[s:m.end() + 64]
        txt = ''.join(chr(c) if 32 <= c < 127 else '.' for c in ctx)
        ctxs.append({"rodata_off": RODATA_OFF + m.start(), "ctx": txt[:160]})
        if len(ctxs) >= 10:
            break
    offsets = {
        "lib_sha256": digest,
        "lib_size": size,
        "note": "candidates only — NOT runtime-verified. Do not hook without testing.",
        "rodata_counts": counts,
        "version_strings": [{"s": k.decode(), "n": n} for k, n in vers],
        "startscreen_contexts": ctxs,
        "candidates": [
            {"id": "oreui-startflow",
             "desc": "OreUI StartScreen/ScreenController string refs in .rodata; resolve callers in Ghidra XREFs",
             "status": "candidate",
             "evidence": f"StartScreen x{counts.get('StartScreen',0)}, ScreenController x{counts.get('ScreenController',0)} in .rodata"},
            {"id": "mainmenu-rodata",
             "desc": "MainMenu/PlayScreen refs; Ghidra: find code XREFs to these .rodata addresses for button factory",
             "status": "candidate",
             "evidence": f"MainMenu x{counts.get('MainMenu',0)}, PlayScreen x{counts.get('PlayScreen',0)}"},
        ],
    }
    with open(a.out, 'w') as f:
        json.dump(offsets, f, indent=2)
    with open(a.report, 'w') as f:
        f.write(f"# Reverse report — libminecraftpe.so\n\n")
        f.write(f"SHA256: `{digest}`\n\nSize: {size} bytes\n\n")
        f.write("## Method (Actions-only, static)\n\n")
        f.write("- `elf_triage.py` section parse; `.rodata` scan (python); Ghidra headless import/auto-analyze.\n")
        f.write("- REA methodology ref: static strings → XREFs → signature candidates → validate before hook.\n\n")
        f.write("## Findings\n\n")
        for k, v in counts.items():
            f.write(f"- `{k}`: {v}\n")
        f.write("\n## Version strings\n\n")
        for k, n in vers:
            f.write(f"- `{k.decode()}` x{n}\n")
        f.write("\n## Status\n\nAll entries are `candidate`. Runtime test (Actions validator + on-device ModMenu fallback) required before any hook ships. `riru` default stays ModMenu-only.\n")
    print(f"wrote {a.out} + {a.report}")

if __name__ == "__main__":
    main()
