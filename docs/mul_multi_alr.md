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

## Persistence checkpoint

The shared32-byte schedule is an additive EEPROM TLV on LPS; absence derives
profile1 from the unchanged saved reference/follower/slot. Its own CRC supplements
the existing EEPROM checksum. Local ALR identity must be present in the roster.
The legacy EEPROM update remains write/readback verified and fail-closed, not a
new power-fail journal. Profile2 RF enable is deliberately blocked until the
corresponding radio adapter milestone is implemented; stored rosters can be
prepared/rebooted safely in the meantime. `:cfg roster show` and
`:cfg roster set 0x84 0x85 0x86` require acknowledged RF OFF for edits.

LPS MUL/application compilation uses -Os to retain all stock engines and the
existing safety queues/stacks inside128KB/16KB. A whole-program LTO experiment
was rejected: it increased flash and exposed legacy naked-assembly branch limits.
Stock engines and hardware drivers retain their previous compiler optimization.
The existing hardware delayed-TX guards remain authoritative; software arithmetic
regressions do not establish physical response/timing performance.

## Profile2 radio encoding

All sizes exclude the radio's two-byte FCS. Reference frames are40bytes, follower
relation frames80bytes, TWR frames90bytes; all fit the127-byte PHY limit.

- Broadcast bytes0..33 retain the legacy MAC/MUL/sequence/TX timestamp layout.
  Type at24 is0x30(reference) or0x31(follower); sequence is the reference cycle
  for every slot. Byte34 is protocol version2;35 is the roster slot;36..39 is the
  little-endian canonical schedule fingerprint. Follower relation bytes34..73
  from the existing codec move to40..79, with unchanged units/meaning.
- TWR retains84bytes of existing payload, with types0x18..0x1c mapping to the
  legacy0x10..0x14 stages. Byte84 is version2,85 is selected follower slot,
  86..89 is the same fingerprint. The deterministic cycle/window owner and full
  MAC endpoints are validated before accepting the exchange.
- A source must equal the configured full identity at its advertised slot.
  Available relation referenceSequence must equal the packet cycle. Hash/version,
  source/slot/length mismatch or unavailable synchronization cannot silently
  create a valid TDoA. Legacy codecs reject these distinct types/lengths.

`mul_multi_protocol.c` performs only bounded encoding/decoding; hardware scheduling
and receiver reconstruction are separate integration steps. Production clock and
relation codecs are reused rather than duplicating their arithmetic.

## Shared multi receiver

`mul_multi_receiver` uses one reference-rate estimator, eight bounded source slots
and seven independently validated relations/results. It associates explicit cycle
IDs and slot identities; it does not instantiate seven legacy radio receivers.
The original localization/downlink arithmetic is reused with an explicit expected
slot offset; legacy APIs retain their original spacing/freshness gates.

A vector closes after last_data_offset+20ms, or before advancing to a new cycle.
Validity is frozen at each follower's capture/evaluation time: waiting for a later
slot does not turn an earlier valid measurement stale. Missing/invalid siblings
retain separate statuses and INT32_MIN sentinels. Reference loss invalidates only
cycles without a reference; follower epoch changes do not reset sibling clocks.
Out-of-order packets within capture-age bounds can complete a partial cycle;
duplicates and old/closed-cycle packets cannot overwrite accepted results.
A reference restart rebases only after liveness expiry and discards receiver rate.

The deterministic native tests use the production codec, receiver, clock and
localization code with independent clock skews, signed/zero geometry, missing
reference/followers, invalid relations, reordering, bad capture age, schedule
mismatch, reference recovery and40-bit/32-bit rollover at N=2/3/4/8. This is a
software simulation; ALR driver/STM32 telemetry integration is still required.

### LPS multi radio adapter

Mode 5 now selects profile 2 from a persisted roster of three through eight IDs.
The reference uses one reusable TWR exchange context; followers retain their own
clock and propagation correction. Data TX is hardware delayed at `index*slot_ms`
after each freshly received reference, and carries that reference's cycle.
Two maintenance windows prepare at 630/780 ms and poll at 650/800 ms. A failed
first exchange is canceled before preparing the second. Missing followers are
skipped, not replaced by a different unscheduled owner. No late immediate TX is
allowed. Broadcast preparation at 980 ms takes priority over all ranging.

Profile 1 retains its original frames and fifth-cycle TWR cadence. LPS passive
mode 6 does not implement profile 2 and refuses to enable it; the generalized
passive receiver is intended for STM32. Legacy configuration and observations
continue using their original paths. Application objects use `-Os` to fit the
128 KB flash; HAL, RTOS and libdw1000 retain their previous optimization. Existing
mode-exclusive state is reused; stack and queue reservations are unchanged.
Native tests exercise every follower slot for N=2/3/4/8, both complete TWR
windows with independent clocks, cycle wrap, and first-window failure isolation.
These are software checks, not hardware timing or accuracy validation.

### Multi telemetry wire additions

UWB3 type 8 is a coherent cycle vector; type 9 is optional raw multi-frame
capture. Types 1–7 keep their existing interpretation. Both additions retain the
152-byte binary record, CRC16 at150, and309-character text line. Common header
bytes0..39 keep their existing meaning; uptime is receiver observation time.

Type8: cycle40..43, schedule CRC44..47, revision48..51, reference RX40-bit52..56,
slot_ms57..58, cycle_ms59..60, count61, seenMask62 and validMask63 (bit index is
roster index), roster IDs64..71. Up to seven10-byte follower entries start72:
int32 dd_mm, int32 dt_dtu, packed status0..2/clock bit3/rate bits4..5, reason byte9.
Unused entries are zero. Invalid values use INT32_MIN and decode to JSON null.
Bytes142..145 are dropped count,146 queue depth,147 high-water (both saturate255),
148 stationary policy,149 zero. Reasons follow `mulMultiReason_t`0..9.

Type9: radio frame40..129 (zero padded to90), local RX130..134, capture age135..136,
length137 (40/80/90), spacing138..139, schedule CRC140..143, revision144..147,
stationary policy148, zero149. It is diagnostic and lower priority than type8.
