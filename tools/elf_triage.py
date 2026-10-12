#!/usr/bin/env python3
"""Lightweight ELF triage for libminecraftpe.so (no Ghidra)."""
import argparse, struct, json, os

def parse(p):
    size = os.path.getsize(p)
    with open(p, 'rb') as f:
        hdr = f.read(64)
        assert hdr[:4] == b'\x7fELF', "not ELF"
        e_machine = struct.unpack('<H', hdr[18:20])[0]
        e_phnum = struct.unpack('<H', hdr[56:58])[0]
        e_shoff = struct.unpack('<Q', hdr[40:48])[0]
        e_shnum = struct.unpack('<H', hdr[60:62])[0]
        e_shstrndx = struct.unpack('<H', hdr[62:64])[0]
        f.seek(e_shoff)
        shdrs = []
        for _ in range(e_shnum):
            d = f.read(64)
            shdrs.append(struct.unpack('<IIQQQQ', d[:40]) + struct.unpack('<IIQQ', d[40:64]))
        f.seek(shdrs[e_shstrndx][4])
        shstr = f.read(shdrs[e_shstrndx][5])
        def nm(i):
            e = shstr.find(b'\x00', i)
            return shstr[i:e].decode()
        secs = [{"name": nm(n), "off": off, "size": sz} for (n, _t, _f, _a, off, sz, *_r) in shdrs]
    return {"path": p, "size": size, "e_machine": e_machine, "e_phnum": e_phnum, "sections": secs}

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("lib")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    with open(a.out, "w") as f:
        json.dump(parse(a.lib), f, indent=2)
    print(f"wrote {a.out}")
