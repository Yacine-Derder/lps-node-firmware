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

Stages 0–2 are implemented and software tested.
Stage 2's hardware-validation status follows the user's current project report;
it is not a claim of hardware testing by Codex during documentation setup.
The prior timestamp-discrepancy investigation does not establish a root cause;
retain that distinction when interpreting validation and diagnostics.

## Hardware checkpoint — Stage 3A.1: Multi-ALR protocol and reference acquisition

Status: **Implemented/software tested; awaiting hardware validation.**
Objective: distinguish reference/follower roles and acquire reference broadcasts,
while preparing the RLR to retain observations separately for multiple sources.

Expected physical configuration, all using Bitcraze DW1000 LPS nodes:

| Node | Address | Stage 3A.1 role |
|---|---|---|
| ALR 1 | `0x84` | Reference transmitter |
| ALR 2 | `0x85` | Receive-only follower |
| RLR | `0x86` | Passive receiver |

Acceptance: reference acquisition and loss/reacquisition are observable; source
identity is preserved and RLR source state is isolated in software tests.
On hardware, `0x85` and `0x86` receive `0x84` without autonomous follower TX.
Stop at this three-node hardware checkpoint before Stage 3A.2.
The implementation now selects these mode-5 roles by full configured address.
Unsupported ALR identities remain idle. Software tests cover synthetic multi-source
RLR observations; physical three-node acceptance remains outstanding.

## Pending milestones

### Stage 3A.2 — Hardware-timed TDMA follower scheduling

Status: **Not started.** Objective: schedule follower TX from reference observations
using each follower's local radio clock and hardware delayed transmission.
Acceptance: tests cover deadline misses, wraparound, and reference loss; hardware
captures show intended reference/follower ordering without overlapping slots.

### Stage 3B — Relative clock offset/skew estimation

Status: **Not started.** Objective: estimate relative clock relationships while
distinguishing propagation delay from clock offset.
Acceptance: synthetic offset/skew/wrap tests pass; hardware residuals and model
limitations are measured, with no unsupported claim of exact offset identifiability.

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
