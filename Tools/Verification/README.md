This folder contains doomV, which the physical hardware will be built around.

# Verification

Ouroboros's core is held to one reference: **DoomV**, an RVA23S64 emulator that
matches the Sail RISC-V model instruction by instruction. The hardware is built
around it -- every retired instruction the core reports is checked against
DoomV, and DoomV is checked against Sail. Sail is the specification of record;
where Sail and anything else disagree, Sail is right.

Everything here is a git submodule, pinned to a commit. Nothing is vendored.

```
Tools/Verification/
  DoomV/                     the golden model, and its own harness (Sail, Spike,
                             riscv-arch-test, OpenSBI, Linux are its submodules)
  suites/
    riscv-formal/            formal checks on the core's RVFI retirement port
    riscv-dv/                random instruction streams, run on the core and on
                             DoomV, compared instruction by instruction
    riscv-tests/             the base ISA and rv64mi/si, as source -- rebuilt for
                             bring-up configurations (no V, no H, small memory)
    riscv-vector-tests/      V at VLEN=128, as source, for the same reason
```

## Getting them

```sh
git submodule update --init                      # the five above, shallow where large
git -C Tools/Verification/DoomV submodule update --init tools/verification/simulators/sail/src tools/verification/tests/arch-test
bash Tools/Verification/DoomV/tools/verification/tests/suites/fetch.sh
```

The last line fetches the prebuilt riscv-tests, the DAMO hypervisor tests and
riscv-vector-tests at VLEN=128 from `sail-riscv-tests`' releases, which DoomV's
suites already run against Sail. DoomV's Linux, OpenSBI and BusyBox submodules
are only needed to rebuild the guests; leave them uninitialised otherwise --
Linux alone is gigabytes.

## What each suite is for

| suite | checks | against | when |
|---|---|---|---|
| riscv-arch-test (in DoomV) | RVA23S64 conformance, signature-diffed | Sail, via DoomV | every core change |
| riscv-tests, riscv-vector-tests | broad ISA behaviour, self-checking | the test itself, then DoomV lock-step | bring-up, every change |
| DAMO tests (prebuilt, via DoomV) | the H extension -- arch-test has none | Sail, via DoomV | once H exists |
| riscv-dv | random streams: hazards, exceptions, CSR corners the directed suites miss | DoomV lock-step | nightly, many seeds |
| riscv-formal | per-instruction correctness of the retirement port, bounded proofs | the ISA semantics in riscv-formal | per pipeline change |
| DoomV's own lock-step tests | the corners found so far (misaligned splits, rounding modes, fences, clock) | Sail | every core change |
| Linux / Ubuntu boot | everything at once | DoomV, lock-stepped from a snapshot | milestones |

The lock-step itself is described in [docs/lockstep.md](../../docs/lockstep.md),
and how to run it -- the core in Vitis's software or hardware emulation,
against DoomV -- in [docs/simulation-harness.md](../../docs/simulation-harness.md).
How the core's retirements reach DoomV on the board is still a design to prove.
