#!/usr/bin/env python3
"""Build, test for accuracy, and report on performance -- unattended.

  python scripts/pipeline.py check
      The pinned AMD tools and DoomV: present, the right version, and able to
      see the target's part. Run first; a multi-hour run should not be the
      thing that finds out.

  python scripts/pipeline.py run [--components a,b] [--stages hls,csim,cosim,impl,accuracy]
                                 [--suites x,y] [--target kv260] [--jobs N] [--label "..."]
      For each HLS component: synthesis (resources, estimated clock, latency),
      C simulation, co-simulation, out-of-context implementation (post-route
      resources and Fmax). Then the accuracy stage: every test of every suite
      run on the device under test and lock-stepped against DoomV, strictly.

  python scripts/pipeline.py targets [--family zynquplus] [--search kv260]
      What the program asks for a target, read from the Vitis installation:
      the families, then a family's boards and devices.

  python scripts/pipeline.py selftest [--limit N]
      The accuracy stage with DoomV standing in for the core, to prove the
      harness end to end before the core exists.

Every run writes Performance/runs/<id>/report.json and report.md, adds a line
to Performance/RUNS.md, compares itself with the last comparable run, and
exits non-zero if anything failed or regressed. Configuration is in
scripts/pipeline.toml.
"""
from __future__ import annotations

import argparse
import concurrent.futures as cf
import datetime as dt
import hashlib
import json
import os
import re
import subprocess
import sys
import time
import tomllib
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
from ouro import accuracy, hls, report, targets  # noqa: E402
from ouro.amd import AmdTools  # noqa: E402

BUILD = ROOT / "build" / "pipeline"
PERF = ROOT / "Performance"


def load_config() -> dict:
    cfg = tomllib.loads((HERE / "pipeline.toml").read_text())
    cfg["tools"]["root"] = os.environ.get("OUROBOROS_AMD_ROOT", cfg["tools"]["root"])
    cfg["doomv"]["root"] = os.environ.get("OUROBOROS_DOOMV_ROOT", cfg["doomv"]["root"])
    return cfg


def rooted(p: str) -> Path:
    q = Path(p)
    return q if q.is_absolute() else ROOT / q


def say(msg: str):
    print(f"[{dt.datetime.now():%H:%M:%S}] {msg}", flush=True)


def git_commit() -> str:
    r = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, capture_output=True, text=True)
    dirty = subprocess.run(["git", "status", "--porcelain", "--untracked-files=no"], cwd=ROOT,
                           capture_output=True, text=True).stdout.strip()
    return r.stdout.strip() + ("+" if dirty else "")


# ---- DoomV: always the newest ---------------------------------------------------

def follow_doomv(cfg: dict) -> dict:
    """Ouroboros follows DoomV's main branch (decisions, 2026-10-07): before
    anything runs, the DoomV submodule moves to the newest commit on main, and
    is rebuilt if it has been built here before. OUROBOROS_DOOMV_ROOT names a
    checkout that is used as it is. Returns what happened, for the report."""
    root = rooted(cfg["doomv"]["root"])
    git = lambda *a: subprocess.run(["git", "-C", str(root), *a], capture_output=True, text=True)
    out = {"root": str(root)}
    if os.environ.get("OUROBOROS_DOOMV_ROOT"):
        out.update(commit=git("rev-parse", "--short", "HEAD").stdout.strip(), followed=False,
                   note="OUROBOROS_DOOMV_ROOT: used as it is")
        DOOMV_USED.update(out)
        return out
    if git("fetch", "-q", "origin", "main").returncode:
        out.update(commit=git("rev-parse", "--short", "HEAD").stdout.strip(), followed=False,
                   note="could not fetch DoomV; using the commit checked out")
        say(f"DoomV: {out['note']} ({out['commit']})")
        DOOMV_USED.update(out)
        return out
    before = git("rev-parse", "HEAD").stdout.strip()
    latest = git("rev-parse", "origin/main").stdout.strip()
    out.update(commit=latest[:7], followed=True, moved=before != latest)
    if before != latest:
        git("checkout", "-q", latest)
        say(f"DoomV: moved to the newest main, {before[:7]} -> {latest[:7]} "
            "(commit the submodule to record it)")
    exe = root / cfg["doomv"]["exe"]
    if exe.exists() and before != latest:
        say("DoomV: rebuilding ...")
        r = subprocess.run(["make"], cwd=root, capture_output=True, text=True)
        out["rebuilt"] = r.returncode == 0
        if r.returncode:
            say("DoomV: the build failed -- see `make` in " + str(root))
    DOOMV_USED.update(out)
    return out


