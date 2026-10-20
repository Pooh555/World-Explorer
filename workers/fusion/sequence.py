"""Bounded owned scratch for one global fusion over native cache boundaries."""

from contextlib import contextmanager
from pathlib import Path

from geometry import check_cancel, source_consistency, world_points

__all__ = ["StagedSequence"]


class StagedSequence:
    """Own immutable scratch planes; mapped views must not escape their context.

    Stage the entire validated iterator before computing or publishing anything.
    Input arrays are never modified. Scratch, including failed attempts, belongs
    inside a newly reserved job output directory and is never reused implicitly.
    """

    def __init__(self, directory, recipe, cancelled=lambda: False):
        self.directory = Path(directory)
        self.recipe = recipe
        self.cancelled = cancelled
        self.records = []
        self.reserved_bytes = 0
        self.positive_samples = 0

    def stage(self, batches):
        import numpy as np

        self.directory.mkdir(parents=True, exist_ok=False)
        for views, _ in batches:
            for view in views:
                check_cancel(self.cancelled)
                if len(self.records) >= 120:
                    raise ValueError("Fusion sequence frame budget exceeded")
                planes = {
                    key: view[key]
                    for key in ("depth", "valid", "confidence", "support", "sigma")
                    if view[key] is not None
                }
                positive = int(((view["valid"] == 1) & (view["confidence"] > 0)).sum())
                # Reserve all planes, masks, NPY headers and three exact float64
                # percentile axes before writing. Extent scratch is never output.
                reserved = (
                    sum(array.nbytes + 512 for array in planes.values())
                    + view["valid"].size
                    + 512
                    + positive * 3 * 8
                )
                if self.reserved_bytes + reserved > self.recipe["max_scratch_bytes"]:
                    raise ValueError("Fusion sequence scratch budget exceeded")
                self.reserved_bytes += reserved
                self.positive_samples += positive
                prefix = self.directory / str(len(self.records))
                record = {
                    key: view[key] for key in ("name", "intrinsics", "cam_from_world")
                }
                record["prefix"] = prefix
                record["planes"] = tuple(planes)
                for key, array in planes.items():
                    check_cancel(self.cancelled)
                    with prefix.with_suffix("." + key + ".npy").open("xb") as stream:
                        np.save(stream, array, allow_pickle=False)
                self.records.append(record)
            # Drop both the decoded batch and its final view/plane aliases before
            # asking the native reader to decode the next bounded child cache.
            del views, view, planes, array
        if not self.records:
            raise ValueError("Empty fusion sequence")

    @contextmanager
    def open_view(self, index, *, mask=False):
        import numpy as np

        check_cancel(self.cancelled)
        record = self.records[index]
        view = {key: record[key] for key in ("name", "intrinsics", "cam_from_world")}
        mapped = []
        try:
            for key in record["planes"] + (("mask",) if mask else ()):
                plane = np.load(
                    record["prefix"].with_suffix("." + key + ".npy"),
                    mmap_mode="r",
                    allow_pickle=False,
                )
                mapped.append(plane)
                view[key] = plane
            view.setdefault("sigma", None)
            yield view
        finally:
            # np.memmap lacks a public close method; explicitly close owned maps
            # so repeated source scans cannot retain the whole sequence in RSS.
            for plane in mapped:
                plane._mmap.close()

    def pairs(self):
        for index in range(len(self.records)):
            with self.open_view(index, mask=True) as view:
                yield view, view["mask"]

    def extent(self):
        """Exact pooled linear percentiles, using three on-disk partition axes."""
        import numpy as np

        if not self.positive_samples:
            raise ValueError("No finite positive-weight depth extent")
        axes = []
        try:
            for axis in range(3):
                axes.append(
                    np.memmap(
                        self.directory / f"extent-{axis}.f64",
                        mode="w+",
                        dtype=np.float64,
                        shape=(self.positive_samples,),
                    )
                )
            offset = 0
            for index in range(len(self.records)):
                with self.open_view(index) as view:
                    y, x = np.nonzero((view["valid"] == 1) & (view["confidence"] > 0))
                    for start in range(0, len(x), self.recipe["chunk_voxels"]):
                        check_cancel(self.cancelled)
                        xx = x[start : start + self.recipe["chunk_voxels"]]
                        yy = y[start : start + self.recipe["chunk_voxels"]]
                        points = world_points(view, xx, yy, view["depth"][yy, xx])
                        if not np.isfinite(points).all():
                            raise ValueError("No finite positive-weight depth extent")
                        for axis, values in enumerate(axes):
                            values[offset : offset + len(points)] = points[:, axis]
                        offset += len(points)
            if offset != self.positive_samples:
                raise ValueError("Fusion extent sample count changed")
            bounds = np.empty((2, 3), dtype=np.float64)
            for axis, values in enumerate(axes):
                check_cancel(self.cancelled)
                bounds[:, axis] = np.percentile(
                    values, [1, 99], method="linear", overwrite_input=True
                )
            extent = float(np.max(bounds[1] - bounds[0]))
            if not 1e-12 < extent < 1e12:
                raise ValueError("Degenerate or unsupported arbitrary-unit extent")
            return extent, bounds.tolist(), self.positive_samples
        finally:
            for values in axes:
                values._mmap.close()

    def filter(self, progress=lambda row: None):
        """All sources participate, including occluders in later native caches."""
        import numpy as np

        summaries = []
        for index, record in enumerate(self.records):
            with self.open_view(index) as view:
                y, x = np.nonzero((view["valid"] == 1) & (view["confidence"] > 0))
                support = np.zeros(len(x), dtype=np.uint8)
                blocked = np.zeros(len(x), dtype=bool)
                for source_index in range(len(self.records)):
                    if source_index == index:
                        continue
                    with self.open_view(source_index) as source:
                        for start in range(0, len(x), self.recipe["chunk_voxels"]):
                            check_cancel(self.cancelled)
                            end = start + self.recipe["chunk_voxels"]
                            xx, yy = x[start:end], y[start:end]
                            points = world_points(view, xx, yy, view["depth"][yy, xx])
                            agree, occluded = source_consistency(
                                view, xx, yy, points, source, self.recipe
                            )
                            support[start:end] += agree.astype(np.uint8)
                            blocked[start:end] |= occluded
                enough = support >= self.recipe["minimum_consistent_sources"]
                keep = enough & ~blocked
                mask = np.zeros(view["depth"].shape, dtype=bool)
                mask[y, x] = keep
                with record["prefix"].with_suffix(".mask.npy").open("xb") as stream:
                    np.save(stream, mask, allow_pickle=False)
                valid = int((view["valid"] == 1).sum())
                row = {
                    "name": view["name"],
                    "pixels": int(mask.size),
                    "valid": valid,
                    "unknown": int(mask.size) - valid,
                    "zero_confidence_valid": valid - len(x),
                    "enough_consistent_sources": int(enough.sum()),
                    "known_occlusion": int(blocked.sum()),
                    "accepted": int(keep.sum()),
                    "rejected_positive_weight": len(x) - int(keep.sum()),
                    "uncertainty": (
                        "UNAVAILABLE"
                        if view["sigma"] is None
                        else "CAMERA_UNIT_STANDARD_DEVIATION"
                    ),
                }
                summaries.append(row)
                progress(row)
        return summaries
