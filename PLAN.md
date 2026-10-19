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
| ALR 2 | `0x85` | Reference-following transmitter, approximately 500 ms later |
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
The user now reports about 0.97 m at approximately 1 m and 5.51–7.24 m at
approximately 5 m, with functioning TWR, broadcasts, clock estimation and RLR
reception. The gross antenna-delay omission is fixed; remaining distance-dependent
bias may involve power, multipath, orientation or other effects. Stage 3C is now
authorized to add existing Bitcraze power correction and observation processing.
Physical accuracy and synchronization uncertainty remain unbounded.

## Pending milestones

### Stage 3C — TDoA-ready multi-ALR observation pipeline

Status: **In progress.** Objective: associate observations by source and transmission,
retaining clock domain, correction model, validity, and uncertainty.
Acceptance: tests reject missing/stale/inconsistent associations; hardware
observations from multiple ALRs can be paired without mixing clock domains.

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

## Codex continuation checkpoint

Stage 3C checkpoints 1–4 implemented. Previous pushed checkpoint: `1887dcd`.
CP4 is the commit containing this section: bounded reference/follower association
requires consecutive 1 Hz source progress in TX/RX/coarse clocks, reference-first
order, 450–550 ms raw arrival separation, <=700 ms age, matching receiver domains
and local generations. One candidate is consumed once; gaps/restarts require fresh
cadence. Raw/corrected local differences are available, but inter-ALR clock relation
and final geometric TDoA remain UNAVAILABLE because no clock model is broadcast.
Validation: 151 host tests and clean firmware build pass. RLR integration remains
passive and captures power before re-arm. No known failing tests. CP2 TWR uses a
versioned 84-byte format; both ALRs must upgrade. Broadcasts remain 34 bytes.
Limits: remote epochs UNKNOWN; power/antenna calibration and physical uncertainty
unbounded; 900 MHz unsupported; vendor endpoint holds retained.
Next exact task: checkpoint 5, full regression and all MUL ASan/UBSan, final clean
build, update architecture/roadmap and exact physical validation instructions.
