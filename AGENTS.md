# Working on Ouroboros

Rules for anyone -- person or agent (Claude Code, Codex) -- changing this repo.
The README says what Ouroboros is; this says how to work on it.

## Phase

**Research and setup. No hardware code yet.** Research notes, reports, designs and
repository structure, until the owner says otherwise -- plus the build, test
and report automation in `scripts/`, which the owner asked for (2026-10-01).

## Targets

- **The stock Kria KV260 (its own carrier board) is the first target, and the
  design must stay as portable as possible.** No custom carrier board.
- **No board-specific code is written by hand.** Everything that depends on
  the target -- pins, clocks, memory map, DDR ports, PS configuration, boot
  image, device tree, resource budgets -- is *generated* from the platform
  specification Vivado and Vitis provide for that target (the board files,
  the exported hardware platform, the part's data). Supporting a board means
  pointing the generator at its platform, not writing files for it.
  `FPGA-Hardware/boards/<board>/` holds generated output, never hand edits.
  The core, the accelerator and the tools take the platform as input.
- **No software on the FPGA's hard CPU cores.** The RISC-V core is the only
  processor the system uses. Where a vendor platform cannot come up without
  its hard cores doing something (memory controller bring-up, configuration),
  that is generated from the platform too, kept as small as it can be, and
  ends before the RISC-V starts.
- **RVA23S64 harts, one or more.** The pipeline is a configuration choice
  (configurator CORE-19): in-order with stalls for hazards first, then
  scoreboarding, Tomasulo and explicit renaming, all committing in order. V
  with VLEN=128 and a configurable datapath width. The hart count is a
  configuration choice (docs/configurator.md CORE-17); DoomV `-harts=N` is the
  lock-step reference for any count.
- **All hardware is C++ HLS.** The core, the accelerator and everything else
  in the FPGA are written in C++ for Vitis HLS, and the RTL is what Vitis and
  Vivado generate from it. No hand-written Verilog/VHDL/SystemVerilog, and no
  other HDL. Where HLS makes something hard (pipeline control, variable-latency
  memory), the answer is an HLS coding pattern, not a drop to RTL.
  Test benches are not hardware: riscv-formal's SystemVerilog wrapper is
  allowed for testing, and only for testing.
- **100% Ouroboros's own IP.** All logic in the FPGA is Ouroboros's HLS.
  Silicon primitives (the PS, clock managers, I/O SERDES and DDR registers,
  transceivers, hard blocks) are reached through the thinnest generated
  wrapper, with no vendor logic around them. Vendor soft IP only as a listed
  stopgap with its replacement planned; licensed IP never
  ([docs/io-catalog.md](docs/io-catalog.md)).

## Configuration

- **Everything is adjustable, if the user wishes.** The configurator always
  offers a recommendation -- optimised for the supplied `.gguf` and target --
  and every value in it can be changed. Design nothing as a fixed choice that
  a build could vary; the project rules above are the only things not offered
  as settings.

## Verification

- **Lock-step with DoomV is the target.** The core must run in lock-step with
  DoomV (`Tools/Verification/DoomV`), instruction by instruction; DoomV in turn
  matches Sail, the reference of record. Where the specification
  leaves a choice open and Sail makes one, the core makes the same one.
- **Deterministic.** Same inputs, same trace, every run, in simulation and on
  the board.
- Suites and how to get them: [`Tools/Verification/README.md`](Tools/Verification/README.md).

## Where things go

| what | where |
|---|---|
| research notes (raw, sourced) | `agentic/research_notes/<topic>/` |
| research reports (synthesised) | `agentic/reports/` |
| every bug found, and how it was resolved | `agentic/bugs/` |
| designs and decisions | `docs/` |
| every build's outputs, one folder per build | `build/<FPGA-name>-<date-time>/` |
| generated per-board output (never hand-edited) | `FPGA-Hardware/boards/<board>/` |
| owner decisions | `docs/decisions.md` |
| laptop physical hardware | `Hardware/` |
| software (configurator, generator) | `Source/` |
| third-party tools and references (llama.cpp, the verification suites) | `Tools/` |
| measured runs (written by `scripts/pipeline.py`) | `Performance/` |
| build, test and report automation | `scripts/` |
| models for the accelerator | `gguf/` |

## Submodules

Pinned, never vendored. `git submodule update --init` gets them; DoomV's own
submodules (Sail, Linux, ...) are fetched only as needed -- see
`Tools/Verification/README.md`. Bump a pin in its own commit, saying why.
