#!/usr/bin/env python3
"""Gate: reject unverified hooks. Candidates must not be treated as verified."""
import argparse, json, sys

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("offsets")
    ap.add_argument("--lib", required=True)
    a = ap.parse_args()
    d = json.load(open(a.offsets))
    bad = [c for c in d.get("candidates", []) if c.get("status") == "verified"]
    if bad:
        print("FAIL: entries marked verified without runtime evidence:")
        for c in bad:
            print(" ", c)
        sys.exit(1)
    print(f"OK: {len(d.get('candidates', []))} candidate(s), none claimed verified. Hook use blocked until tested.")
