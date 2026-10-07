# Lock-step with DoomV

Status: **draft for review** (Phase 0). Decisions marked **[decide]** need the
owner.

The core runs in lock-step with DoomV: every instruction it retires, and every
trap it takes, is compared with DoomV's, one at a time, and the first
difference stops the run with both sides' state. DoomV matches Sail, so a core
that matches DoomV matches the reference of record. This document fixes what
the core reports, how it reaches DoomV at each level of the project, and what
DoomV needs that it does not have yet.

## The record is Sail's trace format

DoomV already lock-steps against a trace in **Sail's format** -- the one
`sail_riscv_sim --trace-instr --trace-gpr --trace-fpr --trace-vreg --trace-csr
--trace-mem --trace-exception --trace-interrupt` writes, and which DoomV's
`-lockstep=<trace>` reads ("Sail's, or an RTL testbench's in the same shape",
`src/lockstep.cpp`). Ouroboros adopts it unchanged. There is no Ouroboros
format to design, to keep in step with DoomV, or to translate: the core's
retirements are rendered as Sail trace records, and DoomV checks them exactly
as it checks Sail.

One record per retired instruction or trap, a header line and one line per
effect:

```
[83] [U]: 0x00000000800001BC (0x00113023)
mem[W,0x0000000080002000] <- 0x00AA00AA00AA00AA
x1 <- 0x0000000000000004
CSR mstatus (0x300) <- 0x8000000A00006080
```

What the core must therefore expose per retirement, at the top of its
retirement stage -- the RVFI set, plus what Sail prints that RVFI leaves out:

| field | from | notes |
|---|---|---|
| step number | a retirement counter | Sail's `[n]`; also the order check |
| privilege and V | the privilege register at retirement | `[M]`, `[S]`, `[U]`, `[VS]`, `[VU]` |
| pc, instruction bits | the retiring instruction | 16-bit encodings as fetched |
| X, F and V register writes | the write-back stage | every register written, with its new value; a vector write as the whole register |
| CSR writes | the CSR file | every CSR whose value the instruction changed, including the ones trap entry writes |
| stores | the store unit | physical address, width and data, one entry per store as Sail splits them (a page-crossing store is two) |
| trap | the trap unit | cause, tval, tval2 and tinst, the mode trapped from and to |
| interrupt | the trap unit | which interrupt, taken before which instruction |

Loads are not in Sail's trace and are not compared directly; a wrong load
shows up as a wrong register write. Device loads matter for the event log
below.

## What cannot be predicted, and the two modes

Two things decide values the instruction set does not: the **clock** (time
and counter reads, when a timer interrupt becomes pending) and **devices**
(what a UART or disk load returns, when a device raises an interrupt). DoomV
handles them in one of two modes, both already implemented:

- **Strict** (`-lockstep-strict`): everything is compared and nothing is taken
  from the other side. DoomV is held to Sail this way, by adopting Sail's
  platform: Sail's configuration (`rva23s64.json`), its clock (one `mtime`
  tick every two instructions, the WFI wait limit), its devices. A core run
  this way is deterministic and comparable to Sail directly.
- **Lenient** (default): counter and time reads, pending-interrupt state
  (`mip`, `sip`, the `topi`/`topei` registers, `hgeip`, `seed`), and loads from
  anything that is not RAM are taken from the core's record, and interrupts
  are taken where the core took them, checking that they were enabled there.
  The run reports how many values it took. This is how hardware with its own
  real-time timer and real devices is stepped.

**The core's clock** (decisions, 2026-10-01): the core runs on the FPGA's
clock, defined the same way in Vitis software emulation, hardware emulation
and on the physical FPGA. `mcycle` counts the core's clock cycles; `mtime` is
derived from that clock; the timebase in the device tree comes from the
platform's clock frequency. Since the core's pipeline is one loop iteration
per clock cycle ([hls-coding-standard.md](hls-coding-standard.md)), even
software emulation counts real cycles.