# What follow_doomv found: each run's report names the DoomV it ran against.
DOOMV_USED: dict = {}


def pick_target(cfg: dict, name: str | None) -> tuple[str, dict]:
    """A target from the Vitis installation: a board, or a full part. The
    clock is a configuration choice, from the defaults."""
    name = name or cfg["defaults"]["target"]
    try:
        t = targets.resolve(name, rooted(cfg["tools"]["root"]))
    except LookupError as e:
        sys.exit(str(e))
    t["clock_mhz"] = cfg["defaults"]["clock_mhz"]
    return name, t


def cmd_targets(args, cfg) -> int:
    """What the program will ask: family, then FPGA -- all read from Vitis."""
    tools_root = rooted(cfg["tools"]["root"])
    fams = targets.families(tools_root)
    brds = targets.boards(tools_root)
    q = (args.search or "").lower()
    if args.board:
        try:
            t = targets.resolve(args.board, tools_root)
        except LookupError as e:
            sys.exit(str(e))
        print(f"{t['name']}\n  part     {t['part']}  ({targets.family_name(t['family'])})\n"
              f"  boards   {', '.join(t['boards'])}\n\nMemory:")
        for m in t["memory"]:
            size = f"{m['bytes'] / 2**30:.0f} GB, " if m.get("bytes") else ""
            print(f"  {m['type']:10} {size}{m.get('bus_bits') or '?'}-bit {m.get('speed') or ''}  on the {m['where']}")
        print("\nPeripherals in the processing system (hard IP):")
        print("  " + (", ".join(t["ps_peripherals"]) or "--"))
        print("\nInterfaces on fabric pins:")
        for f in t["fabric_io"]:
            print(f"  {f['name']:22} {f['type']:18} ({f['board']})")
        return 0
    if not args.family:
        print(f"Families Vitis has installed ({tools_root}): {len(fams)}, "
              f"{sum(len(f['devices']) for f in fams)} devices, {len(brds)} boards, "
              f"{len(targets.platforms(tools_root))} platforms\n")
        for f in fams:
            nb = sum(1 for b in brds if b["family"] == f["code"])
            if not q or q in f["name"].lower() or q in f["code"].lower():
                print(f"  {f['code']:24} {f['name']:42} {len(f['devices']):3} devices  {nb:2} boards")
        print("\nthen: --family <code> to list its FPGAs, --board <name> for a board's I/O and memory")
        return 0
    fam = next((f for f in fams if f["code"] == args.family), None)
    if not fam:
        sys.exit(f"no family '{args.family}' in Vitis's installed devices")
    print(f"{fam['name']} ({fam['code']})\n\nBoards (each fixes the exact part):")
    for b in brds:
        if b["family"] == fam["code"] and (not q or q in b["id"].lower() or q in b["name"].lower()):
            print(f"  {b['id']:40} {b['name'][:44]:44} {b['part']}")
    print("\nDevices (give device-package-speed):")
    for d in fam["devices"]:
        if not q or q in d.lower():
            print(f"  {d:16} packages: {', '.join(targets.packages(d, tools_root)) or '--'}")
    return 0


# ---- check ----------------------------------------------------------------------

# Counting all parts tells "nothing usable at all" (a licence tier that covers
# none of the installed families) from "this part missing"; trying to use the
# part makes Vivado name the licence tier when that is the reason.
PARTS_TCL = """puts "OUROBOROS_PARTS [llength [get_parts -quiet {part}]] [llength [get_parts -quiet]]"
catch {{create_project -in_memory -part {part}}}
"""
LICENCE_TIER = re.compile(r"current selected license is (\w+)", re.I)


def part_visible(tools: AmdTools, part: str) -> tuple[bool, str]:
    """Whether Vivado can use the part: installed, and licensed. When it
    cannot, says why as precisely as Vivado does."""
    BUILD.mkdir(parents=True, exist_ok=True)
    tcl = BUILD / "check_part.tcl"
    tcl.write_text(PARTS_TCL.format(part=part))
    status, _ = tools.run("vivado", ["-mode", "batch", "-nojournal", "-nolog", "-source", str(tcl)],
                          log=BUILD / "check_part.log", cwd=BUILD, timeout=1800)
    text = (BUILD / "check_part.log").read_text(errors="replace")
    tier = LICENCE_TIER.search(text)
    for line in text.splitlines():
        if line.startswith("OUROBOROS_PARTS"):
            seen, total = (int(x) for x in line.split()[1:3])
            if seen:
                return True, ""
            if tier:
                return False, (f"the selected licence tier is {tier.group(1)}, which does not cover this part "
                               f"(Vivado sees {total} parts in all) -- change it in the Vivado License Manager "
                               f"({tools.root / 'Vivado' / 'bin' / 'vlm.bat'}, 'Manage Licenses')")
            return False, (f"Vivado cannot see the part ({total} parts visible in all): "
                           f"not installed, or not licensed")
    return False, f"Vivado did not answer (exit {status}); see {BUILD / 'check_part.log'}"


