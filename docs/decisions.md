# Decisions

The owner's decisions, newest first. Where one overrides a research report in
`agentic/reports/`, it says so; the report itself is left as written.

## 2026-10-07

**Several branch predictors and execution models, out of order included.**
The configurator offers branch direction predictors from none and static
through 1-bit and 2-bit counters to two-level, gshare, tournament and TAGE,
with a BTB, return-address stack and indirect predictor; and execution models
from in-order with stalls through scoreboarding and Tomasulo to explicit
renaming, with issue width and the out-of-order structures sized
([configurator.md](configurator.md), CORE-7, CORE-19 to CORE-23). Every model
commits in program order through a reorder buffer, so the lock-step against
DoomV is the same for all of them. Overrides AGENTS.md's "simple pipeline,
stalls for hazards", which becomes the first model built and the KV260's
recommendation.

**Always the newest DoomV.** Ouroboros follows DoomV's `main` rather than a
pinned commit: the submodule tracks `main`, `scripts/pipeline.py` moves it to
the newest commit before `check`, `run` and `selftest` (and rebuilds it where
it is built), and each run's report records the DoomV commit it ran against,
so a result can still be reproduced. The submodule's recorded commit is moved
to DoomV's newest with every DoomV change.

**Configuration options are what DoomV supports.** Anything DoomV supports is
fair game -- the hart count, VLEN, the core's microarchitecture, branch
prediction -- and nothing else: an architectural option exists only where
DoomV can be configured to the same machine. The interrupt controller is AIA
only. Overrides the configurator's fixed single hart and VLEN of 128, its
PLIC option, and the ISA options DoomV does not implement
([configurator.md](configurator.md)).

