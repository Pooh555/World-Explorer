"""Bounded match-graph observations; connectivity never certifies 3D registration."""

from contextlib import closing
from pathlib import Path
import re
import sqlite3
import tempfile

from .model_selection import select_candidate

PAIR_BASE = 2147483647
MAX_FRAMES = 120
MAX_PAIRS = MAX_FRAMES * (MAX_FRAMES - 1) // 2
MAX_DATABASE_BYTES = 1024**3


def read_matches(path):
    path = Path(path)
    if (
        path.is_symlink()
        or not path.is_file()
        or path.stat().st_size > MAX_DATABASE_BYTES
    ):
        raise ValueError("Match database must be a bounded regular file")
    # Sidecars may own uncheckpointed data. Require an offline snapshot;
    # do not silently inspect only its main database. Even mode=ro can create
    # WAL bookkeeping, so open a private copy and leave the input directory alone.
    if any(
        Path(str(path) + suffix).exists() or Path(str(path) + suffix).is_symlink()
        for suffix in ("-wal", "-shm", "-journal")
    ):
        raise ValueError("Match database has a live or unreviewed SQLite sidecar")
    with tempfile.TemporaryDirectory(prefix="we-recovery-") as temporary:
        snapshot = Path(temporary) / "database.db"
        copied = 0
        with path.open("rb") as source, snapshot.open("xb") as destination:
            for block in iter(lambda: source.read(1024**2), b""):
                copied += len(block)
                if copied > MAX_DATABASE_BYTES:
                    raise ValueError("Copied database exceeds byte budget")
                destination.write(block)
        with closing(
            sqlite3.connect(snapshot.as_uri() + "?mode=ro", uri=True)
        ) as database:
            database.execute("PRAGMA query_only=ON")
            if database.execute("PRAGMA integrity_check").fetchone()[0] != "ok":
                raise ValueError("Invalid match database")
            images = database.execute(
                "SELECT image_id,name FROM images LIMIT ?", (MAX_FRAMES + 1,)
            ).fetchall()
            pairs = database.execute(
                "SELECT pair_id,rows,config FROM two_view_geometries LIMIT ?",
                (MAX_PAIRS + 1,),
            ).fetchall()
    if any(
        Path(str(path) + suffix).exists() or Path(str(path) + suffix).is_symlink()
        for suffix in ("-wal", "-shm", "-journal")
    ):
        raise ValueError("Match database acquired a SQLite sidecar during inspection")
    if len(images) > MAX_FRAMES or len(pairs) > MAX_PAIRS:
        raise ValueError("Match graph exceeds the selected-frame budget")
    return images, pairs


def components(names, edges, minimum):
    adjacency = {name: set() for name in names}
    for first, second, inliers, _ in edges:
        if inliers >= minimum:
            adjacency[first].add(second)
            adjacency[second].add(first)
    unseen = set(names)
    result = []
    while unseen:
        pending = [min(unseen)]
        unseen.remove(pending[0])
        group = []
        while pending:
            name = pending.pop()
            group.append(name)
            new = adjacency[name] & unseen
            unseen.difference_update(new)
            pending.extend(sorted(new))
        result.append(sorted(group))
    return sorted(result, key=lambda group: (-len(group), group))


def analyze_matches(images, pairs, selected, models, configurations, thresholds):
    if (
        not 3 <= len(selected) <= MAX_FRAMES
        or any(
            not isinstance(name, str) or not re.fullmatch(r"[0-9]{6}\.png", name)
            for name in selected
        )
        or len(set(selected)) != len(selected)
    ):
        raise ValueError("Invalid selected frame inventory")
    if not 1 <= len(models) <= 50:
        raise ValueError("Invalid model count")
    for minimum in thresholds.values():
        if type(minimum) is not int or not 1 <= minimum <= 1000000:
            raise ValueError("Invalid backend inlier threshold")
    names = {}
    for image_id, name in images:
        if (
            type(image_id) is not int
            or not 0 < image_id < PAIR_BASE
            or image_id in names
            or name not in selected
        ):
            raise ValueError("Invalid or duplicate match database image")
        names[image_id] = name
    if len(names) != len(selected) or set(names.values()) != set(selected):
        raise ValueError("Match database differs from the selected frame inventory")
    model_sets = []
    for model in models:
        frames = model["images"]
        if any(not isinstance(name, str) for name in frames) or len(frames) != len(
            set(frames)
        ):
            raise ValueError("Duplicate model image")
        model_sets.append(set(frames))
    if len({model["model"] for model in models}) != len(models):
        raise ValueError("Duplicate model identity")
    choice = select_candidate(model_sets, set(selected))
    edges, seen = [], set()
    counts = {}
    if len(pairs) > MAX_PAIRS:
        raise ValueError("Match pair budget exceeded")
    for pair_id, inliers, config in pairs:
        if (
            type(pair_id) is not int
            or pair_id in seen
            or pair_id <= 0
            or type(inliers) is not int
            or not 0 <= inliers <= 1000000
            or type(config) is not int
            or config not in configurations
        ):
            raise ValueError("Invalid or duplicate verified pair")
        first, second = divmod(pair_id, PAIR_BASE)
        if first not in names or second not in names or first >= second:
            raise ValueError("Invalid COLMAP pair identity")
        seen.add(pair_id)
        label = configurations[config]
        counts[label] = counts.get(label, 0) + 1
        if inliers:
            edges.append((names[first], names[second], inliers, label))
    required = (9 * len(selected) + 9) // 10
    candidates = []
    for model, registered in zip(models, model_sets):
        missing = []
        for name in sorted(set(selected) - registered):
            neighbors = []
            all_neighbors = 0
            for first, second, inliers, config in edges:
                if name not in (first, second):
                    continue
                all_neighbors += 1
                other = second if first == name else first
                if other in registered:
                    neighbors.append(
                        {"image": other, "inliers": inliers, "configuration": config}
                    )
            missing.append(
                {
                    "image": name,
                    "positive_inlier_neighbors": all_neighbors,
                    "registered_neighbors": sorted(
                        neighbors, key=lambda edge: (-edge["inliers"], edge["image"])
                    ),
                }
            )
        candidates.append(
            {
                "model": model["model"],
                "registered": len(registered),
                "images": sorted(registered),
                "registration_deficit": max(0, required - len(registered)),
                "missing": missing,
            }
        )
    return {
        "selected_frame_count": len(selected),
        "required_registered_frames": required,
        "selection_policy": "unique_strict_superset_v1",
        "selected_model": None if choice is None else models[choice]["model"],
        "candidates": candidates,
        "stored_pairs": len(pairs),
        "positive_inlier_pairs": len(edges),
        "configuration_counts": counts,
        "graphs": {
            label: {
                "minimum_pair_inliers": minimum,
                "components": components(selected, edges, minimum),
            }
            for label, minimum in {"positive_inliers": 1, **thresholds}.items()
        },
        "interpretation": "Pair counts include planar/panoramic configurations. "
        "They are not 2D-to-3D PnP inliers, triangulation/parallax evidence, "
        "or proof that a mapper can register these frames.",
    }
