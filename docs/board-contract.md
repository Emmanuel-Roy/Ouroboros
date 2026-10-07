# The board contract

Status: **draft for review** (Phase 0).

The core and the accelerator never see a board. They see this contract, and
everything on the far side of it is generated from the target's platform
specification ([platform-generator.md](platform-generator.md)). Nothing on
either side is written for the KV260.

## Why a contract

The KV260 is not a neutral FPGA board: its processing system owns the DDR,
the fabric clocks, boot and every PC-style peripheral (feasibility report,
"The PS is unavoidable plumbing"). Other boards differ the other way -- a
Genesys 2 has DDR on the fabric and no processing system at all. If the core
knew about any of that, each board would be a fork. With the contract, the
core is the same C++ on every target, and only generated files differ.

## What the core and accelerator see

| signal | direction | shape | notes |
|---|---|---|---|
| **memory ports** | out: request, in: response | `hls::stream` pairs (AXI4-Stream after synthesis) | one or more; each serves a physical address window. Requests carry address, size, write data and mask; responses carry data or an error. Variable latency is expected -- the core stalls on a missing response. |
| **MMIO window** | out | the same request/response shape | a physical address range for devices; uncached, strictly ordered |
| **interrupt lines** | in | level, one per source | external interrupts into the interrupt controller; the timer is inside the contract's own uncore |
| **clock and reset** | in | one clock per domain | the core's domain and the accelerator's may differ; the board side provides and crosses them |
| **video stream** | out | pixels with video timing, from the uncore's display engine | the board side carries it to the display output the generator found (on the KV260, the PS DisplayPort controller's live-video input, which feeds the HDMI connector) |
| **trace stream** | out | stream of retirement records | for lock-step: full records in simulation, hashes and the value log on the board ([lockstep.md](lockstep.md)) |
| **enabled interfaces** | build-time input | the configurator's choice | which of the platform's interfaces exist in this build; a disabled one generates nothing and returns its resources to the accelerator's budget |
| **platform description** | build-time input | generated header | memory windows, port count and width, clock frequencies, interrupt numbering -- template parameters, never constants in the core |

Ports are **streams** because:

- the core's pipeline is one II=1 loop, and an `m_axi` access inside it
  serialises on worst-case latency; a request/response stream gives variable
  latency through valid/ready;
- Vitis co-simulation of a free-running (`ap_ctrl_none`) design supports only
  combinational designs, II=1 pipelines, or stream ports;
- a stream is what any board's memory system can be adapted to.

## What the board side provides, generated

Behind the contract, per target, all generated from the platform
specification:

- **adapters** from the memory-port streams to the board's memory system -- on
  the KV260, the PS's HP ports (several in parallel; the accelerator needs at
  least four for near-roofline decode), hiding the split DDR map (two 2 GB
  windows) behind one contiguous physical range or describing it in the device
  tree;
- **clocks**: on the KV260, from the PS's `pl_clk` outputs -- the board has no
  fabric clock source of its own;
- **the platform's devices** that are the board's to give: on the KV260 the PS
  USB, SD and DisplayPort controllers, reached through the MMIO window and the
  PS's low-power AXI port; elsewhere, whatever the board has;
- **the boot image** and whatever the board's hard logic must do before the
  RISC-V starts (on the KV260, a generated FSBL on the R5 that sets clocks and
  DDR, loads the bitstream and parks);
- **the device tree** and the firmware platform the RISC-V boots with;
- **the resource budget**: LUTs, FFs, DSPs, BRAM, URAM and memory bandwidth,
  which Ouroboros and the core's configuration read.

## What lives inside the contract but is not the core

An "uncore" common to every board, in HLS like everything else: the timer
(ACLINT `mtime`/`mtimecmp`, counting from the core's own clock -- the FPGA's
clock in every mode, see [lockstep.md](lockstep.md)), the interrupt controller (AIA: an APLIC and
IMSIC files, as DoomV models them; decisions, 2026-10-07), a UART for the console, and the bridge that splits the
core's traffic between memory ports and the MMIO window, and the display engine
that scans a framebuffer out of memory onto the video stream
([platform-generator.md](platform-generator.md), "Interfaces the generator
discovers"). These are part of the
machine Linux sees, so they are the same on every board.

## Open

- The interrupt controller is AIA (decided 2026-10-07: only what DoomV
  models). Open is how much of DoomV's platform to take as it is: a pair of
  IMSIC files per hart at `0x24000000`/`0x28000000 + hart * 0x1000`, the
  APLIC, and a CLINT `msip`/`mtimecmp` per hart.
- Whether PS peripheral interrupts on the KV260 can reach the fabric
  (unverified); polling is the fallback.
- Cache coherence: none needed for one hart and non-coherent HP ports, as long
  as DMA from the accelerator is ordered with the core's caches by `cbo`
  instructions (Zicbom). With several harts the harts' caches must be
  coherent with each other (or shared), and LR/SC reservations must be
  broken by another hart's store to the same 8-byte set, as in DoomV and
  Sail.
