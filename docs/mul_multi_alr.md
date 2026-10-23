# Multi-ALR protocol and implementation

The user-reported two-ALR10ms hybrid_static experiment (approximately5–7cm
single-sample range-difference RMSE) is the regression baseline, not new hardware
validation by this implementation. No hardware operation is authorized here.

## Implementation plan and compatibility

1. Shared compact ordered schedule, validation/fingerprint and persistent codecs.
2. RF-OFF configuration on ALR/STM32, with read-only legacy migration.
3. Multi-profile ALR delayed-TX scheduling and serialized reference/follower TWR.
4. Fixed-size STM32 per-follower reconstruction and partial cycle results.
5. Fixed-length vector telemetry, bridge/collector support and deterministic replay.
6. Regression/sanitizer/build/memory review, deployment instructions and handoff.

Keep the current N=2 radio/math/telemetry path as protocol profile1. Profile2 is an
explicit generalized path for2..8 ALRs. Configurations without a multi-schedule
record migrate to profile1, preserving their roster, slot, mode and calibration.
New multi settings require RF OFF on every participant; received packets never
rewrite persistent configuration. Invalid/incompatible configurations fail closed.

## Schedule and synchronization decisions

Roster position0 is the logical reference; all other positions are followers.
IDs are arbitrary canonical UWB address suffixes, never sequential robot numbers.
The schedule carries a protocol profile, roster length, slot spacing, cycle period
and operator-controlled revision. A canonical byte encoding and CRC32 fingerprint
bind every schedule field and ordered identity. N is derived from roster length.
The initial hardware-supported cycle is1000ms. Unsupported periods fail validation
rather than pretending that existing1Hz clock estimators support other cadences.

Data offsets are index*slot_ms in the follower's own hardware clock following
reference reception. Guarded delayed-TX/antenna correction and skip-late behavior
remain mandatory. All data must finish before synchronization preparation.

For profile2, two nonoverlapping TWR windows are reserved per cycle: poll650ms /
end800ms, then poll800ms / end950ms. Each five-message exchange uses the existing
20ms replies; only the selected reference/follower pair participates. Up to seven
followers are serviced round-robin over four cycles, keeping a healthy propagation
update within the existing6000ms policy. The reference reuses one active exchange
and does not allocate seven radio contexts. Every follower retains its own current
reference clock and propagation estimate. Missed service affects that follower;
no simultaneous N-way TWR is permitted. Profile1 retains its original every-fifth-
cycle exchange and650..800ms timing unchanged.

## Packet and receiver direction

Profile2 uses distinct versioned radio types. It carries cycle, slot index and a
schedule fingerprint; source identity stays in the existing MAC header. A compact
schedule header extends the existing reference/relation/TWR payloads without
changing timestamp units or applying propagation correction twice. All frames
must remain below127bytes including the radio FCS. Exact layouts are specified
with the production codec when introduced; do not deploy an unfinished profile.

STM32 maintains one receiver-rate estimate from reference cadence and independently
validated state/results for each follower. A cycle closes after all scheduled data
slots plus bounded guard, or before a newer reference cycle replaces it. Missing,
stale, mismatched or invalid followers receive explicit unavailable entries; valid
siblings remain valid. No dynamic allocation is used. Range-difference sign stays
follower distance minus reference distance. Ordinary hybrid never claims a valid
metric measurement without an independently justified receiver rate.

A new UWB3 vector type will retain the152-byte binary /310-byte UART bound,
including roster, fingerprint, cycle and independent status/value entries. Legacy
type7 is never reinterpreted. Raw captures and optional metadata remain available.

## Hardware and resource constraints

LPS has128KB flash and16KB RAM with little baseline headroom. Inspect linked LOAD
segments and reserved stacks for every firmware milestone. Do not reduce safety
queues/stacks to make features fit. The operational multi receiver is STM32; the
legacy passive DW1000 RLR mode may remain limited to profile1 if resources demand
it, with an explicit configuration rejection rather than silent truncation.

Physical deployment is deferred until software implementation/testing is complete.
The final handoff must distinguish implemented, simulated and hardware-unverified
behavior and include exact reproducible build/configuration/acceptance commands.
