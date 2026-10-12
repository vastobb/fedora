#!/usr/bin/env python3
"""Validate riru build output and package .levipack as ZIP.

Levi FileHandler.isValidZipModPackage REQUIRES a zip (ZipInputStream) with
exactly 1 .so + manifest.json at zip root or a single subdir root.
TAR files are rejected with "Invalid ZIP Mod Package" (seen 2026-10-12).
"""
import argparse, json, os, struct, zipfile, shutil, sys

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
        print("WARN: PLGetModRegistration string not found (link may still export it)")
    else:
        print("export marker PLGetModRegistration found")
    # c++_shared gate: passing std::string/std::function to libpreloader.so
    # needs shared libc++ on both sides (Levi app/build.gradle mandates it).
    with open(so, 'rb') as f:
        blob = f.read()
    if b'libc++_shared.so' not in blob:
        print("FAIL: libc++_shared.so not referenced — built with c++_static.")
        print("Rebuild with -DANDROID_STL=c++_shared (see build-riru.yml).")
        sys.exit(1)
    print("STL: libc++_shared OK")
    man = json.load(open("riru/manifest.json"))
    assert man.get("type") == "preload-native", "manifest type must be preload-native"
    entry = man.get("entry")
    assert entry, "manifest entry missing"
    assert "/" not in entry, "entry must be a plain file name for zip-root layout"
    os.makedirs(a.out, exist_ok=True)
    pkgdir = os.path.join(a.out, "riru")
    if os.path.isdir(pkgdir):
        shutil.rmtree(pkgdir)
    os.makedirs(pkgdir)
    shutil.copy(so, os.path.join(pkgdir, os.path.basename(entry)))
    shutil.copy("riru/manifest.json", os.path.join(pkgdir, "manifest.json"))
    # .levipack = ZIP with manifest.json + .so at ROOT (fast path in validator:
    # manifestRoots.contains("") && isSoUnderRoot("", so)).
    levipack = os.path.join(a.out, "riru.levipack")
    if os.path.isfile(levipack):
        os.remove(levipack)
    with zipfile.ZipFile(levipack, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(os.path.join(pkgdir, "manifest.json"), "manifest.json")
        z.write(os.path.join(pkgdir, os.path.basename(entry)), os.path.basename(entry))
    print(f"packaged {levipack}")
    with zipfile.ZipFile(levipack) as z:
        names = z.namelist()
        print("\n".join(names))
        assert "manifest.json" in names, "zip must contain manifest.json at root"
        assert sum(n.lower().endswith(".so") for n in names) == 1, "zip must contain exactly 1 .so"
    print("ZIP validation OK (mirrors FileHandler.isValidZipModPackage)")
