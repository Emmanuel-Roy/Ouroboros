Research and designs will go here.

## Designs (Phase 0, drafts for review)

| document | decides |
|---|---|
| [ouroboros-flow.md](ouroboros-flow.md) | the program's flow, from picking a model and a target to a bitstream, and where its outputs go |
| [configurator.md](configurator.md) | every option the program offers: the always-asked questions, the three tiers, recommendations from the `.gguf` and target, dependencies |
| [decisions.md](decisions.md) | the owner's decisions, and where they override the research |
| [board-contract.md](board-contract.md) | the only things the core and accelerator see of a board: memory-port streams, an MMIO window, interrupts, clocks, a trace stream |
| [platform-generator.md](platform-generator.md) | how everything board-specific is generated from the Vivado/Vitis platform specification, and what it produces |
| [lockstep.md](lockstep.md) | how the core is held to DoomV: Sail's trace format, strict and lenient modes, three levels from C simulation to the board |
| [simulation-harness.md](simulation-harness.md) | how to run the core in SW-Emu or HW-Emu against DoomV, and which other simulators to add |
| [io-catalog.md](io-catalog.md) | every I/O class a board may have, how each can be implemented (PS peripheral, fabric hard block, vendor IP, HLS), what Linux binds to, and in what order to support them |
| [gguf-datatypes.md](gguf-datatypes.md) | all 35 GGUF datatypes, exact block layouts, and how the accelerator supports every one |
| [hls-coding-standard.md](hls-coding-standard.md) | how a CPU is written in HLS: the hand-scheduled II=1 pipeline, stalling state machines, many small tops joined by streams |

The research behind them is in [`agentic/reports/`](../agentic/reports/).
