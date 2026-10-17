#!/usr/bin/env python3
"""Run the repository C++ style gate for first-party code.

This script mirrors the project policy described in AGENTS.md and the style
reference: format first-party C++ with clang-format, run clang-tidy against the
active build compile database when available, and reject trailing whitespace or
other git diff issues.
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SEARCH_ROOTS = ("apps", "include", "src", "tests", "workers")
CPP_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}


def discover_cpp_files(search_roots: tuple[str, ...]) -> list[Path]:
    files: list[Path] = []
    seen: set[Path] = set()
    for root_name in search_roots:
        root = ROOT / root_name
        if not root.exists():
            continue
        for path in sorted(root.rglob("*")):
            if not path.is_file() or path.suffix.lower() not in CPP_SUFFIXES:
                continue
            normalized = path.resolve()
            if normalized not in seen:
                seen.add(normalized)
                files.append(normalized)
    return files


def resolve_targets(provided: list[str]) -> list[Path]:
    targets: list[Path] = []
    for raw in provided:
        candidate = Path(raw)
        if not candidate.is_absolute():
            candidate = ROOT / candidate
        candidate = candidate.resolve()
        if not candidate.exists():
            print(f"Missing target: {candidate}", file=sys.stderr)
            raise SystemExit(1)
        if candidate.is_dir():
            for path in sorted(candidate.rglob("*")):
                if path.is_file() and path.suffix.lower() in CPP_SUFFIXES:
                    targets.append(path)
        else:
            targets.append(candidate)
    return targets


def select_tidy_files(files: list[Path], compile_db: Path | None = None) -> list[Path]:
    if compile_db is None or not compile_db.exists():
        return files

    try:
        database = json.loads(compile_db.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return files

    if not isinstance(database, list):
        return files

    project_roots = {(ROOT / root_name).resolve() for root_name in DEFAULT_SEARCH_ROOTS}
    known_files = {
        Path(entry["file"]).resolve()
        for entry in database
        if isinstance(entry, dict) and isinstance(entry.get("file"), str)
    }

    def is_project_file(path: Path) -> bool:
        resolved = path.resolve()
        return any(resolved.is_relative_to(root) for root in project_roots)

    return [path for path in files if path.resolve() in known_files and is_project_file(path)]


def run_command(command: list[str]) -> int:
    print("+ " + " ".join(command))
    return subprocess.run(command, cwd=str(ROOT)).returncode


def run_repo_quality_checks() -> int:
    quality_checks = [
        ["python3", str(ROOT / "tools/check_docs.py")],
        ["python3", str(ROOT / "tools/check_boundaries.py")],
    ]
    for command in quality_checks:
        rc = run_command(command)
        if rc != 0:
            print("Repository quality check failed.", file=sys.stderr)
            return rc
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Enforce project C++ style checks.")
    parser.add_argument(
        "paths",
        nargs="*",
        help="Files or directories to check. Defaults to the repo C++ source trees.",
    )
    parser.add_argument(
        "--build-dir",
        default="build/dev",
        help="Compile database directory for clang-tidy; skipped if absent.",
    )
    args = parser.parse_args()

    format_executable = shutil.which("clang-format")
    if not format_executable:
        print("clang-format is required but was not found in PATH.", file=sys.stderr)
        return 1

    if args.paths:
        files = resolve_targets(args.paths)
    else:
        files = discover_cpp_files(DEFAULT_SEARCH_ROOTS)

    if not files:
        print("No C++ files found to check.")
        return 0

    relative_files = [str(path.relative_to(ROOT)) for path in files]

    format_rc = run_command([format_executable, "--dry-run", "--Werror", *relative_files])
    if format_rc != 0:
        print("clang-format check failed; run 'clang-format -i ...' on the listed files.", file=sys.stderr)
        return format_rc

    build_dir = (ROOT / args.build_dir).resolve()
    compile_db = build_dir / "compile_commands.json"
    if compile_db.exists():
        tidy_exe = shutil.which("clang-tidy")
        if not tidy_exe:
            print("clang-tidy is required when compile_commands.json exists but was not found in PATH.", file=sys.stderr)
            return 1
        tidy_files = select_tidy_files(files, compile_db)
        if not tidy_files:
            print("No translation units are available in the compile database for clang-tidy.", file=sys.stderr)
            return 0
        tidy_relative = [str(path.relative_to(ROOT)) for path in tidy_files]
        tidy_rc = run_command([
            tidy_exe,
            "-p",
            str(build_dir),
            "--warnings-as-errors=*",
            *tidy_relative,
        ])
        if tidy_rc != 0:
            print("clang-tidy check failed.", file=sys.stderr)
            return tidy_rc
    else:
        print(f"clang-tidy skipped: {compile_db} is not present.")

    quality_rc = run_repo_quality_checks()
    if quality_rc != 0:
        return quality_rc

    diff_rc = run_command(["git", "-C", str(ROOT), "diff", "--check"])
    if diff_rc != 0:
        print("git diff --check failed; fix whitespace and trailing newline issues.", file=sys.stderr)
        return diff_rc

    print(f"C++ style checks passed for {len(relative_files)} file(s).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
