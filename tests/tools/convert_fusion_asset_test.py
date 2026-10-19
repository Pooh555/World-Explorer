"""Synthetic conversion boundary tests; run in the existing NumPy environment."""

import importlib.util
import io
from pathlib import Path
import struct
import unittest
import warnings
import zipfile

import numpy as np

SPEC = importlib.util.spec_from_file_location(
    "convert_fusion_asset",
    Path(__file__).resolve().parents[2] / "tools/convert_fusion_asset.py",
)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def archive(entries):
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, "w", compression=zipfile.ZIP_DEFLATED) as output:
        for name, encoded in entries:
            output.writestr(name, encoded)
    return stream.getvalue()


def npy(value):
    stream = io.BytesIO()
    np.save(stream, value, allow_pickle=False)
    return stream.getvalue()


class ConversionTests(unittest.TestCase):
    def test_exact_integer_widths_and_float_bytes(self):
        for dtype in ("<i4", "<i8"):
            value = np.array([[0, 1, 2]], dtype=dtype)
            result = MODULE.arrays(
                archive([("triangles.npy", npy(value))]),
                {"triangles": (("<i4", "<i8"), (1, 3))},
            )
            self.assertEqual(result["triangles"].tobytes(), value.tobytes())
        value = np.array([0.125, -0.5], dtype="<f4")
        result = MODULE.arrays(
            archive([("tsdf.npy", npy(value))]), {"tsdf": ("<f4", (2,))}
        )
        self.assertEqual(result["tsdf"].tobytes(), struct.pack("<ff", 0.125, -0.5))

    def test_duplicate_and_extra_zip_names_reject(self):
        encoded = npy(np.array([1], dtype="<i4"))
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", UserWarning)
            duplicate = archive([("a.npy", encoded), ("a.npy", encoded)])
        for data in (
            duplicate,
            archive([("../a.npy", encoded)]),
            archive([("a.npy", encoded), ("extra.npy", encoded)]),
        ):
            with self.assertRaises(ValueError):
                MODULE.arrays(data, {"a": ("<i4", (1,))})

    def test_declared_huge_shape_rejects_before_array_loading(self):
        stream = io.BytesIO()
        np.lib.format.write_array_header_1_0(
            stream, {"descr": "<f4", "fortran_order": False, "shape": (2**62,)}
        )
        with self.assertRaises(ValueError):
            MODULE.arrays(archive([("a.npy", stream.getvalue())]), {"a": ("<f4", (1,))})

    def test_dtype_shape_order_and_payload_length_reject(self):
        values = [
            np.array([1], dtype="<f8"),
            np.array([[1]], dtype="<f4"),
            np.asfortranarray(np.ones((2, 2), dtype="<f4")),
        ]
        for value in values:
            with self.assertRaises(ValueError):
                MODULE.arrays(archive([("a.npy", npy(value))]), {"a": ("<f4", (1,))})
        encoded = npy(np.ones(1, dtype="<f4"))
        for changed in (encoded[:-1], encoded + b"extra"):
            with self.assertRaises(ValueError):
                MODULE.arrays(archive([("a.npy", changed)]), {"a": ("<f4", (1,))})

    def test_object_array_rejects_without_pickle(self):
        stream = io.BytesIO()
        np.save(stream, np.array([{"untrusted": True}], dtype=object))
        with self.assertRaises(ValueError):
            MODULE.arrays(archive([("a.npy", stream.getvalue())]), {"a": ("<f4", (1,))})


if __name__ == "__main__":
    unittest.main()
