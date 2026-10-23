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

## Current multi-ALR milestone

**Implemented/software tested; manual hardware acceptance pending.** Persistent
ordered2..8-ID schedules, reference-relative hardware TDMA, collision-free
round-robin TWR, independent STM32 partial-cycle reconstruction, bounded vector
telemetry, ESP priority forwarding and self-describing Python JSONL are connected.
The validated two-ALR protocol remains the default for two-ID rosters. The user's
current10ms baseline reportedly achieves about5–7cm single-measurement RMSE; no
new physical accuracy is asserted. Historical checkpoints below describe earlier
stages and do not restrict this explicitly authorized multi-ALR milestone.

See [multi architecture](docs/mul_multi_alr.md) for limits/wire formats and
[handoff/deployment](docs/mul_multi_handoff.md) for builds, tests and exact manual
next steps. Hardware programming/operation was excluded and was not performed.

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
build dependency. Initial extraction preserved local work; the follow-up
integrates the reviewed streaming/client changes.

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

Final audit also fixed LPS incremental dependencies on canonical C/headers;
Make graph and source-mtime invalidation checks pass. Default/new RX rebuilds
retain their measured binary sizes. Flag changes still require clean builds.

CP6: at-most-once-per-ms polling, bounded USB queue and opt-in DWT timing for
outer SPI IRQ-off/service/start gaps. No masking/speed/safety logic change and
no claim that Stage 3F UART faults are fixed. CP7 hardware procedures ready.
The central roadmap is stale (3F not started); leave it unchanged for owner
update to Stage 3F accepted / Stage 3G software ready, hardware pending.

**Hardware review/follow-up:** A/B demonstrate bidirectional shared-core packets
and exact comparable previous-TX markers (43/43,62/62); boot TX counters89/93
fall short of the >=100 gate. C reaches at least542 valid RX with6 gaps and a
USART3 ORE (`bits=0x8`); no added recorded gap near that visible fault. The user
confirms the disabled baseline: separate1000ms and100ms sessions show zero UART/
drop/parser faults. Unmatched windows prevent causal attribution or delivery ratios.