def cmd_check(args, cfg) -> int:
    ok = True
    tools = AmdTools(rooted(cfg["tools"]["root"]), cfg["tools"]["version"])
    say(f"AMD tools at {tools.root}, pinned {tools.version}")
    for tool, r in tools.check().items():
        print(f"  {tool:10} {'ok ' + r['version'] if r['ok'] else 'NO: ' + r['why']}")
        ok &= r["ok"]
    name, target = pick_target(cfg, args.target)
    if ok:
        seen, why = part_visible(tools, target["part"])
        print(f"  {'part':10} {target['part']} ({name}): {'ok' if seen else 'NO: ' + why}")
        ok &= seen
    droot = rooted(cfg["doomv"]["root"])
    exe = droot / cfg["doomv"]["exe"]
    print(f"  {'DoomV':10} {exe}: {'ok' if exe.exists() else 'NO: not built (see its README)'}")
    ok &= exe.exists()
    for sname, s in cfg.get("suites", {}).items():
        n = sum(1 for p in droot.glob(s["glob"]) if p.is_file() and not p.suffix)
        print(f"  {'suite':10} {sname}: {n} tests" + ("" if n else " -- NO TESTS (fetch them: see Tools/Verification/README.md)"))
    print("ready" if ok else "NOT READY")
    return 0 if ok else 1


# ---- run ------------------------------------------------------------------------

def build_component(name: str, comp: dict, stages: list[str], target_name: str, target: dict,
                    tools: AmdTools, run_dir: Path, timeouts: dict) -> dict:
    comp_dir = rooted(comp["dir"])
    work = BUILD / target_name / name
    logs = run_dir / "logs" / name
    cfg_path = work / "hls_config.cfg"
    out = {"notes": hls.write_config(comp_dir, target, cfg_path)}
    for stage in stages:
        if stage == "cosim" and not comp.get("cosim", True):
            continue
        say(f"{name}: {stage} ...")
        r = hls.run_stage(tools, stage, cfg_path, work / "work", logs, timeouts[stage])
        out[stage] = r
        say(f"{name}: {stage} {'ok' if r['ok'] else 'FAILED'} in {r['seconds']:.0f} s"
            + ("" if r["ok"] else f" -- {(r.get('errors') or ['see ' + r['log']])[0]}"))
        if not r["ok"]:
            break          # the later stages need this one
    return out


def finish(r: dict, started: float) -> int:
    r["seconds"] = round(time.monotonic() - started, 1)
    builds_ok = all(s.get("ok", True) for c in r.get("components", {}).values() for s in c.values() if isinstance(s, dict))
    acc_ok = all(s["mismatch"] == 0 and s["error"] == 0 for s in r.get("accuracy", {}).values())
    r["ok"] = builds_ok and acc_ok
    d = report.write(r, PERF)
    r["ok"] = r["ok"] and not r["regressions"]
    (d / "report.json").write_text(json.dumps(r, indent=2) + "\n")
    print()
    print((d / "report.md").read_text())
    say(f"{'PASS' if r['ok'] else 'FAIL'}: report in {d}")
    return 0 if r["ok"] else 1


def new_run(label: str, target_name: str, key_parts: dict, tools_version: str) -> dict:
    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    slug = "".join(c if c.isalnum() else "-" for c in label.lower()).strip("-")[:40]
    key = hashlib.sha256(json.dumps(key_parts, sort_keys=True).encode()).hexdigest()[:16]
    return {"id": f"{stamp}-{slug}" if slug else stamp, "label": label, "commit": git_commit(), "doomv": DOOMV_USED.get("commit"),
            "target": target_name, "tools": tools_version, "key": key, "config": key_parts,
            "components": {}, "accuracy": {}, "notes": []}


