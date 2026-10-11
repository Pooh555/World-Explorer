# ADR 002: official COLMAP bindings for the sparse worker

2026-10-11. The host has no COLMAP executable. Official pycolmap 4.2.1 wheels
provide the same feature extraction, sequential matching, incremental mapping
and undistortion APIs without globally installing a GUI package or compiler graph.
Use that exact version in an isolated Python 3.12 environment. Poses remain real
COLMAP poses; this is an adapter choice, not a replacement algorithm or relaxed
registration gate. The installed API signatures were inspected before use.

CPU poses are the development path. CUDA poses, dense MVS, the wheel's full
transitive license inventory and H_R performance remain unqualified.

References: [official Python API](https://colmap.github.io/pycolmap/pycolmap.html),
[official package](https://pypi.org/project/pycolmap/4.2.1/).
