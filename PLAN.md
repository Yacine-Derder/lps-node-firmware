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

## Stage 3D.1 — Controlled transmission-spacing experiment

Status: **CP1–CP5 implemented/software tested; awaiting controlled hardware comparison.**
User-reported geometry: reference --1 m-- RLR --1 m-- follower. Expected geometric
range difference ~0 m; reported `TDOA status=AVAILABLE tx=07704B52F0
rx=07704B517F dtu=-369 mm=-1731 rate=STATIC/-2534 age=503 power=1`.
TWR was 1.88–1.95 m versus nominal 2 m, follower skew +1.415 ppm, TX delta=0,
reception/pairing worked, and no gaps appeared in the midpoint excerpt. Relation
was available shortly after TWR, then stale/unavailable. An earlier configuration
produced approximately -4.5 m. Cause remains unknown; shorter spacing is a
diagnostic experiment, not an established fix or hardware accuracy acceptance.

Completed checkpoints:
- CP1 timing inspection/plan: `dd52423` pushed.
- CP2 shared compile-time slot configuration: `9a4f43c` pushed.
- CP3 slot-dependent scheduling/association: `bdd7cbf` pushed.
- CP4 diagnostics and independent physical-clock oracle: `c61303e` pushed.
- CP5 full validation/docs: the commit containing this section; hash reported
  after push (a commit cannot contain its own hash).

Configuration, timing windows, latency budget and exact clean build commands:
[architecture spacing experiment](docs/mul_architecture.md#configurable-spacing-experiment-stage-3d1).
TWR stays 650..730 ms with 800 ms cutoff, no collision with 500/50/10 ms slots;
priority and the one-second delay validity remain unchanged. Both ALRs remain
1 Hz, TWR approximately five seconds, RLR passive. No physical hardware flashed.

## Hardware comparison — next required work

The user performs this experiment; Codex has not measured RF timing or accuracy.
Keep reference --1 m-- RLR --1 m-- follower, antenna orientation/height and
surroundings identical. Preserve addresses 0x84/0x85 mode 5 and 0x86 mode 6.

1. Build the stationary 500, 50 and 10 ms configurations using the architecture's
   commands, one at a time. The same image/configuration goes to all three nodes.
   Confirm slot_ms at initialization and record full logs from both ALRs/RLR.
2. After each build allow at least six fresh reference observations and a
   successful TWR. Collect at least 10 AVAILABLE observations over multiple TWR
   cycles (usually only one such observation per five-second cycle).
3. For each observation retain the adjacent PAIR/TDOA sequences, actual arrival
   slot_us, signed DTU/mm, reference-unit TX/RX intervals, STATIC rate in ppb,
   relation age and power flag, plus corresponding TWR distance/skew. Retain
   UNAVAILABLE reasons too; uncertainty remains UNKNOWN.
4. Verify packet lengths 34/74/84, scheduled=packet=actual with delta=0, near-1 Hz
   broadcasts, near-five-second TWR, no unexplained gaps or starvation. Observe
   >40 s for wrap. In particular, check 10 ms for SLOT skipped/late TX, missing
   RX/pairs and TWR failures: source-level budgeting does not prove feasibility.
   If it fails, use 50 ms as the shortest validated interval; never relax guards.
5. Compare mean signed error, sample standard deviation, RLR-rate mean/spread,
   actual arrival separation and TWR ranges for each slot. Proportional bias
   implicates residual rate error; fixed bias warrants offset/propagation/domain
   investigation. Mixed results require quantifying both components; estimator
   noise, changed scheduling or hardware conditions may confound interpretation.
   Do not fit a firmware offset or promote this stationary option to mobile use.

## Codex continuation checkpoint

Stage 3D.1 CP1–CP4 hashes above are pushed; CP5 is the commit containing this
section. All software work is complete. Native suite: 173/173 for every stationary
slot plus default 500 policy; MUL ASan/UBSan: 160/160 for each slot plus default
policy, including 11 real-driver cases per configuration. Unsupported slot20
compilation fails. Clean default and stationary 500/50/10 firmware builds pass;
final local build is default.
Independent physical-clock oracle: midpoint and +/-1 m recovered within 2 DTU
(~9.4 mm) at each spacing, with wrap; +/-10 ppb injected errors follow analytical
bias approximately +/-1.499/0.150/0.030 m, while omitted propagation gives a fixed
approximately +2 m. No physical cause has been established for the user's bias.
RAM unchanged: data1072 + bss/reservations15292 =16364 bytes, 20 spare; scratch128,
UWB1600 and main/ISR2048 reservations unchanged. Compiler frames: ALR entry184,
TX setup256 (8 above previous recorded estimate), publisher376, corrected API240,
map80; RLR event512, association216, relation inverse56. Reviewed publication
chain approximately1264 bytes before compiler-library/context overhead, within
1600 reservation with margin; no extra buffers/state. On-device stack high-water
and loss-free10ms timing remain unmeasured. Default text120840 (+56), stationary
50/10 text120808. Diagnostic burst unchanged (<=501 bytes). Next exact work:
user hardware comparison above; no Stage4/5 or physical flashing by Codex.
