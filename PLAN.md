# MUL development roadmap

## Objective

Infrastructure-free relative localization for teams of mobile robots using
downlink UWB TDoA, mobile anchor-like robots (ALRs), receiver/localizing robots
(RLRs), relative clock synchronization, and occasional TWR measurements for
online error calibration.

This is the single overview of current development state.
See [AGENTS.md](AGENTS.md) for operating rules and
[the architecture](docs/mul_architecture.md) for durable technical decisions.
Source code remains authoritative for what is actually implemented.

## Status meanings

- **Not started:** planned work without a completed implementation.
- **Implemented/software tested:** relevant builds and automated tests pass;
  physical acceptance is still outstanding.
- **Hardware validated:** physical measurements support the milestone's criteria;
  identify whether validation was performed by the user or by the implementing agent.
- Completing implementation alone must not promote a milestone to hardware validated.

## Completed baseline

| Milestone | Current status | Established capability |
|---|---|---|
| Stage 0 — Custom MUL operating modes | Complete | Modes 5/6, USB selection, EEPROM persistence |
| Stage 1 — ALR-to-RLR UWB communication | Complete | Periodic broadcast, passive reception, source/sequence diagnostics |
| Stage 2 — Hardware-delayed TX and accurate TX/RX timestamps | Hardware validated (user-reported) | Embedded hardware TX stamp and local RX stamp; physical antenna calibration pending |
| Stage 3A.1 — Multi-ALR protocol and reference acquisition | Hardware validated (user-reported) | Reference reception, loss/reacquisition, and observed wraparound |
| Stage 3A.2 — Hardware-timed TDMA | Hardware validated (user-reported) | Both ALRs TX, passive RLR RX, approximately 500.04 ms separation, TX timestamp equality |
| Stage 3B.1 — Relative clock skew | Hardware validated (user-reported) | Stable negative skew, loss reset/reconvergence, preserved TX equality and RLR reception |
| Stage 3B.2a — Arrival-aligned affine mapping | Hardware validated (user-reported) | Mapping-state behavior and loss/restart recovery; conversion accuracy not independently measured |
| Stage 3B.2b.1 — External propagation-delay correction | Software complete | Separate corrected APIs, delay contract, epoch/age checks and explicit unknown uncertainty |

Stages 0–2, 3A.1, 3A.2, 3B.1, and 3B.2a are implemented and software tested.
Hardware-validation status follows the user's current physical-test reports;
it is not a claim of hardware testing by Codex for this milestone.
The prior timestamp-discrepancy investigation does not establish a root cause;
retain that distinction when interpreting validation and diagnostics.

## Hardware checkpoint — Stage 3B.2b.3: Antenna-delay bias correction

Status: **Functional correction hardware validated (user-reported); accuracy unresolved.**
The user validated Stage 3B.2b.2 exchange cadence, matching results, TX delta=0,
RLR reception and SYNC state behavior, but reported approximately 33300 DTU /
156.2 m at close range. Ranging accuracy therefore failed acceptance.
Stage 3B.2b.3 fixes the proven zero-register compensation omission, initializes
MUL ALR hardware antenna delays and detects configuration overwrites. Exact
provenance and remaining calibration limits are in the architecture document.
Objective: opportunistic asymmetric double-sided ranging initiated by `0x84`,
with a confirmed reference-unit one-way estimate supplied to the follower's
existing corrected mapping. Arrival MAP remains UNCALIBRATED; corrected SYNC
availability is separate from fully bounded accuracy and never changes TDMA.

Current configuration, all using Bitcraze DW1000 LPS nodes:

| Node | Address | Current role |
|---|---|---|
| ALR 1 | `0x84` | Reference transmitter, approximately 1 Hz |
| ALR 2 | `0x85` | Reference-following transmitter, configured slot (default 500 ms) |
| RLR | `0x86` | Passive receiver of both sources |

Software acceptance covers complete two-radio exchanges with independent clock
rates, six-timestamp arithmetic, wraparound, IDs, packet validation, missing/error
responses, verified TX, loss/epoch cancellation, broadcast priority, RLR passivity,
and real-driver register transitions. Existing clock/correction and TDMA tests pass.