**The references use the same clock** (decisions, 2026-10-01). Each
retirement record carries the core's cycle count, and in lock-step DoomV and
Sail take their clock from it: before stepping, they set `mtime` and `mcycle`
to what the core's clock gives at that instruction, instead of advancing
their own instruction-counted clock (one tick per two instructions). Time and
counter reads then match by construction, and a timer interrupt becomes
pending in DoomV at the cycle it does in the core -- so **lock-step is
strict**: DoomV decides when the interrupt is taken and checks that the core
took it at the same instruction, rather than taking the core's word for it.
Device loads are compared too wherever the device is modelled on both sides;
where it is not, they are the one thing taken from the core's record.

**Where the modes can differ.** The cycle count of a run depends on memory
timing. Software and hardware emulation see the latencies their memory
models give; the physical board sees the PS DDR controller's, which can vary
between runs. So the same program can take a timer interrupt at a different
instruction on the board than in emulation. That is not a mismatch: each run
is checked against DoomV driven by that run's own cycle stamps, logged on the
board (below) and replayed.

**Sail on the same clock.** Sail's C emulator advances its clock itself, by
`instructions_per_tick`. Driving it from the core's cycle stamps changes how
the emulator is run -- its clock source -- not the model. For DoomV this is a
new clock mode; DoomV's own Sail lock-step keeps Sail's clock.

## Three levels, one record

