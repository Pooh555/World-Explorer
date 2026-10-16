# 017 — Explicit depth interchange before dense geometry integration

Date: 2026-10-17, Asia/Hong_Kong. Status: implemented development decision;
no milestone/gate scope change.

M3 needs a project-owned depth boundary before an isolated MVS worker can publish
geometry. The current pose package exposes dense APIs but reports no CUDA
support; API presence is not a dense reconstruction result. Backend-native depth
files alone cannot identify calibration, pose revisions, units or unknown pixels.

Use the [V1 depth interchange](../protocols/depth.md): fixed little-endian planes,
explicit axial camera-Z convention, a separate validity mask, relative confidence,
actual observation support and optional standard deviation. Bind both original
and calibrated image hashes, recipe and exact canonical pose dependency. Reject
stale or untracked/invalidated pose inputs. Match the current canonical pose
contract's arbitrary camera units; do not accept measured-scale claims before
independent measurement provenance is implemented.

An explicit uncompressed interchange keeps the first C++/Python boundary testable
without another dependency. It is an input to later scene compilation, not a
replacement for the plan's compressed depth/normal/confidence assets. Zstd,
chunk indexing, normal/confidence calibration and dense-worker qualification remain
open M3 work. Readers reject unsupported versions rather than guessing layouts.

Alternatives were immediately exposing COLMAP buffers to the application or
building fusion against depth values with implicit units/validity. The chosen
boundary adds metadata and validation work but permits backend-independent
admission and cancellation tests. Reuse the existing immutable-file primitives
in `core`; keep their moved implementation in the package/update coverage scope.

Revisit when real dense input preparation or measured-scale integration requires
additional domains/provenance, and version incompatible semantics explicitly.
Synthetic planes and successful format inspection never qualify Q5/N2 or make a
scene geometry-ready. Full reconstruction, quality and release obligations stand.
