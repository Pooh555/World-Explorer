#!/usr/bin/env python3
"""Check repository-local Markdown link targets; never access the network.

Checks docs/, README.md and AGENTS.md. Fenced examples and external links are
excluded. Fragments are not validated: heading anchors depend on the renderer.
"""

from pathlib import Path
import re
import sys
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
LINK = re.compile(r"!?\[[^\]\n]*\]\((<[^>]+>|[^\s)]+)(?:\s+\"[^\"]*\")?\)")
REFERENCE = re.compile(r"^\s{0,3}\[[^\]]+\]:\s*(<[^>]+>|\S+)")
FENCE = re.compile(r"^\s{0,3}(`{3,}|~{3,})")


def targets(text):
    fence_char = None
    fence_length = 0
    for number, line in enumerate(text.splitlines(), 1):
        marker = FENCE.match(line)
        if marker:
            token = marker.group(1)
            if fence_char is None:
                fence_char, fence_length = token[0], len(token)
            elif token[0] == fence_char and len(token) >= fence_length:
                fence_char = None
            continue
        if fence_char is not None:
            continue
        for match in LINK.finditer(line):
            yield number, match.group(1).strip("<>")
        reference = REFERENCE.match(line)
        if reference:
            yield number, reference.group(1).strip("<>")


def main():
    paths = sorted((ROOT / "docs").rglob("*.md")) + [
        ROOT / "README.md",
        ROOT / "AGENTS.md",
    ]
    errors = []
    count = 0
    for path in paths:
        for number, target in targets(path.read_text(encoding="utf-8")):
            url = urlsplit(target)
            if url.scheme or url.netloc or not url.path:
                continue
            count += 1
            # Root-relative Markdown links are repository-relative, not host paths.
            base = ROOT if url.path.startswith("/") else path.parent
            destination = (base / unquote(url.path.lstrip("/"))).resolve()
            if not destination.is_relative_to(ROOT) or not destination.exists():
                errors.append(
                    f"{path.relative_to(ROOT)}:{number}: invalid local link {target}"
                )
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print(f"Documentation links passed: {count} local targets in {len(paths)} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
