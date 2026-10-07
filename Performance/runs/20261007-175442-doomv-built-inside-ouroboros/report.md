# DoomV built inside Ouroboros

Run `20261007-175442-doomv-built-inside-ouroboros`, commit `cfcc74c`, target `doomv-stub`, tools 2026.1.

Notes:

- DoomV stands in for the core: this proves the harness, not a design

## Accuracy (strict lock-step against DoomV)

| suite | tests | match | mismatch | error | skip | test passed | instructions | instructions/s | time |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| riscv-tests | 10 | 10 | 0 | 0 | 0 | 10 | 1,128 | 467 | 0.6 s |
| riscv-tests-priv | 10 | 10 | 0 | 0 | 0 | 10 | 2,005 | 1,853 | 0.4 s |

## Against the previous comparable run

None: this is the first run of its kind.
