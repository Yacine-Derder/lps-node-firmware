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

Status: **Implemented/software tested; awaiting hardware validation.**
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

## Stage 3D hardware validation checkpoint

No physical hardware was flashed by Codex. Use the same current build on 0x84,
0x85 (mode 5) and 0x86 (mode 6), retaining addresses and PHY.

1. Default `make clean && make`: expect reference packets at 34 bytes, follower
   packets at 74 bytes, TWR at 84 bytes. TX scheduled/packet/actual must still
   match, TWR remain near five-second cadence, and PAIR retain raw/corrected local
   differences. After a successful TWR, the following follower slot can show
   clock_relation=AVAILABLE; other frames revert to UNAVAILABLE. Default TDOA
   must report reason=RLR_RATE_UNAVAILABLE whenever the relation is available.
2. Only for a declared stationary bench experiment, build with
   `make clean && make EXTRA_CFLAGS=-DMUL_EXPERIMENTAL_STATIONARY_RLR_RATE=1`.
   Keep all three nodes fixed and allow at least six fresh reference observations
   plus the next successful TWR. Expect intermittent TDOA status=AVAILABLE with
   rate=STATIC/<ppb>, signed dtu/mm, and power=1 only with both RLR RX corrections.
   Missing power retains raw TDoA (power=0); UNKNOWN physical uncertainty remains.
3. Place RLR at equal known distances from the ALRs, then nearer the reference,
   then nearer the follower. After each reposition, remain stationary for at
   least six reference observations before interpreting another fresh relation.
   Expected signed range difference is distance(follower,RLR)-distance(reference,RLR):
   near zero, positive, negative respectively. Compare logged mm against measured
   geometry and save all three logs; do not fit an offset from these observations.
4. Observe at least 40 seconds for counter wrap and several availability cycles.
   No extra transmissions, gaps, lockups or nonzero TX deltas should appear.
   Stale/missing model or rate must report unavailable, never a carried-forward
   TDoA. Additional deliberate restart tests are optional; software covers them.

The static option is an experimental declaration, not automatic stationarity
recognition. Constant radial motion can masquerade as skew, so never enable it
for mobile robots. No centimetre accuracy or finite confidence bound is claimed.

## Stage 3D.1 — Controlled transmission-spacing experiment

Status: CP4 diagnostics and independent physical oracle implemented; full validation next. Starting point
`3191b39`. User-reported midpoint geometry (1 m each side) produced -369 DTU /
-1731 mm, STATIC/-2534 ppb, age 503 ms, power=1; TWR 1.88–1.95 m versus 2 m,
follower skew +1.415 ppm, TX delta=0 and no gaps in the excerpt. An earlier
configuration gave about -4.5 m. Cause remains unknown; Stage 3D functional
operation is supported by these excerpts, physical accuracy is not accepted.

Implementation checkpoints:
- CP1: inspect timing and record this plan; baseline host suite and docs review.
- CP2: shared compile-time slot 500 (default), 50 or 10 ms; reject other values.
- CP3: derive association, converted-TX/RX gates, follower-presence gate and
  candidate age from the slot; keep clock equations and 1000 ms delay validity.
- CP4: configuration diagnostics and slot-parametric regressions; independent
  physical-clock synthetic oracle, including injected rate error and wrap.
- CP5: all-slot host/sanitizer/real-driver tests, three clean firmware builds,
  RAM/stack review, architecture and exact stationary hardware procedure.
Each checkpoint is committed and immediately pushed to origin/master. Record
its hash in the next checkpoint (a commit cannot contain its own hash).

Timing audit: slot is follower local hardware reference RX +500 ms, with live
TX antenna compensation and upward 512-DTU alignment. Fixed 450..550 ms gates
exist in observation association and downlink converted-TX/raw-RX validation;
reference follower-presence is 450..600 ms. Candidate/rate age is 700 ms and
publication lead is capped at 600 ms. Reference cadence/rate estimator uses 1 Hz
reference observations, independent of follower spacing. Publication predicts
freshness at actual TX, so shorter spacing changes age, not equations/lifetime.
TWR prepares at 630 ms, transmits near 650/670/690/710/730 and stops at 800;
POLL gates 600..700 refer to TWR, not follower slots. No proposed slot overlaps
this window. Keep TWR timing, five-second eligibility and broadcast priority.

Timing feasibility: reference packet completion follows its RMARKER; even the
supported slower/longer PHY combinations have an engineering allowance below
2 ms for the 34-byte packet tail. Normal SPI is 48 MHz /4 =12 Mbit/s (slow
initialization /16 =3 Mbit/s). A deliberately generous 1 KiB aggregate transfer
allowance costs 0.69 ms fast /2.74 ms slow, excluding HAL/CPU/IRQ overhead.
Follower work is bounded integer estimator/model/74-byte encoding before arm;
RLR freezes 34-byte RX/power diagnostics (including software floating point),
then rearms BEFORE association/logging. UWB task has highest task priority;
serial sends are nonblocking, but HAL SPI uses HAL_MAX_DELAY. Therefore no
finite worst-case wall time can be established from source/host tests: IRQ,
HAL stalls and software float costs require device measurement. At 10 ms the
existing 5 ms post-preparation hardware lead guard permits less than 5 ms total
RX-tail + dispatch + processing + setup (slightly less due to TX antenna delay).
Budgeting 2 ms RX tail +0.69 ms SPI leaves about 2.3 ms for CPU/dispatch at fast
SPI. RLR must rearm before the follower preamble, conservatively within 8 ms.
Ten ms is an experimental, fail-closed schedule, not guaranteed loss-free:
late ALR setup skips rather than moving TX or compromising timestamps. Validate
10 ms on-device; use 50 ms if skips/loss appear. No PHY or antenna changes.

## Codex continuation checkpoint

Stage 3D.1 CP1 `dd52423`, CP2 `9a4f43c`, CP3 `bdd7cbf` pushed; CP4 is the
commit containing this section. All 173 host tests pass in stationary 500/50/10
configurations, including real-driver TX equality, live TWR, out-of-window
one-DTU boundaries, missing reference/follower, and stalled-TWR broadcast
priority. Unsupported slot compilation fails as intended.
Independent SI-time/metre oracle uses three independent oscillators and absolute
clock origins, with zero and +/-1 m geometry and wrap: error <=2 DTU (~9.4 mm).
Injected +/-10 ppb produces analytical spacing-dependent bias (approximately
+/-1.499/0.150/0.030 m); wrong receiver units and arrival-aligned origins are
separately detected. The latter produces a fixed approximately +2 m error.
No firmware arithmetic/correction equation changed. Initialization shows slot
and RLR rate units; per-observation diagnostics retain their previous fields.
Reproduce matrix: `python3 tools/test/mul_slots.py` (temporary native configs;
add `--sanitize` for MUL ASan/UBSan including real-driver tests).
Next: CP5 default-policy full suite, all-slot sanitizer matrix, clean default/
50/10 firmware builds, RAM/stack review, architecture and hardware instructions.
No physical hardware flashed; measured hardware bias remains unexplained.
