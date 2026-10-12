# lib/ — `libminecraftpe.so` storage

The game binary is **not** committed as a git blob.

- Source: `/emulated/libminecraftpe.so` on the research machine.
- Size: `328,419,680` bytes (313.2 MiB) — exceeds GitHub's 100 MiB per-file git limit.
- SHA256: `5f5baf8e9ded92432e4c2cc9ff58a9313245323e8ca53ce37c169cc720e757c4`
- Published as: GitHub **Release asset** `libminecraftpe.so` under tag `lib-v1` (+ `sha256.txt`).
- Workflows fetch it on demand:

```bash
gh release download lib-v1 --repo vastobb/fedora --pattern 'libminecraftpe.so' --dir work/lib
sha256sum -c sha256.txt
```

Why Release (not git LFS / chunks):

- No `git-lfs` on the research host; chunked 45 MiB parts would bloat clone and still need reassembly.
- Release assets allow up to 2 GiB per file and keep the repo clone small.
- `reverse.yml` and future validator jobs download only when dispatched.

To (re-)publish:

```bash
gh release create lib-v1 --repo vastobb/fedora --title "libminecraftpe" --notes "Game lib for Actions-side RE. SHA256: 5f5baf..." || true
gh release upload lib-v1 /emulated/libminecraftpe.so --repo vastobb/fedora --clobber
sha256sum /emulated/libminecraftpe.so | gh release upload lib-v1 --repo vastobb/fedora --clobber -- -  # or save as sha256.txt first
```

Do **not** `git add` the `.so`. This directory keeps only this README + verification metadata.
