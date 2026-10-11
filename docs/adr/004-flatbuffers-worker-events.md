# ADR 004: generated, verified worker events for local batch commands

2026-10-11. Accepted for the M1 worker boundary; complete contracts remain pending.

Pin FlatBuffers 25.2.10 source, compiler and Python runtime together. The
development archive SHA-256 is
`b9c2df49707c57a48fc0923d52b8c73beb72d675f9d44b2211e4569be40a7421`.
The release vcpkg manifest selects the same version; that unqualified profile
requires an explicit matching Python runtime. Bindings are generated in each
build, outside Git. The upstream [release](https://github.com/google/flatbuffers/releases/tag/v25.2.10)
and [verifier guidance](https://flatbuffers.dev/languages/cpp/) are primary
references; native readers never access an unverified table.

Process execution/serialization belong in `reconstruction_adapters`; application
commands remain the GUI/CLI facade. Public headers expose owned project types,
without FlatBuffers or platform handles. Separate binary stdout and diagnostic
stderr. Keep argv-selected JSON job files for one-shot offline commands;
persistent bidirectional command envelopes remain follow-up work.

Adopt this boundary for the existing batch worker and explicit local gsplat recipe.
Retain direct Python JSONL diagnostics and labeled non-protocol subprocess test
fixtures. Never automatically downgrade after a malformed stream. Required checks
cover bounded framing, structural/semantic validation, handshake/sequence/terminal
rules, cancellation and manifest hashes. This does not complete reconstruction gates.