Stage 3B.2a's supplied physical results validate WARMUP/VALID and loss/restart
state behavior with stable skew and preserved TX/RLR operation, not independent
conversion accuracy. The interruption and USB disconnect were intentional.
Physical acceptance: both ALRs/RLR retain approximately 1 Hz broadcasts/reception,
500 ms RLR-clock slot separation, and exact broadcast TX timestamp equality.
TWR should succeed near every five seconds without slot loss or lockups; measured
distances must be assessed at known separations, with calibration uncertainty explicit.
After Stage 3C, the user reports corrected TWR mean approximately 0.977 m at
approximately 1 m. At approximately 5 m the six corrected ranges were 4.955,
4.973, 4.959, 5.006, 4.945 and 4.936 m (mean approximately 4.962 m); all six
shown exchanges succeeded. Eleven consecutive RLR pairs associated independent
counters differing by two, without gaps/duplicates in the excerpt. Arrival slots
were 499997–499998 us; wraparound and TX equality remained functional. These are
user-reported excerpts, not bounded accuracy evidence. The user elects to proceed
without the earlier additional 20-sample and deliberate restart experiments.

## Pending milestones

### Stage 3C — TDoA-ready multi-ALR observation pipeline

Status: **Implemented/software tested; functional hardware evidence (user-reported excerpts).**
Objective: associate observations by source and transmission,
retaining clock domain, correction model, validity, and uncertainty.
Acceptance: tests reject missing/stale/inconsistent associations; hardware
observations from multiple ALRs can be paired without mixing clock domains.

### Stage 3D — Distributed clock relation and downlink TDoA

Status: **Implemented/software tested; functional hardware evidence, accuracy unresolved.**
Versioned follower metadata transports the existing propagation-corrected model.
RLR association binds the current model to both transmissions. Default firmware
exposes clock-relation availability but TDoA remains UNAVAILABLE without a justified
RLR/reference rate. Explicit stationary experimental opt-in enables cadence-rate
conversion and signed raw/corrected TDoA; physical uncertainty remains UNKNOWN.
No Stage 4 online calibration or Stage 5 position solving is implemented.

### Stage 4 — Occasional TWR and online measurement-error calibration

Status: **Not started.** Objective: obtain occasional TWR measurements to calibrate
an online error model without replacing the downlink observation flow.
Acceptance: exchange and failure tests pass; physical calibration measurements
support the model and quantify their effect on broadcast operation.

### Stage 5 — Downlink TDoA localization with uncertain mobile ALR poses

Status: **Not started.** Objective: estimate relative RLR position while accounting
for ALR motion, uncertain poses, and timing/measurement uncertainty.
Acceptance: simulated geometry/motion tests expose observability limits; physical
relative-position estimates are compared against an independent reference.

### Stage 6 — Probabilistic localization and experimental evaluation

Status: **Not started.** Objective: integrate probabilistic measurement conditions,
pose uncertainty, and LOS information into localization and evaluate performance.
Acceptance: controlled software evaluations and repeatable robot experiments
compare accuracy, uncertainty consistency, robustness, and relevant baselines.

Update milestone status in place as evidence changes; Git preserves earlier states.

## Stage 3D.1 — Short-slot hardware evidence

Status: **Software complete; functional hardware validation user-reported at 10 ms.**
With ALRs approximately 2 m apart, the user reports midpoint mean +0.07 m,
0.5 m from reference +0.96 m (expected +1 m), and 1.5 m from reference -1.20 m
(expected -1 m), short-term SD 3–7 cm. TWR remains approximately five seconds;
only one in five TDoA observations was available under the one-second policy.
These results supersede the earlier 500 ms midpoint -1.731 m example as the
current operating configuration. They do not establish bounded accuracy or the
cause of the spacing sensitivity. Detailed calibration awaits efficient MCS data.

## Stage 3E — Continuous observations

Status: **CP1–CP3 complete; stationary functional hardware validation user-reported, accuracy unresolved.**
Default retains strict 1000 ms/v1 policy. Explicit `MUL_PROP_MAX_AGE_MS=6000`
selects same-length 74-byte v2 with original TWR age, numeric estimate and state;
all three nodes must use the same policy. Reference 34/TWR 84 bytes unchanged.
Affine horizon stays 2000 ms and is reanchored every reference; no clock/TDoA
or DS-TWR equation changes. Confirmed measurement and attempted exchange are
separate. Timeout/late/missed refresh retains original data only until expiry;
malformed active exchanges/results, antenna/TX verification faults, clock epoch
changes and loss invalidate. FRESH denotes the measurement cycle/first following
reference; later valid cycles are REUSED. Original POLL capture time is committed
only after RESULT and never refreshed by a broadcast or failed attempt.