**The CPU is tested with DoomV between its generation and the bitstream.**
Ouroboros generates the CPU (Vitis HLS synthesis), then asks whether to test
it with DoomV, offering SW-Emu (Vitis software emulation), HW-Emu (Vitis
hardware emulation) or both; the chosen lock-steps run automatically, and
only then does the flow proceed to bitstream generation
([ouroboros-flow.md](ouroboros-flow.md), steps 7-9; how to run the same by
hand: [Tools/Verification/README.md](../Tools/Verification/README.md#launching-sw-emu-and-hw-emu)).
Replaces configurator BLD-1's choice of verification stages. Generally only
DoomV checks the core; Sail, Spike, Whisper and QEMU are added by hand when
chasing a mismatch.

## 2026-10-02

**Several harts, DoomV first.** Ouroboros supports more than one hart; the
hart count is the user's to choose like everything else (configurator CORE-17).
The reference had to come first: DoomV now runs `-harts=N` -- harts in a fixed
round-robin, per-hart CLINT `msip`/`mtimecmp` and IMSIC files, Linux SMP
booting deterministically -- and is held to a multi-hart build of Sail
(`sail_riscv_mh`, in DoomV's `tools/verification/simulators/sail/multihart`)
in strict lock-step. Overrides the README's and AGENTS.md's "one hart". What
lock-step needs for harts that run truly in parallel is in
[lockstep.md](lockstep.md) ("Several harts").

## 2026-10-01

**Everything is adjustable, if the user wishes.** Every recommendation the
configurator makes -- from the `.gguf`, the target, or anything else -- is a
starting point the user can change, never a lock. Fixed project rules (all HLS,
own IP, strict lock-step) are rules, not settings; everything a build can vary
is the user's to vary.

**The KV cache: recommended for the `.gguf`, configurable like the PEs.** The
recommendation derives its K/V types, size, context and attention hardware from
the supplied model; the user can set the K and V types separately (all nine
llama.cpp cache types), the context and DDR share, dedicated attention
hardware for other KV types from the LUT budget, and placement where the
target allows ([gguf-datatypes.md](gguf-datatypes.md)).

**Recommended: just the supplied `.gguf`. Allowed: LUTs for other datatypes.**
The recommended configuration is optimised for the supplied `.gguf` alone. The
user may allocate any share of the LUT budget to PEs of other datatypes, to
run other models on the same bitstream; the configurator shows the split and
what it costs the supplied model ([gguf-datatypes.md](gguf-datatypes.md)).

**The model's datatypes are optimised for the `.gguf`.** Beyond supporting
every type, the accelerator's datapath is shaped around the chosen model's
tensor-type mix: multiplier widths and DSP packing for its weight widths,
multiplier-free PEs for ternary models, FP4 PEs for FP4 models, a
floating-point datapath for F16/BF16 models, and unpacker throughput in
proportion to each type's share of the bytes -- always bit-exact with ggml
([gguf-datatypes.md](gguf-datatypes.md)).

**Every datatype a `.gguf` can hold is supported, in hardware.** All 35 live
`ggml_type`s, including the IQ lattice-codebook, ternary and FP4 types, with
none left to a CPU fallback -- superseding the configurator report's proposal
to warn on IQ2/IQ3 and fall back. By default a build includes the unpackers
for the types in the chosen `.gguf`; more, or all, can be added
([gguf-datatypes.md](gguf-datatypes.md)).

**The program is called Ouroboros.** The repository README's "Hydra" is
renamed; notes written before this may still say Hydra.

**The flow, and where things go** ([ouroboros-flow.md](ouroboros-flow.md)).
Run it; a rich terminal UI opens; it finds a `.gguf` in `gguf/` or asks for
one; it asks for the target; it walks through the configuration with a
recommendation from the `.gguf`; it generates the files, synthesises in Vitis
then Vivado and writes the bitstream, with live progress bars. Every build
goes to `build/<FPGA-name>-<date-time>/`, with folders for the Vitis files,
the Vivado files, the XDCs and the rest, and the bitstream in `bitstreams/`.

**Targets are read from Vitis, not kept in the repository.** No `FPGAs/`
folder: the program reads the families, devices, boards, memory and I/O from
the pinned Vitis installation and asks the user -- which family, which FPGA
(board or device), and so on (docs/ouroboros-flow.md). Supersedes the
`FPGAs/` folder of the flow as first written.

**Ouroboros aims to use 100% its own IP.** All logic is Ouroboros's HLS. The
silicon itself -- the PS, clock managers, I/O primitives, transceivers, hard
blocks -- is reached through the thinnest generated wrapper, with no vendor
logic around it. Vendor soft IP is allowed only as a listed stopgap with its
replacement planned (today only MIG, for DDR on boards without a PS); licensed
IP is never used. Answers the two questions `io-catalog.md` had left open.

**A variety of I/O to build around, for any FPGA.** Ouroboros supports a broad
catalogue of interface types, organised by what drives them, so the generator
finds support for most of what any board offers
([io-catalog.md](io-catalog.md)). "Any FPGA" means any AMD part the pinned
Vivado/Vitis release targets, since the toolchain is AMD's.

**Every I/O the platform shows is discovered, and each can be enabled or
disabled.** Not only the display: USB, Ethernet, SD, QSPI, the cameras (MIPI
CSI-2, the AP1302 ISP, USB cameras), Pmod and GPIO. The generator produces
each enabled interface's path from what drives it; a disabled interface
generates nothing, and its resources go to the systolic array's budget
([platform-generator.md](platform-generator.md), "Every interface is a
switch").

**The generator discovers the display and drives it.** It must see from the
platform specification that the KV260 has an HDMI output, generate the code
for it, and the system outputs through it. Since the KV260's HDMI is driven
by the PS DisplayPort controller, the generated path is: PS configuration for
that controller's fabric video input, the uncore's HLS display engine
feeding it, and the controller's set-up run on the RISC-V
([platform-generator.md](platform-generator.md)).

**Sail and DoomV use the Vitis simulation clock.** In lock-step, the
references take their clock from the simulation the core runs in: each
retirement carries the core's cycle count, and DoomV and Sail derive `mtime`
and `mcycle` from it instead of from their own instruction-counted clock (one
tick per two instructions). Time, counter reads and timer interrupts are then
compared strictly, like everything else. Refines the entry below, which had
made clock-decided values the core's to report. For Sail this changes how its
C emulator is driven, not the model; it is an exception, for Ouroboros, to
DoomV's rule of running Sail with its configuration unmodified.

**The core's clock is the FPGA's clock, in every mode.** Vitis software
emulation, hardware emulation and the physical FPGA all run the core on the
same clock definition: `mcycle` counts the core's clock cycles, `mtime` is
derived from that clock, and the timebase the device tree advertises comes
from the platform's clock frequency. Supersedes the proposal in
`lockstep.md` to give verification builds Sail's instruction-counted clock.
Consequence: lock-step against DoomV is strict on everything except values
the clock decides (counter and time reads, pending timer interrupts and where
they are taken), which come from the core's record -- DoomV's lenient mode.

## 2026-09-30

**riscv-formal's SystemVerilog wrapper is allowed, for testing only.** It checks the core's retirement port and is never part of the hardware; the all-HLS rule covers everything that is.

**Stock KV260 carrier board, no custom carrier.** Overrides the feasibility
report's recommendation of a custom K26 carrier as the long-term route for
peripherals and the laptop build ("Off-the-shelf parts make a $460-650
laptop"; phase 7). Peripherals come from the stock board.

**No board-specific code written by hand; it is generated from the platform
specification Vivado and Vitis provide.** Overrides the report's board layer of
hand-written files in `FPGA-Hardware/boards/kv260/` ("The PS is unavoidable
plumbing, so confine it to one folder"; phase 2). The board contract the report
describes -- memory-request streams, an MMIO window, interrupt lines, clock and
reset, a trace stream -- still stands; what changes is that everything behind it
(block design, constraints, memory map and DDR ports, PS configuration, boot
image and its FSBL, device tree, resource budgets) is produced by a generator
from the target's board files, exported hardware platform and part data. A new
target is a new platform given to the generator.

**Lock-step with DoomV is the verification target.** The core runs in
lock-step with DoomV, which matches Sail.

**All hardware is C++ for Vitis HLS; Vivado produces the RTL.** No hand-written
HDL. The report's HLS risks are to be met with HLS coding patterns.

**No code yet.** Research and repository setup only, until the owner says
otherwise.

**The KV260 is the first target, and everything must be as portable as
possible.**
