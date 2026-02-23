# smolnes Vendoring Notes

This directory is a vendored copy of `smolnes` from:

- Upstream: `https://github.com/binji/smolnes`
- Upstream commit: `f7edb2640b7bb3a89c3b7c8c5bde1d2a8f01967c`

## Retained Files

We intentionally keep a minimal subset needed for DirtSim integration:

- `deobfuscated.c` (compiled-in emulator core source used by DirtSim).
- `LICENSE` (upstream MIT license text).
- `README.md` (upstream project context).

## License

`smolnes` is MIT-licensed. Keep the upstream license text in `LICENSE` and keep the upstream
copyright notice.

When we modify files in this directory, we keep the upstream license file in place and retain all
existing copyright/license notices.
