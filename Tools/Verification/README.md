This folder contains doomV, which the physical hardware will be built around.

# Verification

Ouroboros's core is held to one reference: **DoomV**, an RVA23S64 emulator that
matches the Sail RISC-V model instruction by instruction. The hardware is built
around it -- every retired instruction the core reports is checked against
DoomV, and DoomV is checked against Sail. Sail is the specification of record;
where Sail and anything else disagree, Sail is right.

Everything here is a git submodule, pinned to a commit -- except DoomV, which
follows its `main` branch: `scripts/pipeline.py` moves it to the newest
commit before every run and rebuilds it if it was built here, and every run's
report names the DoomV commit it used. By hand: `git submodule update
--remote Tools/Verification/DoomV`. Nothing is vendored.

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

The lock-step itself is described in [docs/lockstep.md](../../docs/lockstep.md).
How the core's retirements reach DoomV on the board is still a design to prove.

## Launching SW-Emu and HW-Emu

The core runs in one of Vitis HLS's two emulations, and DoomV, inside the
testbench's process, checks every instruction it retires, strictly. The
harness is DoomV's `corun.py`; run it from the repository root.

**SW-Emu** -- Vitis software emulation (C simulation). The core's C++,
compiled natively once per run, then run on every test. Fast: the level for
every change.

```
python Tools/Verification/DoomV/tools/verification/corun.py --lockstep sw-emu --sims none --suite riscv-tests
```

**HW-Emu** -- Vitis hardware emulation (C/RTL co-simulation). The core is
synthesised once per run and the generated Verilog runs in XSim on each
test, one at a time; DoomV checks the records the RTL produced. Slow: give it
a short list, after each synthesis.

```
python Tools/Verification/DoomV/tools/verification/corun.py --lockstep hw-emu --sims none --suite riscv-tests --count 20
```

**Which simulators.** `--sims none` is the usual run: the core against DoomV
alone. DoomV is always there; `--sims` adds others, which run the same
program beside the core and are compared with what it retired -- they do not
decide the verdict, but at a mismatch they show whether the reference agrees
with DoomV or with the core:

| `--sims` | beside the core and DoomV |
|---|---|
| `none` | nothing -- **the usual run** |
| `sail` | Sail, to confirm a mismatch against the specification itself |
| `sail,spike,whisper,qemu` | all four (also what leaving `--sims` out does) |

**Which core and which programs.** `--component <dir>` is the core's Vitis
HLS component (until the core exists, a stand-in from DoomV); `--suite
riscv-tests` with test names or `--count N`, or ELF paths, chooses the
programs. The exit status is 0 only if the core matched DoomV on every one.
Everything else -- the core testbench's contract, reading the report, the
snapshot a mismatch leaves, prerequisites -- is in
[docs/simulation-harness.md](../../docs/simulation-harness.md).

### In the Ouroboros flow

The same runs happen on their own when Ouroboros builds a CPU
([docs/ouroboros-flow.md](../../docs/ouroboros-flow.md), decisions
2026-10-07):

1. **Generate the CPU.** Vitis HLS synthesises the core and the other
   components.
2. **Ask.** *Test the CPU with DoomV?* -- **SW-Emu**, **HW-Emu**, **both**
   (recommended) or **skip**. A saved configuration remembers the answer,
   so a re-run does not ask again.
3. **Lock-step.** The chosen emulations run, DoomV only (`--sims none`):

   | emulation | runs | what it shows |
   |---|---|---|
   | SW-Emu | every test of the suites: riscv-tests, the M- and S-mode tests, DoomV's directed lock-step tests | the C++ that becomes the hardware is right |
   | HW-Emu | a short directed set, after SW-Emu if both were chosen | the generated RTL does what the C++ did |

   A mismatch stops the flow before the bitstream, with the test, the
   instruction, the field and the DoomV snapshot from just before it.
   Ouroboros asks whether to stop there or build the bitstream anyway; a
   bitstream built past a mismatch, or with the test skipped, is marked
   unverified in the build's report.
4. **Bitstream.** Vivado implements the design and writes the bitstream,
   then the boot image.

Each test's report is kept in the build's `lockstep/` folder, and the
results are part of the build's report.
