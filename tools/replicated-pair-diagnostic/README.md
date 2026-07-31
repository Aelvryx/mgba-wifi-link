# Replicated-pair Android diagnostic

This isolated configuration runs the continuous GBA MULTI fixture through two
local mGBA instances and the existing local lockstep coordinator. It exercises
the candidate protocol-v2 execution architecture without any network traffic.

The diagnostic core option is disabled by default and labelled as requiring a
restart. The supplied core-options file enables it only for this isolated run.
Save files, states, logs, and core options all use the dedicated
`mgba-replicated-pair-diagnostic` external-storage directory; the normal
RetroArch configuration and user save directories are not read or modified.

The core emits a structured summary every 60 frames. It includes the frame and
transfer counts for both logical GBAs, local lockstep waits, paired-core runtime,
wall-clock rate, and serial words per emulated second. Android qualification
also samples process CPU, peak resident memory, thermal zones, thermal-service
status, and CPU frequencies from ADB.
