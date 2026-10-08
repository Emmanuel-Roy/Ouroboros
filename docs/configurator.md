# The configurator

Status: **draft for review** (Phase 0). The options Ouroboros offers in step 4
of its flow ([ouroboros-flow.md](ouroboros-flow.md)), taken from the research
report [`agentic/reports/Ouroboros configurator options.md`](../agentic/reports/Ouroboros%20configurator%20options.md)
and amended by every decision since ([decisions.md](decisions.md)).

## Principles

- **Recommended for the `.gguf` and the target.** Nearly every default is
  computed from the model's header and the target's platform specification, in
  a fixed order: platform and I/O costs first, then the core, then the decode
  floor, then prefill from what is left, then context and KV cache.
- **Everything adjustable, if the user wishes.** Every lever below can be
  changed; only the fixed project rules (top table) are not offered as
  settings. An override is checked by the constraint model, and an impossible
  combination says why.
- **Three tiers, one configuration.** **A** = asked every time. **E** =
  expanded view: every lever with its trade-off in a line, grouped by
  subsystem. **F** = full view, searchable. They are three views of one flat
  configuration, not nested menus.
- **Only what DoomV supports.** An option that changes the architecture --
  anything software can see: the ISA, the harts, VLEN, the interrupt
  controller, the CSRs and their widths -- is offered only where DoomV can be
  configured to the same machine, because the core is held to DoomV in
  strict lock-step (decisions, 2026-10-07). Microarchitecture that software
  cannot see -- pipeline, branch prediction, caches, TLBs, the datapath
  widths -- is free. Supporting a new architectural option means adding it to
  DoomV first.
- **Estimates before building.** Every change re-runs the estimates --
  resources with headroom, decode and prefill tokens/s, memory left for Linux,
  build time. Estimates are replaced by measured costs as the pipeline produces
  them.

## Always-asked questions

Asked in this order. Each opens with the recommendation selected; Enter
accepts it.

| # | Question | Choices | Recommended | Sets |
|---|---|---|---|---|
| 0a | Which model? | `.gguf` files in `gguf/`, or a path | the only file, else ask | every model-derived lever |
| 0b | Which family? | the families Vitis has installed | the attached board's family, else the default target's | the device and board lists |
| 0c | Which FPGA? | the family's boards (each fixes the part) and devices (then package and speed grade) | the attached, identified board; else the default target | every platform-derived lever: resources, memory, I/O |
| 1 | Which I/O do you want? | every interface the target shows, each on or off, with its LUT, memory-port and bandwidth cost | a laptop set: display, USB, SD, Ethernet on; cameras, Pmod, GPIO off | IO-* |
| 2 | Is RVA23 good, or do you want less? | RVA23S64 / RVA22S64 / RV64GC (RVA20), with which distributions each runs | RVA23S64 | ISA-1, SW-2 |
| 3 | Wide vector unit or not? | 32 / 64 / 128-bit datapath (VLEN 128) | the widest that leaves the decode floor plus 15% LUT headroom; KV260: 32 | CORE-1 |
| 4 | Faster CPU or faster systolic array? | Linux-first / Balanced / LLM-first | Balanced; LLM-first for models of 1B parameters and up when LUTs are tight | CORE-3..10, ACC-11 |
| 5 | Big KV cache or not? | context length and the K and V types | from the model: its context limit capped by memory and speed; K/V types from its precision (KV-*) | KV-1..3 |
| 6 | Decode or prefill? | Chat / Balanced / Long prompts, or 0-100 | Chat; Balanced when DSPs are spare beyond the decode floor | ACC-7, ACC-9, ACC-10 |
| 7 | Lots of PE units or not? | decode floor only / moderate / fill, and any share for other datatypes | moderate, all for the model's own types; "past this point PEs do not raise decode" shown | ACC-8, ACC-17, PLAT-4 |
| 8 | Rebuild OpenSBI/Linux for this configuration, or have you already? | build for me / reuse the last build / my images | build; reuse preselected when only device-tree-level changes since the last build | SW-1 |
| 9 | Test the CPU with DoomV? -- asked after the CPU is generated, before the bitstream (flow step 8) | SW-Emu / HW-Emu / both / skip | both | BLD-1 |

