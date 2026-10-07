# Ouroboros: from a model to a bitstream

Status: **draft for review** (Phase 0). The owner's flow (decisions,
2026-10-01); the options asked in step 4 are the subject of
[`agentic/reports/Ouroboros configurator options.md`](../agentic/reports/) and,
once settled, `configurator.md`.

## The flow

You run Ouroboros. A full-screen terminal UI opens -- in the style of Claude Code:
rich text, a live task list, progress bars, the current step always visible.

| step | what happens | what you see |
|---|---|---|
| **1. Model** | Ouroboros looks in `gguf/` for `.gguf` files. One: it is proposed. Several: you pick. None: it asks you to open one. It reads only the header -- architecture, dimensions, context length, the quantization type of every tensor -- not the weights. | the model's name, size, parameter count, quantization mix, context length |
| **2. Target** | Read from the Vitis installation, never from the repository. Ouroboros asks **which family** (Vitis's installed devices: the pinned 2026.1 has 59 families, 302 devices), then **which FPGA**: a board from the AMD board store (45 boards; it fixes the exact part, and its companion boards -- a Kria SOM's carrier -- come with it) or a bare device, then its package and speed grade. If the board is attached and identifiable, it is preselected. | the part and family; the **memory** (type, width, speed, size -- the KV260: 4 GB DDR4-2400, 64-bit, on the processing system); the **I/O**: processing-system peripherals the board's presets enable (USB, DisplayPort, Ethernet, SD, UART, QSPI...) and interfaces on fabric pins (cameras, GPIO, I2S...); resources |
| **3. Recommendation** | From the model and the target, Ouroboros computes a recommended configuration: core profile, vector width, accelerator size and datatypes (built around the `.gguf`'s quantization), KV cache and context, I/O, clocks. | the configuration, why each choice was made, and its estimates: resources with headroom, decode and prefill tokens/s, memory left for Linux, build time |
| **4. Configuration** | Ouroboros walks you through the choices: the questions always asked (faster CPU or faster array, RVA23 or less, wide vector unit, big KV cache, many PEs, decode or prefill, which I/O, rebuild Linux/OpenSBI or not), then, if you want them, the expanded and full selections. Every change re-runs the estimates and the compatibility checks at once; an impossible combination says why. | the recommended value and yours, side by side; estimates updating live |
| **5. Plan** | Before anything long starts: what will be built, what changed from the recommendation, the estimates, and how long the build should take. You confirm. The configuration is saved, so the same build can be re-run without the questions. | a summary to accept, edit or cancel |
| **6. Generate** | Ouroboros generates every file the build needs: the HLS sources' parameters, the platform files from the target's specification (block design, constraints, memory map, boot image description, device tree), and, if chosen, the OpenSBI/Linux configuration. | a checklist of generated files |
| **7. Generate the CPU** | Vitis HLS synthesises each component -- the core, the uncore, the accelerator -- from the generated sources: the CPU's RTL, ready to test. | a progress bar per component; resources and estimated clock as each finishes |
| **8. Test the CPU with DoomV** | Ouroboros asks: **test the CPU with DoomV?** -- in **SW-Emu** (Vitis software emulation: the core's C++, fast), **HW-Emu** (Vitis hardware emulation: the RTL from step 7 in XSim, slow), **both** (recommended), or **skip**. The chosen emulations then run on their own: every test of the verification suites on the core, each record it retires checked against DoomV in strict lock-step ([verification](../Tools/Verification/README.md#launching-sw-emu-and-hw-emu)). All match: the flow goes on to step 9. A mismatch stops it before the bitstream: the test, the instruction, the field, and a DoomV snapshot from just before it; Ouroboros then asks whether to stop there or build the bitstream anyway, marked unverified. | tests done of tests total per emulation and suite, records checked, the first mismatch as soon as there is one |
| **9. Implement and generate the bitstream** | Vivado builds the block design, synthesises, places, routes, and writes the bitstream; then the boot image. | a live task list with a progress bar per stage, elapsed and remaining time, the current phase of each tool, warnings as they appear; a failure says which stage, why, and which choice most likely caused it |
| **10. Result** | The bitstream and boot image, and a report: resources used against the estimate, achieved clock, timing, and the DoomV results -- which emulations ran, tests and records matched, or that the test was skipped. | where everything is, and the next command to run |

## Where things go

### Targets: from Vitis, not from the repository

There is no folder of targets. Everything about a target is read from the
pinned Vitis installation each time (decisions, 2026-10-01):

| question | read from |
|---|---|
| family | `Vitis/data/installed.devices` |
| device, package | the same, and Vitis's part data (`Vitis/data/parts`) |
| board | the AMD board store shipped with the tools: `board.xml` (part, fabric interfaces, connectors) and the processing-system presets |
| memory | the board's processing-system preset (DDR type, width, speed, device size) or its DDR interfaces on fabric pins |
| I/O | the presets' enabled peripherals, and the board's `<interface>` entries |
| Vitis platform | `Vitis/base_platforms` |

`python scripts/pipeline.py targets` shows the same answers on the command
line: the families; `--family <code>` for a family's boards and devices;
`--board <name>` for a board's part, memory and I/O.

### `build/` -- every build

Each build gets its own folder, named for the target and when it started:

```
build/
  KV260-2026-10-01_14-32-05/
    config.toml        the configuration, as chosen; re-runs this build exactly
    plan.md            what step 5 showed
    report.md          what step 10 showed, plus report.json
    logs/              every tool's full output
    generated/         every generated source: HLS parameters, block-design Tcl,
                       device tree, boot image description
    vitis/             each HLS component's synthesis, simulation and packaged IP
    lockstep/          step 8: each test's DoomV report, traces of what failed, snapshots
    vivado/            the project, checkpoints, and utilization and timing reports
    xdc/               the generated constraints
    boot/              FSBL, boot image description, BOOT.BIN (where the target needs one)
    software/          OpenSBI and Linux builds, when chosen
    bitstreams/        the final bitstream (and .bin for flash)
```

Nothing in `build/` is committed; the folder name and `config.toml` are enough
to reproduce a build.

## Progress

Long stages report progress the tools themselves expose:

- **Vitis HLS**: its synthesis steps (C compile, scheduling, binding, RTL
  generation, packaging), one bar per component; components run in parallel.
- **Vivado**: synthesis, then implementation's phases (opt, place, phys-opt,
  route), then bitstream -- each a sub-bar, from the phase markers in the log.
- **Lock-step** (step 8): tests done of tests total, per emulation and suite.

Every bar shows elapsed time and an estimate of what remains, from the last
comparable build when there is one. The full log of every tool is always in
`logs/`, so the screen can stay readable.

## Built on

- the pipeline in `scripts/pipeline.py` -- the same stages, reports and
  regression checks, driven by Ouroboros's configuration instead of
  `pipeline.toml`;
- the platform generator ([platform-generator.md](platform-generator.md));
- the I/O catalogue ([io-catalog.md](io-catalog.md)) for step 4's I/O list;
- lock-step with DoomV ([lockstep.md](lockstep.md)) for accuracy.

## Open

- The UI toolkit (the earlier research recommends Python with Rich or Textual,
  matching the pipeline's language).
- How Ouroboros identifies an attached board (step 2).
- Whether the `.gguf` folder is `gguf/` only, or also other folders the user
  points at.