Checkpoints:
- CP1: `db6e029` pushed.
- CP2: `0257ba0` pushed. Caller-owned 88-byte localization
  record, deterministic outward-rounded motion term (-1 UNKNOWN, 0 stationary,
  positive mm/s), compact OBS output replacing redundant paired RX/PAIR/TDOA.
- CP3: the commit containing this section; end-to-end consecutive 1 Hz pipeline,
  missing refresh/expiry, bounded separation changes, full sanitizer/config/build
  matrix, RAM/stack review and final architecture/hardware instructions complete.

## Hardware checkpoint — Stage 3E

Build/flash commands, OBS schema and MCS-alignment limitations are in
[the architecture](docs/mul_architecture.md#stage-3e-build-and-hardware-procedure).
The preferred stationary build adds `MUL_PROP_MAX_AGE_MS=6000` and explicit
`MUL_RELATIVE_SPEED_MM_S=0` to stationary-rate/10 ms flags. Default remains
strict 1000 ms, legacy v1 and rate unavailable. Continuous v2 stays 74 bytes;
all three nodes must use the identical image. Codex did not flash physical nodes.
After rate warmup and confirmed TWR, verify approximately 1 Hz OBS availability:
FRESH then four REUSED cycles, original delay age increasing, reset only after
successful TWR. Observe multiple cycles and >40 s for wrap, delta=0 and no gaps
or starvation. A missed refresh retains only until original six-second expiry;
reset/loss requires new clock warmup and TWR. Detailed bias/MCS characterization,
Stage4 online calibration and Stage5 localization remain future tasks.

## Stage 3F — Cross-radio PHY experiment

**Stage 3F packet-interoperability gate accepted from user A/B logs (2026-10-20).**
Stage 3E stationary ~1 Hz TDoA is hardware validated (user-reported central
handoff); this is not cross-radio validation or bounded physical accuracy.
Canonical evidence matrix, packet v1 and exact A/B/reliability/restore commands:
[PHY experiment](docs/phy_interop.md). No central-document/ESP32/dsPIC edits.

CP0/CP1 `25bbc97` and CP2 `3f72be3` pushed to origin/master. E-puck CP3
`f7f3e9f` and final `75a90e630de6bfb24bdcd23f2e25d40b07a52fd1`
pushed origin/main.
CP4–CP6 software checks and operator guide are complete in this final commit.
Opt-in0/off,1/TX,2/RX uses ch5/PRF64/code9/preamble128/IEEE4a-SFD/6.8M/stdPHR,
no STS. Default ch2 and all MUL modes/packets/equations remain unchanged. Codec
is a small diagnostic copy, not the Stage 3G shared-core extraction.

## Stage 3F software validation checkpoint

No required software work remains. LPS193 host tests pass default and Stage 3E
continuous10ms;176 MUL ASan/UBSan cases pass plus two diagnostic real-driver cases
for each role. Both codecs independently pass3 golden vectors,768 single-bit
corruptions and wrong-length checks; headers/vectors match. Three host log-analysis
tests pass wrap/loss/reset/direction/minimum checks. STM32 codec/TX/RX and six
activation/init-failure/IRQ combinations pass ASan/UBSan;15 console and 9 ESP
safety/streaming regression scenarios pass. All user pre-existing e-puck edits
retain original checksums and are excluded from commits. Clean default,
Stage 3E and diagnostic TX/RX LPS builds pass; default/diagnostic and exported
committed-tree STM32 builds pass (including legacy UWB enabled).
LPS default text123024/data1072/bss15292 is unchanged (20 bytes spare);
experimental data1080/bss15292 (12 spare), scratch128 unchanged, radio callback
frame120 bytes. STM32 working-tree Release RAM28568 default,28992 RX; diagnostic
service frame280. Compiler estimates are not stack high-water or hard deadlines.
No hardware flashed. The central document remains unchanged and needs an
owner update to Stage 3F packet gate accepted with reliability/timing limitations.

**Hardware review:** all four complete user captures were inspected. A has85
valid receptions/89 receiver-span positions,4 gaps,103 errors,1634 frame-wait
timeouts, CRC0 and PHR/RS8. B has113/137 positions,24 gaps; last STAT before the
final frame reports56 aggregate errors and2611 timeouts. TX demonstrates >=100
completions per direction. Repeated startup prefixes, missing text and unequal
windows preclude a synchronized PDR or reproducible asymmetry claim; do not sum
cumulative counters across capture blocks. Full provenance/accounting and UART
findings are in [the experiment report](docs/phy_interop.md#hardware-results-reviewed-2026-10-20).

**Next milestone:** Stage 3G's bidirectional frame gate is open; no Stage 3G work
was begun during this review. Preserve the validated diagnostic profile and
normal firmware. Carry forward an intact, overlapping >=100-packet fixed-geometry
capture per direction and measured UART/IRQ latency diagnosis before claiming
reliable combined control-loop operation. A records six UART error callbacks,
27 dropped/flushed bytes and console queue loss; B records none. SPI interrupt
masking is a concrete latency risk, not a proven cause. Mixed-radio timestamp,
ranging, calibration and localization correctness remain unvalidated.


## Stage 3G — Portable core and opt-in MUL diagnostic

**Software/build validation complete; hardware validation PENDING. Do not begin Stage 3H.**
CP0/CP1: architecture audit complete; canonical core lives in portable/mul_core.
STM32 deterministic vendoring pins a published LPS commit with offline file-hash
verification at configure/build. No third remote, new submodule or local-only
build dependency. Existing user modifications remain unchanged and uncommitted.

CP2: existing mul_common codec/identity/sequence/40-bit helpers extracted without
behavior change. New strict44-byte type0x70/v1 diagnostic carries explicit raw
clock metadata and the previous completed TX marker; no Stage 3E packet, math,
correction lifecycle or observation fields changed. Core owns no hardware/heap.
CP3/CP4: opt-in MUL_DIAG_ROLE0/1/2 adapters implemented on both radios using the
proven Stage 3F PHY; defaults/EEPROM/modes/Stage 3E/Stage 3F preserved. Radio APIs,
SPI/IRQ, capture/scheduling/rearm, transport and storage remain platform-owned.

Implementation-compatible SHAs: LPS/canonical
`1b807c5c7507d67d802ee976ae4dfe5b27fd88e9`, STM32
`1bd3ecdba534549b4c9ac89f17fd8bd7270fe056`; latter pins former. Final metadata
commits retain the same dependency. See [the guide](docs/mul_core.md) for precise
files/APIs, wire specification, standalone build/export and A/B/C instructions.

CP5: 195 LPS default/continuous tests, 178 MUL sanitizer cases, both new/old DW
radio roles and cross-codec vectors pass. STM32 core/adapter/activation tests,
15 console/9 ESP and 9 interleaved coexistence scenarios pass sanitizers. A clean
committed export also builds/tests independently (6 console/8 ESP/8 coexistence).
All requested default/Stage 3E/Stage 3F/Stage 3G builds pass. LPS default/continuous
RAM unchanged; new diagnostic fits the same 128-byte scratch and 16372-byte RAM
footprint as Stage 3F. Resource/stack estimates and retained warnings are in guide.

CP6: at-most-once-per-ms polling, bounded USB queue and opt-in DWT timing for
outer SPI IRQ-off/service/start gaps. No masking/speed/safety logic change and
no claim that Stage 3F UART faults are fixed. CP7 hardware procedures ready.
The central roadmap is stale (3F not started); leave it unchanged for owner
update to Stage 3F accepted / Stage 3G software ready, hardware pending.

**Next exact action:** user runs A/B (>=100 completed TX each with matching
RX/previous-TX metadata) and C with disabled baseline, sensor/ESP traffic,
STOP/ARM/zero-speed heartbeat checks and timing/UART/drop counters. No hardware
flashed. Preserve the uncommitted 100 ms streaming patch for 10 Hz trials; published
STM32 baseline retains 1 Hz polling. Do not conflate the two tested images.
Full Stage 3G acceptance awaits those logs; physical timestamp/ranging accuracy,
UART causation, mixed-radio TDoA and Stage 3H remain unresolved/out of scope.