## Fixed by project rules

Shown read-only; not settings.

| ID | Item | Value |
|---|---|---|
| FX-1 | Interrupt controller | AIA: APLIC and IMSIC, as DoomV models them (no PLIC) |
| FX-2 | Architectural options | only those DoomV can be configured to (see above) |
| FX-3 | Hardware language | C++ for Vitis HLS only (riscv-formal's wrapper for testing only) |
| FX-4 | IP | Ouroboros's own; vendor soft IP only as a listed stopgap; licensed IP never |
| FX-5 | Tools | the pinned Vivado/Vitis release (2026.1) |
| FX-6 | Hard ARM cores | a generated FSBL only, then parked |
| FX-7 | Core clock | the FPGA's clock in every mode; DoomV and Sail driven by the core's cycle stamps |
| FX-8 | Always generated | fan PWM, clocks and reset, the uncore (timer, interrupt controller, UART, bridge) |
| FX-9 | Datatypes | every GGUF datatype can be built, in hardware; none falls back to the CPU |
| FX-10 | Accuracy | every datapath bit-exact with ggml; strict lock-step with DoomV |

## Option catalogue

### Inputs and platform

| ID | Lever | Tier | Options | Recommended | Depends on |
|---|---|---|---|---|---|
| PLAT-1 | Target | A | Vitis's families, boards and devices | attached board, else default | supplies every budget |
| PLAT-2 | Model | A | a `.gguf` | the only one, else ask | header only |
| PLAT-3 | Start from a preset | E | the golden configurations for this target | "Recommended for this model" | presets are the always-tested set |
| PLAT-4 | Utilization ceiling | E (set by Q7) | 60-90% per resource | LUT 85% hard, warn at 80%; others 90% | caps ACC-8 |
| PLAT-5 | DDR efficiency for estimates | F | 0.5-0.95 | 0.7 until measured | every tokens/s estimate |
| PLAT-6 | Allow listed stopgap IP | F | yes / no | yes, reported | only where a stopgap exists (MIG) |

### I/O -- one row per interface the target shows

Generated from what the target's board files and presets list
([io-catalog.md](io-catalog.md)); the KV260's set as the example.

| ID | Lever | Tier | Options | Recommended | Depends on |
|---|---|---|---|---|---|
| IO-1 | Display output, per connector | A | on / off | on (first connector) | display engine + 1 memory port; KV260: the PS DisplayPort controller, HDMI through its converter |
| IO-2 | Display mode | E | the resolutions the target's pixel clocks allow | 1080p60 if supported, else highest | bandwidth |
| IO-3 | Framebuffer depth | F | 16 / 32 bpp | 32 | IO-2 |
| IO-4 | USB | A | on / off | on | PS glue; interrupts or polling (IO-14) |
| IO-5 | Ethernet | A | on / off | on | PS glue |
| IO-6 | SD | A | on / off | on | needed when booting or rooting from SD |
| IO-7 | QSPI as a Linux device | E | on / off | off | QSPI holds the boot image regardless |
| IO-8 | Cameras (MIPI CSI-2), per connector | A | on / off | off | LUTs + 1 memory port each |
| IO-9 | On-board ISP set-up | E | on / off | on iff its camera path is on | IO-8 |
| IO-10 | Pmod | A | off / GPIO / UART / USB-HID / SPI-SD | off | fabric pins |
| IO-11 | GPIO, LEDs, I2S, other fabric interfaces | E | on / off each | off | MMIO window |
| IO-12 | Fan control | E | constant / temperature-controlled | temperature-controlled if IO-13 is on | FX-8 |
| IO-13 | Temperature and power monitors | E | on / off | on | I2C |
| IO-14 | PS peripheral interrupts | F | interrupts / polling | interrupts if Phase 2 verifies them, else polling | interrupt controller |
| IO-15 | Console UART route | F | the board's USB-UART / Pmod | the board's | uncore UART |

**Memory ports are a budget too.** The KV260 has six PS-PL ports into DDR: a
four-port accelerator, the core and the display take them all, so enabling a
camera takes a port from the accelerator, shares one, or is refused -- the
configurator says which.

### ISA

| ID | Lever | Tier | Options | Recommended | Depends on |
|---|---|---|---|---|---|
| ISA-1 | Profile | A | RVA23S64 / RVA22S64 / RV64GC | RVA23S64 | RVA23 brings V, H and the rest; the distribution (SW-2); DoomV and Sail configured to match |
| ISA-3 | Zicfilp, Zicfiss | E | on / off | off | Zicfiss needs A and Zimop |
| ISA-5 | Zfh, Zvfh | E | on / off each | off on the KV260; on with headroom | Zvfh needs V and Zvfhmin |
| ISA-6 | Zvfbfmin, Zvfbfwma (bf16) | E | on / off each | off on the KV260 | V; Zvfbfwma needs Zvfbfmin |
| ISA-8 | Zbc, Zvbc | F | on / off each | off | Zvbc needs V |
| ISA-9 | Vector crypto: Zvkned (AES), Zvknha/Zvknhb (SHA-2), Zvksed (SM4), Zvksh (SM3), Zvkg (GHASH) | F | each on / off; or the umbrellas Zvkn, Zvknc, Zvkng, Zvks, Zvksc, Zvksg | off: a large LUT cost | V; Zvknhb includes Zvknha |
| ISA-10 | Zkr | F | on / off | off | an on-chip entropy source |
| ISA-11 | Sv48, Sv57 | F | on / off each | off on the KV260 (Sv39 covers its memory) | Sv57 needs Sv48; with H, the same for Sv48x4 / Sv57x4 |
| ISA-12 | Svadu, Sspm | F | on / off each | Svadu off (Svade, as RVA23 requires); Sspm on with RVA23 | Svadu: the walker writes A/D |
| ISA-13 | Emulate in M-mode where allowed | F | per item | hardware | a custom OpenSBI |
| ISA-14 | Below RVA23: the extensions DoomV can switch off one by one (H, V, Zfa, Zicbo*, Zawrs, Zimop/Zcmop, Svinval, Svnapot, Svpbmt, Sscofpmf, Ssstateen, Zba/Zbb/Zbs, Zicond, Zvfhmin, Zvbb/Zvkb, the hints) | F | each on / off | as the profile (ISA-1) | ISA-1 |

Every one is a DoomV `-march` switch, held to Sail on and off in DoomV's
gate (`tools/verification/ext_switches.py`), and Sail is configured to
match. What the chosen profile requires is on and not offered (RVA23S64
requires Zvfhmin and Zvbb, for instance); what it leaves optional is.

Removed, because DoomV does not implement them: Zacas, Zabha, Ziccamoc,
Zama16b, Sdtrig, Ssstrict, Svvptc, and a PLIC.

### Core microarchitecture

| ID | Lever | Tier | Options | Recommended | Depends on |
|---|---|---|---|---|---|
| CORE-1 | Vector datapath width | A | 32 / 64 / 128 | see Q3 | V |
| CORE-2 | Core clock | E | the target's clock choices | the highest the estimator closes timing at | FX-7; device-tree timebase |
| CORE-3 | Scalar FPU | E | pipelined FMA / shared with vector / iterative | shared on the KV260 | V needs D |
| CORE-4 | FP divide and square root | F | radix-2 / radix-4 | radix-2 | -- |
| CORE-5 | Multiplier, divider | E | single-cycle DSP / pipelined / iterative | pipelined; radix-2 | DSPs |
| CORE-6 | Vector slow paths (div/sqrt, permutes, indexed and segment, reductions, widening) | F | fast / serial, per class | serial | LUTs |
| CORE-7 | Branch direction predictor | E | none (fetch stalls until the branch resolves) / static not-taken / static backward-taken-forward-not-taken / 1-bit history table / 2-bit saturating counters (bimodal) / two-level local history / gshare (global history XOR pc) / tournament (bimodal and gshare with a chooser) / TAGE | 2-bit bimodal | the dynamic ones need a BTB (CORE-7a) |
| CORE-7a | Branch targets | E | none / BTB; with a return-address stack; with an indirect-target predictor | BTB + RAS | -- |
| CORE-7b | Predictor sizes | F | table entries (64-16K), history length, BTB entries and ways, RAS depth, TAGE tables | 2-bit: 128 entries; BTB 32; RAS 2 | BRAM/LUTs |
| CORE-8 | L1 caches | E | 4-64 KiB each; ways, line, write policy in F | 16 KiB / 16 KiB | BRAM |
| CORE-9 | L2 | E | none / 64-512 KiB in URAM | none on the KV260 | URAM shared with the accelerator |
| CORE-10 | TLBs | F | 4-64 entries each; an L2 TLB; G-stage TLB | 16 / 16 | H: two-stage walk |
| CORE-11 | PMP entries | -- | 16, as DoomV and Sail's configuration have | -- | an option once DoomV can vary it |
| CORE-12 | Performance counters | F | 0-29 | 4 | Sscofpmf |
| CORE-13 | Pipeline depth | F | 3-7 (in-order); the front end and commit for the others | from the clock target | Fmax against stalls |
| CORE-19 | Execution model | E | in-order, stalling on hazards / scoreboarding (in-order issue, out-of-order execution and completion, CDC 6600 style) / Tomasulo (reservation stations, renaming by tag, a common data bus) / explicit renaming (a physical register file and a free list, MIPS R10000 style) | in-order on the KV260 | every one commits in program order through a reorder buffer: see "Out of order, in lock-step" below |
| CORE-20 | Issue width | E | 1 / 2 / 4 | 1 | above 1: register-file ports, wakeup and bypass grow with it |
| CORE-21 | Out-of-order sizes | F | reorder buffer 8-128; reservation stations per unit 2-16; common data buses 1-4; physical registers 48-256; load and store queues 4-64 | ROB 16, 4 per unit, 1 bus | CORE-19 |
| CORE-22 | Memory ordering of loads | F | wait for every older store / store-to-load forwarding / + memory-dependence prediction | forwarding | CORE-19 beyond in-order |
| CORE-23 | Functional units | F | integer ALUs 1-4, branch units, load/store ports 1-2, multipliers, FP units | 1 of each | CORE-20 |
| CORE-14 | Physical address width | -- | DoomV's | -- | an option once DoomV can vary it |
| CORE-15 | Misaligned accesses | -- | in hardware, split as Sail and DoomV split them | -- | trapping them is not something DoomV models |
| CORE-16 | Board trace hash interval | F | 2^10-2^24 | 2^16 | lockstep.md |
| CORE-18 | VLEN | E | a power of two, 128 to 65536 bits | 128 | at least the vector datapath (CORE-1); DoomV `-vlen`, Sail's `vlen_exp` |
| CORE-17 | Harts | E | 1 to as many as fit | 1 on the KV260: a second core costs the LUTs of a large share of the PEs; more where the decode floor leaves room for a whole core | each hart its own CLINT msip/mtimecmp and IMSIC files; caches coherent across harts (board-contract.md); the device tree's cpus; DoomV `-harts=N` |

### Out of order, in lock-step

The execution model (CORE-19), the predictors (CORE-7) and the widths are
microarchitecture: software cannot see them, so DoomV need not model them,
and every combination is held to the same DoomV. What makes that true:

- **Commit is in program order, always.** Whatever executes out of order,
  instructions retire in order from a reorder buffer, one record each, and
  traps are precise -- RISC-V requires both. Tomasulo is therefore offered
  only with a reorder buffer (the original, without one, has imprecise
  exceptions), and scoreboarding commits through one too. The retirement
  port, the records and the lock-step are the same for every model; only the
  cycle stamps differ.
- **Nothing speculative reaches the outside.** Loads from anything that is
  not RAM, stores, CSR writes with side effects and fences wait until they
  are the oldest instruction; a mispredicted path leaves no trace in memory,
  devices or architectural state. A device load that ran early would read a
  value DoomV's device would not have given.
- **Branch predictors only change time.** A wrong prediction costs cycles,
  never a different result, so every predictor passes the same lock-step,
  and the cycle stamps show what each one costs.

Each model is built in the coding standard's form -- one loop at II=1,
stage registers, a stall vector -- with the reservation stations, reorder
buffer and rename tables as arrays updated once per iteration and wakeup as
fully unrolled tag compares ([hls-coding-standard.md](hls-coding-standard.md)).
No published HLS core is out of order, so these are a design to prove; the
in-order core comes first, and the others are offered once each passes the
lock-step.

### Accelerator

| ID | Lever | Tier | Options | Recommended | Depends on |
|---|---|---|---|---|---|
| ACC-1 | Weight datatypes and their PEs | E | the model's types / plus others / all 35 | exactly the types in the `.gguf`'s tensors, the datapath shaped around their byte mix ([gguf-datatypes.md](gguf-datatypes.md)) | each type needs its family's unpacker |
| ACC-2 | Activation precision | E | INT8 / FP16 / the model's float format | INT8 for quantised models (llama.cpp's Q8 activations); the model's own format for F16/BF16/F32 models | multiplier widths |
| ACC-4 | Decode lanes | F | 16-256 | `ceil(eta * BW / (bits_per_weight * f))`, to a power of two | beyond it decode does not improve |
| ACC-5 | Decode MACs in DSPs or LUTs | F | DSP-packed / LUT | DSP-packed for the model's widths; LUT when LUT headroom is large | prefill always DSP |
| ACC-6 | Memory ports | F | 1 to the target's maximum | 4, +2 when attention is offloaded, capped by the free ports | I/O and the core take ports first |
| ACC-7 | Engine topology | E | GEMV only / GEMV + array / reconfigurable / swapped by partial reconfiguration | from Q6 | partial reconfiguration unproven here |
| ACC-8 | Prefill array size and shape | A (Q7) / F (rows x cols) | 0 to the DSPs left | fill to the ceiling Q7 sets | DSP and LUT budget |
| ACC-9 | Prefill emphasis | A (Q6) | 0-100 | 15 | ACC-7, ACC-10 |
| ACC-10 | Token tile | F | 8-512 | `next_pow2(max(T_min, p * T_max))` | activation buffer; llama.cpp ubatch |
| ACC-11 | Offload level | E | MUL_MAT only / + norm, RoPE, SiLU / + attention and KV append / fused layers / + output head and sampling | fused layers; + sampling when it costs over 20% of a token | rises as the CPU tier falls |
| ACC-12 | Attention engines | F | 1 to the KV head count | 1 decode-first; 2 for prefill emphasis 50+ | grouped-query ratio |
| ACC-13 | Accumulator format | F | 24-bit block + FP32 / fixed 32 | FP32 across blocks | `feed_forward_length` |
| ACC-14 | On-chip buffers | F | URAM/BRAM split | URAM for activations and accumulators; at least 20% left for the core's caches | CORE-8, CORE-9 |
| ACC-15 | Accelerator clock | E | 150-300 MHz | 250 | its own clock domain |
| ACC-16 | Mixture-of-experts router | F | on / off | on iff `expert_count` > 0 | the experts' types |
| ACC-17 | LUTs for PEs of other datatypes | E | a share of the accelerator budget per added datatype | none: all to the supplied model | costs the model prefill speed; shown |

