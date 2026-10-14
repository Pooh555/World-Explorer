#!/usr/bin/env python3
"""Run fresh CPU tests and report LLVM coverage under the reviewed E1 scope."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SCOPE = ROOT / "tools/coverage_scope.json"


def source_files(root, scope):
    files = {}
    for prefix in scope["roots"]:
        for path in (root / prefix).rglob("*"):
            if path.suffix not in (".cpp", ".hpp"):
                continue
            module = path.relative_to(root / prefix).parts[0]
            if module not in scope["excluded_modules"]:
                files[path.relative_to(root).as_posix()] = path
    return files


def summarize(export, files, scope, root):
    if export.get("type") != "llvm.coverage.json.export" or len(export.get("data", [])) != 1:
        raise ValueError("Expected one LLVM source coverage dataset")
    rows = {}
    for entry in export["data"][0]["files"]:
        path = Path(entry["filename"]).resolve()
        if not path.is_relative_to(root):
            continue
        name = path.relative_to(root).as_posix()
        if name not in files:
            continue
        if name in rows:
            raise ValueError(f"Duplicate coverage file: {name}")
        row = {}
        for metric in ("lines", "branches"):
            counts = entry["summary"][metric]
            count, covered = counts["count"], counts["covered"]
            if type(count) is not int or type(covered) is not int or not 0 <= covered <= count:
                raise ValueError(f"Invalid {metric} counters for {name}")
            row[metric] = {"count": count, "covered": covered}
        rows[name] = row
    missing = sorted(name for name in files if name.endswith(".cpp") and name not in rows)
    if missing:
        raise ValueError(f"Production translation units missing from coverage: {missing}")
    groups = {}
    for group in ("non_gpu_core", "package_update"):
        members = rows if group == "non_gpu_core" else {
            name: row for name, row in rows.items()
            if any(name.startswith(f"{prefix}/{module}/")
                   for prefix in scope["roots"] for module in scope["package_update_modules"])
            or name in scope["package_update_files"]
        }
        totals = {}
        for metric in ("lines", "branches"):
            count = sum(row[metric]["count"] for row in members.values())
            covered = sum(row[metric]["covered"] for row in members.values())
            if not count:
                raise ValueError(f"Empty coverage denominator: {group}/{metric}")
            totals[metric] = {"count": count, "covered": covered, "percent": 100 * covered / count}
        totals["files"] = sorted(members)
        groups[group] = totals
    goals = {"core_lines_80": groups["non_gpu_core"]["lines"]["percent"] >= 80,
             "core_branches_70": groups["non_gpu_core"]["branches"]["percent"] >= 70,
             "package_update_branches_90": groups["package_update"]["branches"]["percent"] >= 90}
    return {"groups": groups, "goals": goals, "coverage_goals_met": all(goals.values()),
            "files": rows, "unmapped_headers": sorted(set(files) - set(rows))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("build-dir", "binary", "ctest", "llvm-cov", "llvm-profdata", "compiler"):
        parser.add_argument(f"--{name}", required=True)
    parser.add_argument("--object", action="append", required=True)
    parser.add_argument("--enforce", action="store_true", help="Exit 1 if any unchanged E1 coverage goal is unmet")
    args = parser.parse_args()
    build = Path(args.build_dir).resolve()
    output = build / "coverage"
    output.mkdir(exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix="run-", dir=output))
    commands = []

    def execute(argv, log, env=None):
        with (run / log).open("w") as stream, (run / f"{log}.stderr").open("w") as errors:
            result = subprocess.run(argv, cwd=ROOT, env=env, stdout=stream, stderr=errors)
        commands.append({"argv": argv, "cwd": str(ROOT), "exit": result.returncode,
                         "log": log, "stderr": f"{log}.stderr"})
        (run / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        if result.returncode:
            raise RuntimeError(f"Command failed ({result.returncode}): {argv}; see {run / log}")
        return (run / log).read_text()

    versions = {}
    for name in ("compiler", "llvm_cov", "llvm_profdata"):
        versions[name] = execute([getattr(args, name), "--version"], f"{name}-version.txt")
    majors = [re.search(r"(?:clang version|LLVM version)\s+(\d+)", version)
              for version in versions.values()]
    if any(match is None for match in majors) or len({match[1] for match in majors}) != 1:
        raise ValueError("Compiler, llvm-cov and llvm-profdata must use the same LLVM major")
    scope = json.loads(SCOPE.read_text())
    files = source_files(ROOT, scope)
    (run / "scope.json").write_text(json.dumps(scope, indent=2) + "\n")
    identities = {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in files.items()}
    (run / "source-sha256.json").write_text(json.dumps(identities, indent=2) + "\n")
    inputs = [ROOT / "CMakeLists.txt", ROOT / "CMakePresets.json", SCOPE,
              Path(__file__).resolve(), build / "CMakeCache.txt", build / "compile_commands.json",
              Path(args.binary), *map(Path, args.object)]
    for directory in ("tests", "workers"):
        inputs.extend(path for path in (ROOT / directory).rglob("*")
                      if path.suffix in (".cpp", ".hpp", ".py"))
    (run / "input-sha256.json").write_text(json.dumps({
        str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs
    }, indent=2) + "\n")
    (run / "source.diff").write_text(execute(["git", "diff", "HEAD", "--"], "source-diff.log"))
    execute(["git", "rev-parse", "HEAD"], "parent.txt")
    profiles = run / "profiles"
    profiles.mkdir()
    env = dict(os.environ, LLVM_PROFILE_FILE=str(profiles / "%p-%m.profraw"))
    execute([args.ctest, "--test-dir", str(build), "--output-on-failure"], "ctest.log", env)
    raw = sorted(profiles.glob("*.profraw"))
    if not raw:
        raise ValueError("Tests produced no raw coverage profiles")
    listing = run / "profiles.txt"
    listing.write_text("".join(f"{path}\n" for path in raw))
    profdata = run / "coverage.profdata"
    execute([args.llvm_profdata, "merge", "-sparse", "--failure-mode=any",
             f"--input-files={listing}", "-o", str(profdata)], "merge.log")
    objects = [args.binary]
    for obj in args.object:
        objects.extend(["-object", obj])
    selection = [f"-instr-profile={profdata}", "--sources", *map(str, files.values())]
    execute([args.llvm_cov, "export", *objects, *selection], "export.json")
    summary = summarize(json.loads((run / "export.json").read_text()), files, scope, ROOT)
    summary.update({"scope_sha256": hashlib.sha256(SCOPE.read_bytes()).hexdigest(),
                    "profile_count": len(raw), "tools": versions})
    (run / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    execute([args.llvm_cov, "show", *objects, "-format=html", f"-output-dir={run / 'html'}",
             "-show-branches=count", *selection], "html.log")
    for name, group in summary["groups"].items():
        print(f"{name}: lines {group['lines']['percent']:.2f}%, branches {group['branches']['percent']:.2f}%")
    print(f"Coverage goals {'met' if summary['coverage_goals_met'] else 'UNMET'}; report: {run}")
    print("This measures local CPU coverage only; E1 sanitizer/GPU/platform qualification is separate.")
    return int(args.enforce and not summary["coverage_goals_met"])


if __name__ == "__main__":
    raise SystemExit(main())
