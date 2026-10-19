#!/usr/bin/env python3
"""Enforce first-party include direction without needing a reconstruction GPU."""

from pathlib import Path
import re
import sys

__all__ = [
    "ALLOWED",
    "ROOT",
    "errors",
]

ROOT = Path(__file__).resolve().parents[1]
ALLOWED = {
    "core": {"core"},
    "capture": {"core", "capture"},
    "reconstruction_api": {"core", "capture", "reconstruction_api"},
    "reconstruction_adapters": {
        "core",
        "capture",
        "reconstruction_api",
        "reconstruction_adapters",
    },
    "map_store": {"core", "capture", "reconstruction_api", "map_store"},
    "scene_store": {"core", "scene_store"},
    "render_core": {"core", "scene_store", "render_core"},
    "render_vulkan": {"core", "scene_store", "render_core", "render_vulkan"},
    "navigation": {"core", "navigation"},
    "application": {
        "core",
        "capture",
        "reconstruction_api",
        "reconstruction_adapters",
        "map_store",
        "scene_store",
        "navigation",
        "application",
    },
}
errors = []
for prefix in (ROOT / "include/world_explorer", ROOT / "src"):
    for path in prefix.rglob("*"):
        if path.suffix not in (".hpp", ".cpp"):
            continue
        module = path.relative_to(prefix).parts[0]
        for header in re.findall(r'#include\s+[<"]([^">]+)', path.read_text()):
            if header.startswith("world_explorer/"):
                dependency = header.split("/")[1]
                if dependency not in ALLOWED.get(module, set()):
                    errors.append(
                        f"{path.relative_to(ROOT)}: {module} cannot include {dependency}"
                    )
            if module != "render_vulkan" and any(
                header.startswith(name)
                for name in (
                    "vulkan/",
                    "vk_mem_alloc.h",
                    "SDL3/",
                    "torch/",
                    "colmap/",
                    "Jolt/",
                )
            ):
                errors.append(
                    f"{path.relative_to(ROOT)}: forbidden external header {header}"
                )
if errors:
    print("\n".join(errors), file=sys.stderr)
    raise SystemExit(1)
print("First-party include boundaries passed")
