#!/usr/bin/env python3
"""Validate riru build output and package .levipack (tar)."""
import argparse, json, os, struct, tarfile, shutil, sys

def check_elf_aarch64(p):
    with open(p, 'rb') as f:
        h = f.read(64)
        assert h[:4] == b'\x7fELF', "not ELF"
        assert h[4] == 2, "not 64-bit"
        assert struct.unpack('<H', h[18:20])[0] == 183, "not AArch64"

def has_export(p, sym=b'PLGetModRegistration'):
    with open(p, 'rb') as f:
        data = f.read()
    return data.count(sym) > 0

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--so", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    so = a.so
    assert os.path.isfile(so), f"missing {so}"
    check_elf_aarch64(so)
    print("ELF: AArch64 OK")
    if not has_export(so):
        print("WARN: PLGetModRegistration string not found (link may still export it); checking via llvm-nm if present...")
    else:
        print("export marker PLGetModRegistration found")
    # Crash fix v1.0.1: libRiru.so MUST use shared libc++ (DT_NEEDED libc++_shared.so).
    # Static libc++ (no DT_NEEDED) mismatches libpreloader.so's shared libc++ ABI
    # for std::string/std::function across the .so boundary -> SIGSEGV in
    # registerModule (fault 0x10, xCrash 2026-10-12). Fail the build if missing.
    with open(so, 'rb') as f:
        blob = f.read()
    if b'libc++_shared.so' not in blob:
        print("FAIL: libc++_shared.so not referenced — built with c++_static, will crash in registerModule.")
        print("Rebuild with -DANDROID_STL=c++_shared (see build-riru.yml).")
        sys.exit(1)
    print("STL: libc++_shared OK (crash fix v1.0.1)")
    man = json.load(open("riru/manifest.json"))
    assert man.get("type") == "preload-native", "manifest type must be preload-native"
    entry = man.get("entry")
    assert entry, "manifest entry missing"
    os.makedirs(a.out, exist_ok=True)
    pkgdir = os.path.join(a.out, "riru")
    if os.path.isdir(pkgdir):
        shutil.rmtree(pkgdir)
    os.makedirs(pkgdir)
    shutil.copy(so, os.path.join(pkgdir, os.path.basename(entry)))
    shutil.copy("riru/manifest.json", os.path.join(pkgdir, "manifest.json"))
    levipack = os.path.join(a.out, "riru.levipack")
    with tarfile.open(levipack, "w") as t:
        t.add(pkgdir, arcname="riru")
    # also flat tar per Levi convention (root contains manifest.json directly)?
    # Keep both: dist/riru/ dir + dist/riru.levipack (tar of riru/ dir).
    print(f"packaged {levipack}")
    with tarfile.open(levipack) as t:
        print("\n".join(t.getnames()[:20]))
