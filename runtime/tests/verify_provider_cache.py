#!/usr/bin/env python3
"""Compare separately compiled native cache modes on authored inputs and real W^X."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    sources = [root / name for name in ("jit.c", "arch.c", "test_provider_cache.c")]
    tracked = sorted(set(sources + list(root.glob("*.h")) + list(root.glob("*.inc")) + [Path(__file__).resolve()]))
    before = {str(p): sha(p) for p in tracked}
    modes = {"cached": [], "uncached": ["-DNEXTCORE_DISABLE_PROVIDER_CACHE"],
             "small-slot": ["-DNEXTCORE_PROVIDER_CACHE_SLOT_BYTES=64"]}
    commands, summaries = [], {}
    for mode, defines in modes.items():
        folder = output / mode
        folder.mkdir()
        binary = folder / "test"
        command = [args.cc, "-O2", "-g", "-std=gnu11", "-Wall", "-Wextra",
                   *defines, *map(str, sources), "-o", str(binary)]
        commands.append(command)
        build = subprocess.run(command, capture_output=True, text=True)
        (folder / "build.log").write_text(build.stdout + build.stderr)
        build.check_returncode()
        run = subprocess.run([str(binary), str(folder)], capture_output=True, text=True)
        (folder / "stdout.log").write_text(run.stdout)
        (folder / "stderr.log").write_text(run.stderr)
        run.check_returncode()
        summaries[mode] = {fields[0]: dict(zip(("protect", "rw", "rx", "status", "retired"), map(int, fields[1:])))
                           for line in run.stdout.splitlines() if (fields := line.split())}
    baseline = sorted((output / "uncached").glob("*.bin"))
    assert len(baseline) == 13
    for mode in ("cached", "small-slot"):
        assert summaries[mode].keys() == summaries["uncached"].keys()
        for original in baseline:
            assert original.read_bytes() == (output / mode / original.name).read_bytes(), (mode, original.name)
    assert summaries["cached"]["loop"]["protect"] < summaries["uncached"]["loop"]["protect"]
    assert summaries["cached"]["small-buffer"] == summaries["uncached"]["small-buffer"]
    # The loop contains a native entry exceeding 64 bytes; full-buffer fallback
    # must execute it successfully and invalidate entries occupying that buffer.
    assert summaries["small-slot"]["slot-fallback"]["protect"] == summaries["uncached"]["slot-fallback"]["protect"]
    assert summaries["cached"]["slot-fallback"]["protect"] < summaries["small-slot"]["slot-fallback"]["protect"]
    # These source mutants live only in the output directory. Compilation must
    # succeed; rejection requires an executed assertion or full-state mismatch.
    original_source = (root / "jit.c").read_text()
    mutants = {
        "ignore-pc": ("cache[i].pc==cpu->pc &&", "1 &&", []),
        "ignore-word": ("cache[i].instruction==instruction &&", "1 &&", []),
        "ignore-el": ("cache[i].current_el==cpu->current_el)", "1)", []),
        "retain-overwritten-slots": ("for(size_t i=0;i<slots;i++)cache[i].valid=0;", "(void)slots;",
                                      ["-DNEXTCORE_PROVIDER_CACHE_SLOT_BYTES=64"]),
        "skip-hit-entry-count": ("status=execute_native_block(cpu,&entry,0,0);",
                                  "if(hit)cpu->compiled_blocks--;status=execute_native_block(cpu,&entry,0,0);", []),
    }
    rejected = []
    for name, (needle, replacement, defines) in mutants.items():
        assert original_source.count(needle) == 1, name
        folder = output / name
        folder.mkdir()
        source = folder / "jit.c"
        source.write_text(original_source.replace(needle, replacement))
        binary = folder / "test"
        command = [args.cc, "-O2", "-std=gnu11", "-I", str(root), *defines,
                   str(source), str(root / "arch.c"), str(root / "test_provider_cache.c"), "-o", str(binary)]
        commands.append(command)
        subprocess.run(command, check=True, capture_output=True, text=True)
        run = subprocess.run([str(binary), str(folder)], capture_output=True, text=True)
        (folder / "stdout.log").write_text(run.stdout)
        (folder / "stderr.log").write_text(run.stderr)
        different = any(not (folder / original.name).exists() or
                        original.read_bytes() != (folder / original.name).read_bytes() for original in baseline)
        assert run.returncode != 0 or different, f"mutant survived: {name}"
        rejected.append(name)
    after = {str(p): sha(p) for p in tracked}
    assert before == after, "source changed during proof"
    receipt = {"passed": True, "source_sha256": before, "sources_preserved": True,
               "commands": commands, "cases": len(baseline), "modes": summaries, "rejected_mutants": rejected,
               "comparison": "complete CPU bytes, result bytes, RAM, ordered requests/replies/callback results",
               "actual_mprotect_wx": True, "physical_boot_verified": False}
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({"passed": True, "receipt": str(output / "receipt.json"), "cases": len(baseline)}))


if __name__ == "__main__":
    main()