| level | the core is | DoomV is | speed (report's estimates) | used for |
|---|---|---|---|---|
| **C simulation** (Vitis software emulation) | the HLS C++, compiled natively | linked into the same process | about 10^6-10^7 instructions/s | every change: riscv-tests, riscv-vector-tests, arch-test, riscv-dv seeds, Linux and Ubuntu boots |
| **RTL simulation** (Vitis hardware emulation) | the Verilog Vitis/Vivado generate, in XSim or Verilator | linked into the testbench, handed what the RTL retired (or reading a trace it writes) | kHz | short directed tests on every synthesis: does the generated RTL do what the C++ did |
| **on the board** (hardware) | the bitstream on the FPGA | offline, replaying a recorded log | full speed; comparison offline | milestone boots; bisecting a divergence |

**C simulation is the main level**, and the reason the all-HLS rule helps
verification: the C++ that becomes the hardware can be stepped against DoomV
through a whole Ubuntu boot at software speed. Its limit is that it proves
only the C++; scheduling, stream depths and free-running behaviour are
checked by the RTL level, which is therefore mandatory on every synthesis.

**On the board**, the host link (the KV260's USB-UART/JTAG) cannot carry a
record per instruction. The core writes instead, into a ring buffer in DDR:

1. a running hash of its records, emitted every N instructions;
2. in full, every value lenient mode would take from it -- time and counter
   reads, device loads, interrupts with the instruction they came before.

DoomV replays the log offline: it takes the logged values where the core took
them and checks the hashes. A mismatching interval is re-run, from a
snapshot, with full records around it. No published project has lock-stepped
a full Linux boot over a link like this, so this part is a design to prove in
Phase 2, not a known technique.

## Starting points: snapshots

Both sides start from the same machine state, not from a program loaded
through a debug path (Dromajo's experience: loading through the debug module
caused false mismatches). DoomV's snapshots (`-snapshot`, `-restore`) are that
format already. A run can start at reset, or from a snapshot taken by DoomV --
for example, Ubuntu booted to its desktop -- loaded into the core's memory
and registers.

## What DoomV needs

| need | for | status |
|---|---|---|
| a cycle stamp per record, and a clock mode driven by it | strict lock-step on the core's clock | **exists in DoomV** (`-cycle-clock=<n>`, below); in Sail's emulator harness, to build |
| Sail-format trace reader, strict and lenient | RTL level | **exists** |
| snapshots | common starting points | **exists** |
| an in-process API: reset or restore, then compare one record at a time | C simulation at full speed (no text, no file) | **exists**: `doomv_lockstep.dll` (below) |
| replay of a value log: take this time/counter value, this device load, this interrupt before step N | board level, and lenient runs being reproducible | partly: lenient mode does this from a full trace; a compact log format is to build |
| hash checkpoints every N steps | board level | to build |
| loading a snapshot into the core | starting the core from a booted state | to design with the board contract |

These are DoomV changes, made in the DoomV repository. Ouroboros follows
DoomV's main branch: the submodule tracks `main`, and `scripts/pipeline.py`
moves it to the newest commit before every run (decisions, 2026-10-07).

**What exists, and how it is checked** (DoomV README, "Lock-stepping a core,
in Vitis"):

- **The library.** `make lockstep-lib` builds `doomv_lockstep.dll`, DoomV with
  a plain C interface (`src/doomv_lockstep.h`): open a machine with
  riscv_doom's arguments (reset, or `-restore=<snapshot>`), hand it one
  record per retired step -- a `doomv_ls_record` filled from the retirement
  port, or Sail-format text -- and get back a match or the mismatch. Static,
  so it needs nothing beside it; Vitis 2026.1's own MinGW g++ links it with
  `tb.cflags=-I<dir>` and `csim.ldflags=-L<dir> -ldoomv_lockstep` (which
  serves co-simulation too), the DLL's folder on `PATH`. One machine per
  process.
- **Both Vitis emulations, shown.** DoomV's gate synthesises a stand-in for a
  retirement port (a pass-through of record words, in DoomV's
  `tools/verification/lockstep_lib/vitis`) for the KV260's part and runs its
  testbench, which hands what comes out of the port to the library: in C
  simulation, and in C/RTL co-simulation in XSim, where the records DoomV
  checks are the ones the generated Verilog produced. Both match.
- **The core's clock.** With `-cycle-clock=<n>`, each record's cycle count --
  a line `cycle <n>` before the record in text, as `hart <i>` is, or a field
  of the structure -- sets the clock before the step: every hart's `mcycle`
  advances by the cycles since the last record (where `mcountinhibit` and
  the Smcntrpmf filters let it), `mtime` by one tick per `<n>` cycles. The
  count is the one the record's instruction sees, from 0 where the run
  starts. A WFI completes or traps by the state at its own stamp: the core's
  wait is the gap between its stamp and the one before.
- **Held to Sail.** Sail's clock is a cycle clock of one tick per two
  instructions, so DoomV writes Sail's trace back with that count as stamps
  (`-lockstep-stamp`) and runs `-cycle-clock=1` against it: all 381 one-hart
  tests DoomV's Sail lock-step runs match strictly, through the library as
  well, and a record with one value changed is caught.

## Several harts

DoomV runs `-harts=N`, and is held to `sail_riscv_mh` (N Sail models over one
memory) in strict lock-step. Both step the harts in a fixed round-robin, one
step each per round, and the trace marks each hart's records with a line
`hart <i>` before them. A record can span another hart's lines -- a WFI's
instruction line comes when its wait starts -- so the reader keeps one record
in progress per hart.

That fixed order is DoomV's and Sail's, not the core's. Harts in the FPGA run
at the same time, and the order in which their stores reach memory is the
memory system's. So for Ouroboros:

| need | for | status |
|---|---|---|
| the core's trace in the order its harts' records commit, marked `hart <i>` | any multi-hart lock-step | to design with the trace stream |
| DoomV stepping the hart the next reference record names, instead of round-robin | following the core's interleaving | **exists**: `-lockstep-follow`, implied by `-cycle-clock` and always so in-process |
| a rule for loads whose value another hart decided (the reference's commit order may not be one DoomV can reproduce if the core's memory is not sequentially consistent) | strict lock-step with real concurrency | open |

## Open

- Whether the board's memory latency can be made run-to-run constant (for
  example a fixed-latency adapter), which would make board runs repeat
  cycle-for-cycle and not only replayably.
- The record's binary form inside the core and on the trace stream (the
  rendering to Sail text happens outside the hardware).
- Vector register writes are 128 bits each; whether the stream carries whole
  registers or only changed ones is a stream-width question for the board
  contract.
