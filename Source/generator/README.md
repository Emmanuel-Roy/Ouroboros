# The generator

Ouroboros generates its core from a configuration (docs/decisions.md,
2026-10-21). This folder is the generator: the only place the core is
written.

```
python Source/generator/generate.py Source/generator/configs/rv64i-m.toml
```

writes `build/rv64i-m/generated/`:

| file | what |
|---|---|
| `config.hpp` | the configuration as compile-time constants (`cfg::`) |
| `core.hpp`, `core.cpp` | the core, from `templates/core/`, instantiated by `config.hpp` |
| `core_tb.cpp` | the testbench, from `templates/tb/` |
| `hls_config.cfg` | the Vitis HLS component: part, clock, top, testbench |
| `doomv.args` | DoomV's arguments for the same machine: `-march`, `-priv`, `-pmp`, `-misaligned`, `-cycle-clock` |
| `config.toml` | the configuration it came from |

**Nothing generated is ever edited by hand** -- not by a person, not by an
agent. If a generated core is wrong, the template, this script or the
configuration is fixed and the core generated again.

## What it generates today

| option | values the templates implement |
|---|---|
| ISA | RV64I, with Zicsr and Zifencei |
| privilege modes | machine mode only |
| pipeline | in order, five stages (IF ID EX MEM WB), stalling on hazards; CSR accesses, traps and fence.i serialised |
| branch prediction | static not-taken |
| misaligned loads and stores | trap |
| PMP | none |
| memory | on-chip test memory, loaded by the testbench (`bram`) |

The configurator offers an option value once its generated core passes the
lock-step against DoomV in SW-Emu and HW-Emu (docs/configurator.md);
`generate.py` refuses any value the templates do not implement yet.

## Running a generated core

The generated folder is a Vitis HLS component that keeps DoomV's harness
contract, so DoomV's co-run runs it:

```
python Tools/Verification/DoomV/tools/verification/corun.py --lockstep sw-emu --sims none \
    --component build/rv64i-m/generated --suite riscv-tests rv64ui-p-add
```

See [docs/simulation-harness.md](../../docs/simulation-harness.md).
