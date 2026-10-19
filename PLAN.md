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
| Stage 2 — Hardware-delayed TX and accurate TX/RX timestamps | Hardware validated (user-reported) | Embedded antenna-referenced TX time and local raw RX time |
| Stage 3A.1 — Multi-ALR protocol and reference acquisition | Hardware validated (user-reported) | Reference reception, loss/reacquisition, and observed wraparound |
| Stage 3A.2 — Hardware-timed TDMA | Hardware validated (user-reported) | Both ALRs TX, passive RLR RX, approximately 500.04 ms separation, TX timestamp equality |
| Stage 3B.1 — Relative clock skew | Hardware validated (user-reported) | Stable negative skew, loss reset/reconvergence, preserved TX equality and RLR reception |

Stages 0–2, 3A.1, 3A.2, and 3B.1 are implemented and software tested.
Hardware-validation status follows the user's current physical-test reports;
it is not a claim of hardware testing by Codex for this milestone.
The prior timestamp-discrepancy investigation does not establish a root cause;
retain that distinction when interpreting validation and diagnostics.

## Hardware checkpoint — Stage 3B.2a: Arrival-aligned affine clock mapping

Status: **Implemented/software tested; awaiting hardware validation.**
Objective: map reference TX coordinates to expected follower arrival coordinates
and back, using the validated rate estimate and a recent observation as origin.
Keep propagation/true-offset ambiguity and unknown physical uncertainty explicit.
The mapping does not change the existing hardware-timed 500 ms follower slot.

Current configuration, all using Bitcraze DW1000 LPS nodes:

| Node | Address | Current role |
|---|---|---|
| ALR 1 | `0x84` | Reference transmitter, approximately 1 Hz |
| ALR 2 | `0x85` | Reference-following transmitter, approximately 500 ms later |
| RLR | `0x86` | Passive receiver of both sources |

Software acceptance covers known affine relationships, forward/inverse consistency,
quantization, both clock wraps, re-anchoring, bounded horizons/age, invalid slope,
skew invalidation, loss/restart, missing/duplicate/old observations, uncertainty,
propagation ambiguity, and long runs. Existing skew, TDMA, RLR and TX tests pass.

Physical acceptance: MAP progresses WARMUP to VALID with stable diagnostics for
at least 60 seconds across wraps, always UNCALIBRATED with UNKNOWN physical bound;
mapping invalidates on reference loss and reinitializes/reconverges on restart.
Both sources must retain approximately 1 Hz reception, approximately 500 ms
RLR-clock slot separation, and matching scheduled/packet/actual TX timestamps.
Stage 3B.1's intentional reference interruption/USB disconnect was expected.
Stop here for physical results; do not begin Stage 3B.2b.

## Pending milestones

### Stage 3B.2b — Propagation-delay correction

Status: **Not started.** Objective: establish independent delay information before
correcting arrival-aligned mappings toward true clock-domain conversion.
Acceptance: separate delay/offset assumptions and uncertainty are justified;
synthetic and hardware checks quantify correction accuracy and limitations.

### Stage 3C — TDoA-ready multi-ALR observation pipeline

Status: **Not started.** Objective: associate observations by source and transmission,
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
