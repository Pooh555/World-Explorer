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
import re
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


def normalize_brace_spacing(source: str) -> str:
    """Ignore C++11 braced-list interior spaces that clang-format cannot retain."""
    source = re.sub(r"\{[ \t]+", "{", source)
    return re.sub(r"[ \t]+\}", "}", source)


def check_template_initialization(files: list[Path]) -> int:
    violations: list[str] = []
    typed_loop = re.compile(
        r"\bfor\s*\(\s*(?:std::size_t|(?:std::)?size_t|int|unsigned|"
        r"std::u?int\d+_t)\s+\w+\s*="
    )
    size_t_temporary = re.compile(r"\bstd::size_t\s*\{\s*\d+")
    size_t_declaration = re.compile(r"\bstd::size_t\s+\w+\s*\{([^{}\n]*)\}")
    spelled_declaration = re.compile(
        r"\b(?:(?:const|constexpr|inline|static|volatile|extern)\s+)*"
        r"(?:std::(?:size_t|u?int\d+_t|[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)|"
        r"(?:u?int\d+_t|size_t|int|unsigned(?:\s+int)?|long(?:\s+long)?|"
        r"float|double|bool|char|auto))\s+\w+\s*\{([^{}\n]*)\}"
    )
    assigned_list = re.compile(r"(?<![=!<>])=(?!=)\s*\{([^{}\n]*)\}")
    integer = re.compile(r"(?<![\w.])(\d+)([uUlLzZ]*)(?![\w.])")
    string_literal = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
    for path in files:
        source = path.read_text(encoding="utf-8")
        for line_number, line in enumerate(source.splitlines(), start=1):
            # Literal contents (including fixture headers) are data, not C++ tokens.
            code_line = string_literal.sub('""', line)
            if typed_loop.search(code_line):
                violations.append(f"{path}:{line_number}: use direct-list initialization in typed loops")
            if size_t_temporary.search(code_line):
                violations.append(f"{path}:{line_number}: use uz literals instead of std::size_t temporaries")
            for match in spelled_declaration.finditer(code_line):
                initializer = match.group(1)
                if initializer.strip() and not (
                    initializer.startswith(" ") and initializer.endswith(" ")
                ):
                    violations.append(
                        f"{path}:{line_number}: put spaces inside non-empty initializer braces"
                    )
            for match in assigned_list.finditer(code_line):
                initializer = match.group(1)
                if initializer.strip() and not (
                    initializer.startswith(" ") and initializer.endswith(" ")
                ):
                    violations.append(
                        f"{path}:{line_number}: put spaces inside non-empty initializer braces"
                    )
            for match in size_t_declaration.finditer(code_line):
                initializer = match.group(1)
                if initializer.strip() and not (
                    initializer.startswith(" ") and initializer.endswith(" ")
                ):
                    violations.append(f"{path}:{line_number}: put spaces inside std::size_t initializer braces")
                for literal in integer.finditer(initializer):
                    if literal.group(2).lower() != "uz":
                        violations.append(f"{path}:{line_number}: use uz suffixes for std::size_t literals")
    if violations:
        print("Template initialization checks failed:", file=sys.stderr)
        print("\n".join(violations), file=sys.stderr)
        return 1
    return 0


def check_format(format_executable: str, files: list[Path]) -> int:
    failed = False
    for path in files:
        relative = str(path.relative_to(ROOT))
        formatted = subprocess.run(
            [format_executable, relative],
            cwd=str(ROOT),
            check=False,
            capture_output=True,
            text=True,
        )
        if formatted.returncode != 0:
            print(formatted.stderr, file=sys.stderr)
            failed = True
            continue
        actual = normalize_brace_spacing(path.read_text(encoding="utf-8"))
        expected = normalize_brace_spacing(formatted.stdout)
        if actual != expected:
            print(f"clang-format check failed: {relative}", file=sys.stderr)
            failed = True
    return int(failed)


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

    format_rc = check_format(format_executable, files)
    if format_rc != 0:
        print("clang-format check failed; run 'clang-format -i ...' on the listed files.", file=sys.stderr)
        return format_rc

    initialization_rc = check_template_initialization(files)
    if initialization_rc != 0:
        return initialization_rc

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
