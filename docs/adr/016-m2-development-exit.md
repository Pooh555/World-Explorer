# 016 — M2 development exit on the available Linux/NVIDIA host

Date: 2026-10-16, Asia/Hong_Kong. Status: accepted by the user.

The user requested an M2 exit achievable on the current development machine,
with other native-machine tasks deferred until at least M4. The available
Linux/Hyprland RTX 4060 Laptop GPU host is neither H_V nor another reference
profile. Requiring those profiles now prevents renderer development closure
without improving the accuracy of local evidence.

Adopt the six-item [plan §8.2 checklist](../technical_plan.md#82-m2-development-exit-and-deferred-hardware-qualification).
It covers real and synthetic reference parity, revision/admission semantics,
bounded memory and updates, local packing and input observations, recovery and
accepted lifetime evidence. Existing scoped results remain usable when relevant
implementation and fixture identities still apply. ADR 015's packed lifetime
acceptance stands; another two-hour run is not required.

Move other native-machine renderer qualification to M4 or later: reference-device
performance/memory/update measurements, physical input/display and recovery,
Windows/macOS/AMD/MoltenVK and per-device packing qualification. M4 owns access
blockers; M6 still requires complete prescribed evidence for claimed release
profiles. Numerical targets, benchmark scope, corpus rules and M3 reconstruction
requirements remain unchanged.

Alternatives were retaining an externally blocked M2 exit or dropping platform
requirements. The chosen development exit permits M3 while keeping those
requirements visible. It accepts the risk that later hardware qualification may
require renderer changes. M2 remains IN_PROGRESS until its local checklist is
reviewed; this decision does not certify a full Q/P/N/E gate. Revisit when local
failures invalidate evidence or M4 devices expose incompatible behavior.