### KV cache and memory

Recommended from the supplied `.gguf`, adjustable like the PEs
([gguf-datatypes.md](gguf-datatypes.md)).

| ID | Lever | Tier | Options | Recommended | Depends on |
|---|---|---|---|---|---|
| KV-1 | Context length | A (Q5) | 256-token steps up to the model's `context_length` | min(the model's limit, what fits in DDR, a speed cap) | KV bytes per token from the model's attention shape |
| KV-2 | K type | A (Q5, sub-choice) | F32, F16, BF16, Q8_0, Q4_0, Q4_1, IQ4_NL, Q5_0, Q5_1 | the model's float format for a float model; Q8_0 for a quantised one | the attention hardware for that type |
| KV-3 | V type | A (Q5, sub-choice) | the same nine | as K | a quantised V needs the flash-style path (KV-4) |
| KV-4 | Flash-style attention path | F | on / off | on when attention is offloaded or V is quantised | KV-3 |
| KV-5 | Attention hardware for other KV types | E | a LUT share per added type | none | like ACC-17 |
| KV-6 | Cache placement | F | DDR / on-chip for the newest tokens, where the target has room | DDR (the KV260's URAM holds only tens of tokens) | URAM |
| KV-7 | Full sliding-window cache | F | on / off | off | sliding-window models only |
| MEM-1 | OS memory reserve | E | 0.5-3 GiB | 1.25 GiB until measured | DDR fit |
| MEM-2 | Accelerator memory carve-out | F | reserved-memory / CMA | reserved-memory for weights, CMA for shared buffers | contiguous weights |
| MEM-3 | Split DDR windows | F | remap to one range / both in the device tree | both in the device tree | the board contract |

### Software, boot and deployment

| ID | Lever | Tier | Options | Recommended | Depends on |
|---|---|---|---|---|---|
| SW-1 | OpenSBI/Linux | A (Q8) | build / reuse the last build / my images | build; reuse when the software-relevant settings are unchanged | user images are validated |
| SW-2 | Distribution | E | Ubuntu 26.04 / 24.04 / Debian 13 / Fedora / Buildroot | the newest the ISA allows | Ubuntu 25.10+ needs RVA23S64; no FPU needs Buildroot |
| SW-3 | Boot chain | E | OpenSBI, U-Boot, extlinux / OpenSBI straight to Linux | U-Boot (kernel updates work) | FW_DYNAMIC preferred |
| SW-4 | Kernel options | F | per symbol | derived from the ISA | checked after `olddefconfig` |
| SW-5 | Kernel command line | F | console, carve-outs | derived | IO-15, MEM-2 |
| SW-6 | llama.cpp defaults | F | context, cache types, ubatch | KV-1, KV-2, KV-3, ACC-10 | the ggml backend |
| BOOT-1 | Boot medium | E | SD / QSPI / JTAG (development) | SD, with QSPI holding the boot image | QSPI writes need a typed confirmation |
| BOOT-2 | Flash after the build | E | no / yes | no | BOOT-1 |

### Build and verification

| ID | Lever | Tier | Options | Recommended | Depends on |
|---|---|---|---|---|---|
| BLD-1 | Test the CPU with DoomV before the bitstream | A | SW-Emu / HW-Emu / both / skip, asked at flow step 8 (decisions, 2026-10-07) | both | Tools/Verification/README.md; a saved configuration remembers the answer |
| BLD-2 | Implementation strategy | E | default / Performance_Explore / sweep | default, sweeping on failure | names checked against 2026.1 |
| BLD-3 | Fallback policy | E | automatic (non-functional levers only, reported) / ask / never | automatic | order: strategy, PEs, clock, caches and KV, then ask; never the ISA, datatypes, I/O or distribution |
| BLD-4 | Parallel jobs | F | 1 to the host's cores | half the cores | pipeline `--jobs` |
| BLD-5 | Stage reuse | F | on / off | on, keyed by the configuration that stage depends on | -- |
| BLD-6 | Placement exploration | F | directive list | none | Vivado has no true seed |
| BLD-7 | Model accuracy check | E | off / logits against llama.cpp on fixed prompts | on when datatypes or KV types differ from the recommendation | an `llm-accuracy` stage |

## Keeping every combination buildable

From the report: one constraint model (CP-SAT) holding every rule above, each
with a message, so an impossible choice is explained; the same model emits the
HLS parameters, device tree, kernel and OpenSBI configuration, `-march`, and
DoomV/Sail configuration; a validation ladder before any long build; 5-10
golden configurations (the presets) always built, cheap samples on every
commit, pairwise samples nightly; stage reuse keyed by configuration; and
fallbacks that never change what the user asked for silently.

## Open

- The constraint-model implementation and its rule sources.
- Measured costs to replace every estimate (Phase 2 on).
