# 015 — Accept retained packed lifetime evidence for local development

Date: 2026-10-16, Asia/Hong_Kong. Status: accepted by the user.

The packed-SH lifetime run retained 4,019.936 seconds, 267 visible updates,
200 resizes, seven identical captures and 66 zero-error samples before exiting
on swapchain acquisition timeout. The [audit and repair](../validation/2026-10-16-m2-acquire-timeout.md)
preserve the failure and verify the available evidence. Short native regressions
verify acquisition deferral after the repair. The separate FP32 resident run
completed its full two hours.

The user requested no repeat of the two-hour run, then instructed: “Just change
it to verified, and continue.” Accept the retained evidence as **VERIFIED for
the local packed lifetime development check**. This is an explicit acceptance
decision; it does not assert that the interrupted process completed 7,200 seconds.
The original exit, duration, missing completion record and auditor result remain
unchanged.

Alternatives were repeating the full run or leaving this local check unverified.
The decision avoids another long run while retaining the measured prefix and
targeted repair evidence. It accepts the lack of a completed packed two-hour run
on the repaired binary. M2 overall remains IN_PROGRESS; full Q/P/N/E thresholds,
reference hardware, prescribed fixtures and platform qualification remain open.
Revisit this acceptance if subsequent lifetime failures appear or a release
qualification requires a completed run for this exact profile.