def cmd_run(args, cfg) -> int:
    started = time.monotonic()
    target_name, target = pick_target(cfg, args.target)
    all_comps = cfg.get("components", {})
    comps = args.components.split(",") if args.components else list(all_comps)
    stages = args.stages.split(",")
    unknown = [c for c in comps if c not in all_comps]
    if unknown:
        sys.exit(f"unknown components: {', '.join(unknown)}")
    suites = args.suites.split(",") if args.suites else []
    # Comparable runs share their configuration, not only their component
    # names: each component's HLS config and declared parameters are part of
    # the key, so two configurations of one component are never compared.
    comp_config = {c: {"hls_config": hashlib.sha256((rooted(all_comps[c]["dir"]) / "hls_config.cfg").read_bytes()).hexdigest()[:16],
                       "declared": {k: v for k, v in all_comps[c].items() if k != "dir"}} for c in comps}
    r = new_run(args.label, target_name, {"components": comps, "component_config": comp_config, "stages": stages,
                                          "target": {k: target[k] for k in ("part", "clock_mhz")},
                                          "suites": suites}, cfg["tools"]["version"])
    run_dir = PERF / "runs" / r["id"]
    hw_stages = [s for s in stages if s in hls.STAGES]
    if hw_stages and comps:
        tools = AmdTools(rooted(cfg["tools"]["root"]), cfg["tools"]["version"])
        bad = {t: v for t, v in tools.check().items() if not v["ok"]}
        if bad:
            r["notes"].append("AMD tools not usable: " + "; ".join(f"{t} {v['why']}" for t, v in bad.items()))
        else:
            timeouts = {"hls": args.timeout, "csim": args.timeout, "cosim": args.timeout * 2, "impl": args.timeout * 2}
            with cf.ThreadPoolExecutor(max(1, args.jobs)) as pool:
                futs = {pool.submit(build_component, c, all_comps[c], hw_stages, target_name, target, tools,
                                    run_dir, timeouts): c for c in comps}
                for f in cf.as_completed(futs):
                    res = f.result()
                    r["notes"] += res.pop("notes")
                    r["components"][futs[f]] = res
    if "accuracy" in stages:
        cores = [c for c in comps if all_comps[c].get("kind") == "core" and all_comps[c].get("dut")]
        if not cores:
            r["notes"].append("accuracy: no core component with a `dut` yet; use `selftest` to exercise the harness")
        for c in cores:
            # The core's own ISA: its `march` overrides the suites', so a
            # narrower configuration is stepped against DoomV configured the same.
            r["accuracy"].update(run_accuracy(cfg, all_comps[c]["dut"], suites or list(cfg["suites"]), args,
                                              march=all_comps[c].get("march")))
    return finish(r, started)


def run_accuracy(cfg, dut, suite_names, args, march: str | None = None) -> dict:
    droot = rooted(cfg["doomv"]["root"])
    exe = droot / cfg["doomv"]["exe"]
    if not exe.exists():
        sys.exit(f"DoomV is not built: {exe}")
    last = {}

    def progress(suite, i, n, row):
        if row["verdict"] in ("mismatch", "error") or i == n or time.monotonic() - last.get(suite, 0) > 10:
            last[suite] = time.monotonic()
            say(f"{suite}: {i}/{n}  {row['test']}: {row['verdict']}")

    suites = {n: dict(s, march=march) if march else s for n, s in cfg["suites"].items()}
    return accuracy.run_suites(suites, suite_names, droot, exe, dut, BUILD / "accuracy",
                               args.jobs, args.timeout, args.limit, progress)


def cmd_selftest(args, cfg) -> int:
    started = time.monotonic()
    names = args.suites.split(",") if args.suites else cfg["selftest"]["suites"]
    r = new_run(args.label or "selftest: DoomV as the device under test", "doomv-stub",
                {"selftest": True, "suites": names, "limit": args.limit}, cfg["tools"]["version"])
    r["notes"].append("DoomV stands in for the core: this proves the harness, not a design")
    r["accuracy"] = run_accuracy(cfg, cfg["selftest"]["dut"], names, args)
    return finish(r, started)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    t = sub.add_parser("targets", help="families, boards and devices, from the Vitis installation")
    t.add_argument("--family", help="list this family's boards and devices")
    t.add_argument("--board", help="a board's part, memory and I/O, with its companion boards")
    t.add_argument("--search", help="only entries matching this")
    for name in ("check", "run", "selftest"):
        p = sub.add_parser(name)
        p.add_argument("--target")
        p.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 2))
        p.add_argument("--timeout", type=float, default=4 * 3600, help="per stage or test, seconds")
        p.add_argument("--label", default="")
        p.add_argument("--suites")
        p.add_argument("--limit", type=int, help="at most this many tests per suite")
        if name == "run":
            p.add_argument("--components")
            p.add_argument("--stages", default="hls,csim,cosim,impl,accuracy")
    args = ap.parse_args()
    cfg = load_config()
    if args.cmd in ("check", "run", "selftest"):
        cfg["doomv"]["followed"] = follow_doomv(cfg)
    return {"check": cmd_check, "run": cmd_run, "selftest": cmd_selftest, "targets": cmd_targets}[args.cmd](args, cfg)


if __name__ == "__main__":
    sys.exit(main())
