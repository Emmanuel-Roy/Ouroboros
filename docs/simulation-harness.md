# Running the simulation harness

How to run the core in simulation and check it against DoomV, one retired
instruction at a time. The harness is DoomV's `corun.py`, in the DoomV
submodule; why it is built this way is in [lockstep.md](lockstep.md).

```
python Tools/Verification/DoomV/tools/verification/corun.py --lockstep sw-emu --sims none --suite riscv-tests
```

That line runs every riscv-test on the core in software emulation and checks
it against DoomV, strictly. It is the usual run.

## What runs

- **The core**, in one of Vitis HLS's two emulations (below). It is the
  device under test.
- **DoomV, always.** It runs inside the core's testbench process, through
  `doomv_lockstep.dll`. The testbench hands it every record the core
  retires, and DoomV checks each one strictly: privilege, pc, instruction,
  every register and CSR written, every store, every trap. Its clock comes
  from the core's cycle count, so time reads and interrupts are checked too.
- **Other simulators, only if asked** (`--sims`): Sail, Spike, Whisper and
  QEMU. Each runs the same program at the same time and is compared with
  what the core retired. They do not decide the verdict; DoomV does. When
  the core and DoomV disagree, they show whether the others side with DoomV
  or with the core.

## SW-Emu: software emulation

```
python Tools/Verification/DoomV/tools/verification/corun.py --lockstep sw-emu --sims none --suite riscv-tests
```

Vitis HLS C simulation. The core's C++ is compiled natively with its
testbench, once per run, and the resulting program runs once per test. This
is fast and is the main level: run it on every change.

## HW-Emu: hardware emulation

```
python Tools/Verification/DoomV/tools/verification/corun.py --lockstep hw-emu --sims none --suite riscv-tests --count 20
```

Vitis HLS C/RTL co-simulation. The core is synthesised once per run, and the
generated Verilog runs in XSim for each test; the records DoomV checks are
the ones the RTL produced. It is much slower (kHz) and co-simulations run one
at a time, so give it a short list of tests (`--count`, or test names). Run
it after every synthesis, to check that the RTL does what the C++ did.

## Choosing the simulators

`--sims` takes a comma-separated list of `sail`, `spike`, `whisper`, `qemu`
and `doomv`. DoomV in the core's process is always there, whatever `--sims`
says.

| `--sims` | runs beside the core | when |
|---|---|---|
| `none` | nothing: DoomV only | **the usual run** |
| `sail` | Sail | a mismatch, to see whether the reference itself agrees with DoomV |
| `sail,spike,whisper,qemu` | all four (the default when `--sims` is not given) | investigating a disagreement |
| `doomv` | DoomV's own trace, run on its own | rarely; it adds nothing to the in-process check |

Leave `--sims` at `none` unless you are chasing a mismatch: the others cost
time, run in WSL, and only add evidence. If you leave `--sims` out entirely,
all four run.

## Choosing the programs

| arguments | runs |
|---|---|
| `--suite riscv-tests` | every test in DoomV's `tools/verification/tests/suites/riscv-tests` |
| `--suite riscv-tests rv64ui-p-add rv64mi-p-scall` | those tests only |
| `--suite riscv-tests --count 20` | the first 20 |
| `path/to/program.elf` | any bare-metal ELF that ends by writing `tohost` |

`--limit N` caps the instructions per program (default 2 million), and
`--jobs N` sets how many programs run at once (default 2). Programs run from
reset; a core cannot be started from a snapshot yet.

## Choosing the core

`--component <dir>` names the Vitis HLS component: a folder with an
`hls_config.cfg` whose paths are relative to it. The harness adds the part
(the KV260's) and a 10 ns clock if the file names none, and the flags that
link DoomV's library. `--cycle-clock N` is the core's cycles per `mtime`
tick, which must match the timebase the core gives `mtime`.

Until the core exists, the default component is a stand-in for a core's
retirement port, in DoomV's `tools/verification/lockstep_lib/vitis`. It
replays a recorded Sail run through synthesised hardware, so the harness can
be exercised end to end now. Sail runs for it whatever `--sims` says. To see
a failure, `DOOMV_LS_STANDIN_FAULT=<n>` flips a bit in a register value at
record `n`, as a buggy core would.

### What the core's testbench has to do

The harness passes everything through the environment:

| variable | what |
|---|---|
| `DOOMV_LS_ARGS` | DoomV's arguments: pass to `doomv_ls_open_line` |
| `DOOMV_LS_ELF` | the program to load and run |
| `DOOMV_LS_LIMIT` | stop after this many instructions |

The testbench loads the program into the core and runs it. For every
instruction or trap the core retires, it fills a `doomv_ls_record` and calls
`doomv_ls_step`. On anything but `DOOMV_LS_MATCH` it prints
`doomv_ls_message` and stops; `DOOMV_LS_STOPPED` after the program wrote
`tohost` is the normal end. It returns 0 only if every record matched. The
record and the calls are documented in DoomV's `src/doomv_lockstep.h`.

## Reading the report

For each program:

```
== rv64ui-p-add   (entry 0x80000000, VLEN 128)
   core      513 steps (the reference)
   sail      matches the core (sw-emu), 513 steps
   lock-step DoomV in the core (sw-emu) (strict, in-process): 513 records ... matched DoomV
```

On a mismatch the lock-step line says `MISMATCH`, with the record where the
core and DoomV first differ, the field (`x14: reference ..., DoomV ...`), and
a DoomV snapshot from just before that instruction, with the command that
restores it. Step it from there in DoomV's debugger or with gdb. Each
simulator in `--sims` shows where it first parts from the core.

Traces, reports (`report.json`) and snapshots are in DoomV's `build/corun/`,
one folder per program. The exit status is 0 only if the core matched DoomV
on every program.

## Before the first run

- DoomV built: `make` in `Tools/Verification/DoomV`. The harness builds the
  library (`make lockstep-lib`) itself.
- The test suites fetched: see [Tools/Verification/README.md](../Tools/Verification/README.md).
- Vitis 2026.1 installed. The harness finds it under `Z:/FPGA`, `C:/Xilinx`
  or `C:/AMD`; set `VITIS_GXX` to its MinGW `g++.exe` if it is elsewhere.
  Paths must have no spaces (Vitis's Tcl breaks on them).
- Only with `--sims`: Sail, Spike, Whisper and QEMU in WSL, as DoomV's
  README describes.