The software follow-up adds explicit UWB-only STOP/disarm/stream-off policy,
quiet debug output and normal1Hz light-free DATA2 Wi-Fi telemetry. Reviewed
streaming and client/UI/tools work is integrated; normal mode remains available.
SPI speed/masking, core pin, packet/timestamp math and all LPS firmware remain
unchanged. Five STM32 profiles, safety/diagnostic/transport/client tests and ESP
build pass. Full evidence and exact build/ELF/CubeProgrammer commands are in
[the guide](docs/mul_core.md#next-user-experiments--uwb-first).

**Next exact action:** sustained UWB-only >=10min/>=500 completed TX per direction,
then matched UwbOffSafe baseline; optional controlled NormalBaseline/CoexistRX
comparison. Establish safety/STOP and UART impact before closing Stage3G.
No hardware flashed; no Stage3H. Physical timing/ranging accuracy and the exact
cause of UART overrun remain unresolved.

## Stage3G follow-up / Stage3H ADS-TWR foundation

**Software implemented/tested; physical ranging and accuracy unverified.**
The UWB-only forward capture reaches TX done537 and RX valid525/old519. All515
comparable previous-TX markers match. OLD is reproducible retained ordering
baseline after low sender restart sequences, not demonstrated stale RF traffic;
gaps0 cannot measure loss across that epoch. Explicit order and logged5s idle
epoch handling preserve cumulative semantics; rapid v1 restarts remain ambiguous.
The misnamed G-UWB-off-baseline.log contains DW TX, not an STM32 baseline.

Portable versioned ADS core and DW1000/DWM3000 adapters implement one stationary
pair with either initiator role. POLL/RESPONSE/FINAL plus measured timestamp
REPORT provide six actual40-bit markers; no guessed delayed TX stamp. New fresh
session/transaction, bounded state/deadlines, whole-exchange retries and strict
UWB-only safety isolation. Nominal antenna defaults are explicit/read back and
uncalibrated. Existing Stage3E/3F/3G formats, EEPROM and normal modes remain.
Canonical portable checkpoint 0e3b5802e04f87e2c6e5e41ae07c165ae07ea0d1 is pinned
in STM32. ESP/dsPIC/central project are untouched this iteration.

Next: [stationary ranging handoff](docs/mul_ads_twr.md), controlled measured
separations and recovery tests; separately repeat clean forward and sustained
reverse Stage3G communication with a genuine receiver-off baseline/counter
endpoints. Stage3G hardware closure and Stage3H ranging accuracy are not claimed.
No localization, multi-robot scheduling, TDoA extension or online calibration.


## ADS hardware-log audit and minimal hardening

**First six-timestamp mixed-radio exchanges demonstrated by user logs; calibrated
accuracy, safety and hardware acceptance remain open. No new Stage3H work.**
219 printed results independently match timestamp arithmetic/units. One negative
sample is input-consistent, not an identified formula/wrap bug; its physical cause
remains unresolved. Nonnegative verified218 samples average1.3501m at an approximate
1m reference; this is not an antenna calibration. Closed initiator cohort219/225
computed exchanges is separate from responder counters and is not a PDR.

Reusable deterministic analyzer, tests, raw CSV and generated report are in tools/
test and docs/ads_first_hardware_*. Negative raw measurements are retained but now
marked range_valid0; completed and physical-range acceptance stay separate.
Session-tagged TIMES/STAT, classified active aborts/saturating failed-stage counts
and optional ADS SPI/service timing improve diagnosis. No equation, PHY, antenna
constant or SPI policy change. Portable pin0e3b5802e04f87e2c6e5e41ae07c165ae07ea0d1.

Five nominal-distance captures now exist; only the 5m run reaches >=500 audited
valid ranges. Next: precise references, >=500 completions per distance, complete
paired captures, STOP/disarm/stream-off evidence, genuine UWB-disabled baseline,
UART attribution and held-out calibration checks in docs/mul_ads_twr.md. USART1
ORE in the responder capture is demonstrated; causation/impact on UWB is not.
Existing Stage3G gates remain open. No flashing, ESP/dsPIC edits or central-doc edit.


## Universal STM32 runtime UWB configuration

Software implemented; physical persistence/role switching/timing/accuracy gates
remain pending. One UWB-only STM32 image selects ADS role independently of robot
identity, addresses across the complete 8-bit ADS namespace, interval,
antenna delays, optional additive calibration and bounded local telemetry.
Versioned CRC journal reserves F405 sectors10/11; validated SET automatically
quiesces/preflights radio, commits/verifies settings and resumes. Safety/console
rejections make no configuration change; identical values perform no write. Existing
motor-enabled normal profiles remain compile-time choices; safety cannot be
weakened by configuration. Shared core pin and DW1000 firmware are unchanged.

The five-distance audit proposes -469mm for this pair only; calibration defaults
off, no scale fit. References are manual and uncertainty unverified; 5m median
bias is about569mm. The 2m accumulated counters are operator-confirmed reuse,
not independent evidence of a defect. See [additive analysis](docs/ads_additive_calibration.md).

Next: physical acceptance of runtime persistence and the untethered multi-node
extension below. Existing Stage3G safety/baseline gates and held-out calibration
validation remain open. No software result closes those gates.


Runtime configuration correction: human-readable case-insensitive commands,
HELP/SHOW and automatic durability replace the pending APPLY/SAVE workflow.
Factory radio-OFF settings use 0x00/0x01; existing schema-v1 records load unchanged.
ADS codec now supports IDs 0x00..0xFF with unchanged four-message format and strict
full extended-address suffix; legacy diagnostic/TDoA policies remain unchanged.
The current canonical portable checkpoint is recorded by each vendor UPSTREAM.json.

Next hardware check: same Universal ELF on custom boards 0x10/0x11, opposite roles,
uncalibrated; verify each SET confirmation, power-cycle persistence, sustained
six-stamp ranging and STOP/STREAM0 safety. The former SAVE guard is reproduced in
software with radio stopped but STOP/STREAM0 unconfirmed; the supplied hardware
error alone cannot identify its exact missing acknowledgment. See STM32 guide
for actionable rejection reasons, rollback/power-cut tests and timing limitations.
No hardware validation or stage closure is claimed by these software changes.


## Untethered multi-robot milestone

Implemented/software tested; physical acceptance pending. Phases:

- Shared timing/telemetry: versioned CRC records, clock domains/epochs and native
  timestamp/wrap/apparent-rate primitives; legacy DW1000 behavior preserved.
- Untethered capture: optional bounded STM32 UART3→existing ESP32 bridge→Wi-Fi,
  read-only multi-robot JSONL collector, expiry/drop/timing/error diagnostics.
- Multi-peer ADS: eight persistent peers, isolated state, deterministic fair local
  scheduling, peer statistics and primary-only existing pair calibration.
- Acquisition: meaningful STM32 beacon/listen TX/RX, previous actual TX metadata,
  paired observations and apparent arrival-clock models. Offline TDoA requires
  external common-clock mappings; no global synchronization/position claim.
- Integration: sanitizer/fault-injection, actual adapter simulations, safety/UART,
  STM32/ESP32/LPS builds and existing Unity tests. No hardware flashed or operated.

Next user campaign: STM32 `docs/uwb_network.md` provides exact configuration,
collector/offline commands and twelve ordered checks. Start with the proven
stationary two-board ADS pair, Wi-Fi off, preserving calibration. Then compare
Wi-Fi idle/active, multi-peer loss/rejoin, beacon captures and STOP/watchdogs.
Distributed collision-free multi-initiator scheduling, moving-node propagation
separation, common-reference physical calibration and position estimation remain
unimplemented. Keep one coordinated initiator per collision domain. Software
completion does not close previous hardware gates or establish RF accuracy.

## Heterogeneous custom-TDMA RLR integration

Software checkpoints A–E implemented/tested; physical acceptance pending.

- A: confirmed current custom84/85 ALRs, passive RLR and650..800ms inter-ALR TWR
  as reference (e0c9955). Stock Bitcraze TDoA and0x75 beacons are not substitutes.
- B: original pure codecs/association/clock/TWR/downlink math shared through LPS
  forwarding APIs; passive DWM3000 adapter (STM32 a514ee4), same slot choices.
  Only necessary ALR change is opt-in custom-mode channel5 PHY, with default
  channel2, EEPROM, packets and scheduling preserved.
- C: bounded UWB3 original-frame/RX/provenance telemetry with v1/v2 compatibility
  (ESP dce7f3d). Universal-only SPI IRQ-preemption/timeout hardening (STM32 dca615e)
  removes a demonstrated software overrun risk without hiding UART faults.
- D: exact shared-C offline replay and separate guarded generic fixed-anchor2D
  solver (ESP d3a94e3), with synthetic physical-clock/wire/collector/replay and
  localization tests. Default receiver rate stays unknown; stationary opt-in is
  conditional. Two current ALRs cannot identify unconstrained2D position.
- E: all-slot/profiles,195 Unity,178 MUL sanitizer tests at each500/50/10ms,
  existing/new STM32 safety/UART/adapter tests,41 ESP Python tests and firmware
  builds. Both vendors pin the canonical source recorded in UPSTREAM.json.

Next user work: STM32 docs/hybrid_uwb.md provides exact ALR/RLR builds/configuration,
compatible image paths, collector/replay/solver examples and seven ordered checks.
Begin with both ALRs on matching channel5/slot500 and passive RLR, Wi-Fi off.
Preserve the existing calibrated primary ADS peer; ADD legacy ALRs to its table.
Then check stationary-rate observations, loss/restarts, multiple passive RLRs,
Wi-Fi idle/forwarding, USB/Wi-Fi drop separation and STOP/watchdog behavior.

The user's deleted2026-10-22 capture summary is preserved in that guide, explicitly
historical and not independently reanalyzed. HAL bits0x8 is ORE; no evidence yet
shows either UART symptom resolved. Physical interoperability, clock/bias quality,
receiver-rate observability in motion and measured2D localization remain open.
More independent geometry/measurements are needed before current-network2D
position output is identifiable. Previous hardware gates remain open. No hardware
was flashed/armed/moved; the central project document was not edited.


## Heterogeneous checkpoint C/D follow-up

The user has authorized proceeding with C/D while preserving ordinary hybrid
NO_RATE. Checkpoint A's original capture-age/relation defect is physically accepted
from the user's STM retest; metric moving TDoA remains unvalidated. Checkpoint B
feasibility, explicit-bounds prototype and synthetic full-wire geometry tests are
recorded in STM PLAN.md and ESP docs/uwb_mobile.md.

C foundation adds explicit per-context reference/follower rosters in the portable
core, with legacy-default wrappers and unchanged wire/timing/math. Sanitized
swapped/custom network tests cover publication, geometry, NO_RATE, passive TWR and
cross-network isolation for500/50/10ms. All195 Unity tests and the default channel2 firmware build pass.
DW1000 adapter integration now threads the roster through RX, TX/TWR, clock,
relation publication and passive observations. Additive role TLV migration and
bounded :mul commands preserve unrelated settings; corrupt/unreadable EEPROM is
retained with normal RF idle.207 Unity tests and channel2 plus channel5 timing
builds validate this software checkpoint. The two roles share the same image.
STM32550b069 and ESPd1e6b8f integrate persistent schema3 rosters and explicit
capture/replay options, with62 host tests and actual passive adapter tests at all
three spacings. A USB ingress follow-up replaces the demonstrably lossy10-byte
queue with single-packet endpoint backpressure and connection-generation resets.
All207 Unity tests pass, including three actual USB tests under ASan/UBSan.
C is implemented/software tested; custom-roster physical validation is pending. See docs/mul_heterogeneous.md for exact commands, builds and limitations.
D is **implemented/software tested; physical command/RF retest pending**.
The implemented vocabulary, capability differences, exact build flags and operator
checks are in docs/mul_commands.md. STM32 D reference is7769a30682ce8f0d90b31245b496cc657ea8c61c;
STM32 and ESP32 were not modified in this DW1000 task. Shared protocol/core code,
mode IDs, wire data, TDMA/TWR, antenna values and40-bit arithmetic are unchanged.

DW1000 now has :command/:console framing plus legacy :mul/character menus,
independent RF TLV10, explicit passive role3 in TLV9, and owner-only idle/clear/
fresh-init transitions. Pending restart, failed/uncertain storage, invalid required
fields and explicit passive-role conflicts block on. Valid off requests suppress
callbacks immediately and stop in the UWB owner even if EEPROM persistence fails;
the old durable on value may still return after reboot. A failed write is never
reported as verified. Legacy EEPROM remains non-journaled. Concurrent console/LPP
writers fail closed without holding a lock over EEPROM waits. Help/replies reuse
48bytes and atomic bounded TX; connection changes cancel partial/session state.
Diag reset clears only owner event counters after off acknowledgement, retaining
protocol identities/models. Ordinary RLR remains NO_RATE; static remains explicit
stationary-only build policy. No new moving clock-rate estimator was added.

Validation: rake unit summary passes217 tests;15 configuration/lifecycle and5
actual USB tests also pass ASan/UBSan. Coverage includes owner-only SPI, failed and
committed-but-unreadable off, reboot/no-op persistence, late events, invalid syntax,
restart protection, RLR-from-TX, fresh initialization, concurrent writes, preserved
anchor-position/power values, USB fragmentation/session recovery and bounded TX.
Existing real-adapter TDMA/TWR, passive RLR and wrap tests remain passing.
All13 clean REV=B/BOOTLOAD=0 firmware profiles pass: default CH2; CH5 at500/50/10;
stationary CH5; PHY/diagnostic/ADS TX+RX; FTDI; longer range. Every initialized LOAD
section was checked within0x08000000..0x08020000. Both linker scripts now explicitly
check the data load image too. Only non-radio command/configuration support uses
-Os. Override builds retain names/IDs but drop unreachable normal algorithm table
references; normal images retain all seven engines. Final default text129024B,
data1096B, bss15204B (includes reserved stacks); RAM headroom92B, flash image end
0x0801FC48 (952B headroom). Existing platform/FreeRTOS/newlib warnings remain.
Default bin/lps-node-firmware.elf/.dfu restored by a clean default build. No hardware
was flashed or operated. The unrelated docs/mul_core.md formatting edit is retained
and excluded. Stop after this DW1000 commit/push; no next checkpoint is authorized.

Physical limitations: command responsiveness, cancellation of a pending delayed TX,
RF silence after acknowledged off, and reacquisition after resume need the operator
checks in docs/mul_commands.md. EEPROM power loss, stack/timing margins, custom
rosters, residual UART/PHY/TWR faults and metric accuracy remain unvalidated here.

## Persistent hybrid delay — software complete, hardware acceptance pending

One build per platform now supports10/50/500ms through persistent `slot_ms`.
ALR changes require acknowledged RF OFF; the radio owner applies them on resume.
STM32 uses its existing stopped-radio transaction and schema5 journal migration.
New configurations default10ms; old unstored timing migrates to500ms without
rewriting peer/calibration/roster settings. Both ALRs and RLR must be explicitly
set to10ms for the next experiment. Compact broadcast schedule tags detect
mismatch without changing frame lengths or the1Hz cadence/TWR window.

Validated:221 native tests and199 MUL ASan/UBSan tests per10/50/500ms build;
single-executable runtime slot selection in real-driver and shared receiver tests;
STM32 runtime/persistence/mismatch/telemetry and UART/console/frequency suites;
76 ESP Python tests plus C transport tests; canonical/vendor byte agreement;
LPS default and CH5, STM32 Universal and ESP32-S3 builds. Build/linker bounds pass.
LPS has very little flash/RAM margin; bounded passive/codec code uses -Os while
hardware drivers, TX scheduler and clock estimation remain -O3. No buffers or
stacks were reduced. No hardware was flashed or operated. Operator deployment,
actual same-clock10ms timing, sustained Wi-Fi capture and mismatch/reboot checks
are specified in docs/mul_commands.md. Multi-ALR scheduling remains out of scope.
