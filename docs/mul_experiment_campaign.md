# Consolidated MUL experiment campaign

This is a prepared operator campaign, not authorization to run hardware. All work
in the software campaign used native tests, synthetic/replay inputs and builds.
No node was flashed, armed, moved or accessed. No Raspberry Pi software/hardware
or MCS/ROS bridge was implemented, configured or accessed. LPS consumes abstract
noisy poses over its existing USB interface; the pose source remains external.

## Prepare reproducible review artifacts offline

From lps-node-firmware, with a destination that does not exist:

```sh
python3 tools/mul_campaign.py --output /tmp/mul-campaign-NEW
```

This compiles the production planner locally, then writes55 case directories,
plan JSON/64-byte hex, three-cycle previews, exact LPS/STM configuration text,
metadata and future collector command text. It never opens serial/network or
executes those configuration/capture commands.54 cases pass preflight; the
optional2ms slot fails the current conservative timing/roster check and has no
activation commands. Keep that rejection; do not silently reduce guards. Existing
10/5/3ms candidates pass software feasibility, not physical deadline acceptance.
The1000us service/setup margin and20ms next-reference preparation stay intact.

Record all three repository commits, source pins, firmware SHA256 hashes, build
flags, board/radio identities, actual antenna reference points and current saved
settings before the operator campaign. Preserve the existing baseline settings
and calibration; do not reset EEPROM/flash settings. Firmware compatibility and
current sizes are recorded in the [engineering handoff](mul_upgrade_handoff.md).
Use the same matching full-plan fingerprint on every participant. Generated
plans are opt-in RF4; retained legacy profiles stay available via `plan use off`.
Fair policy uses codec2, freshness uses codec3; do not mix participant versions.

Generated command files assume an already initialized plan and correct assigned
IDs/modes. On first use only, RF OFF acknowledged, run `:plan show` then
`:plan init` on an LPS with no stored plan; STM uses `plan show`/`plan init`.
An existing plan intentionally rejects init. LPS IDs132..139 are ALRs, reference
is the first ordered ALR; RLRs141/142 must be outside that roster. Set roles/modes
and restart where required using the [command guide](mul_commands.md); verify
local identity before any activation. No generated file changes addresses, resets
calibration or sends ARM/motor commands. All mutations autosave; inspect every
response and never paste through an error or unconfirmed write.

For each case, manually acknowledge RF OFF on **all** nodes before applying its
files (`lps-config.txt` on every participating LPS, `stm-config.txt` on every
participating STM32). `off` requests a transition; wait for status to show both
requested/running OFF before further commands. Files disable both ranging classes
and deselect before timing/roster edits. While RF is OFF, they temporarily reduce
budgets to allowed minima and raise the utilization cap before normalizing the
period/spacing, then install the final reviewed budgets/cap. They select the complete plan while RF
remains OFF. Finish with `plan show`, `clock show`, `status`; expected plan CRC,
revision/rosters/timing/policy must agree with plan.hex/preview.json. Inspect the
actual reservation preview, not only a successful save acknowledgement.

Source selection is independently persisted on LPS using `:geometry source
{twr|external|none}`. It does not toggle AA reservations. External poses are
volatile; use `:pose begin CLOCK EPOCH FRAME SKEW_MS` then the canonical80-byte
`:pose put HEX160` records, and `:pose show`. Unknown/stale/noisy input never falls
back to truth or TWR. Disconnect clears the pose stream. Input covariance is
statistical, not a hard bound. Common frame/clock, antenna extrinsics and alignment
uncertainty must already be justified by the external source. See
[pose commands and host-only generators](mul_commands.md#abstract-pose-observations-software-only-interface).
Synthetic generation is host-only; no on-node noise or ROS subscription exists.

## Single authorized physical campaign, when separately requested

First run the known deployed baseline at the user's proven spacing and channels.
Record at least300s per case after clock warmup, at least three repeats for
comparisons, consistent pair geometry and independent truth. More cycles are
needed for small miss probabilities;300 observations cannot establish a rare-event
bound. Counterbalance case order within each phase/repeat with an archived seed;
repeat baseline between phases to reveal drift/thermal changes. Use one UWB
network at a time and keep mode/plan changes RF-OFF. Candidate budgets are
experimental, not measured MCU scheduling bounds.

| Phase | Prepared comparison | Hold constant and record |
|---|---|---|
| A | Median/regression × windows8/24/32 | Stationary ALRs/RLR, common source, geometry and broadcast plan; held-out prediction error, applied/fitted skew, fallback, inliers/residuals/age |
| B | Slots10/5/3ms;2ms rejected | Same radios/slots/LOS; observed missed broadcasts, capture age, vector availability, paired TDoA bias/std/RMSE and queue losses |
| C | All six permutations of three followers, reference132 fixed | Device identity and physical positions fixed; repeat each assignment; separate device and slot factors, retain interaction/residuals |
| D | TWR replies20/10/5ms, AA+AR | Identical plan targets/geometry; attempts/completions, timeout/late/stamp faults, six raw timestamps, power metadata, independent range errors and duration |
| E | ALRs2/4/8 × RLRs1/2 × AA/AR independently off/on; extra freshness+priority case | Fixed70% admission cap; planned vs observed operations, per-pair ages/gaps, ownership, broadcast preemption, starvation and missing peers |
| F | Moving RLR, moving ALRs, both; TWR/external/none propagation | Independent truth, noisy ALR observations/covariance, source clock/epoch/frame, validity, actual receiver-rate observability and optional filter comparison |
| G | Matched controlled LOS/NLOS | Same path/geometry/plan; occluder/material/placement, range/TDoA residual distributions, valid/missing rates and available signal quality |

For A–E/G **only when stationary**, STM `cfg set mode hybrid_static` permits the
explicit stationary receiver-rate assumption. For moving conditions use ordinary
`cfg set mode hybrid`; NO_RATE is expected when no justified rate exists and must
remain invalid, not coerced into distance. The external-geometry source corrects
propagation but does not identify an unobservable moving-receiver clock rate.
Missing metric observations are a result to report. Do not use stationary-only
rate assumptions while moving to obtain apparently available positions.

The optional EKF is real portable/ESP software with a defined input abstraction,
but the installed DATA2 fields do not establish measured wheels, source boot epoch
or aligned acquisition clocks. Physical EKF comparisons therefore depend on a
separately supplied genuine measured-input backend. Until then, keep `EKF OFF`
and run the [host-only replay campaign](../../epuck-esp32/docs/uwb_ekf.md) for the
filter comparison. This dependency does not require Raspberry Pi implementation
in this campaign. F external-pose physical tests likewise require an already
available external supplier; ROS/MCS/Pi integration remains explicitly excluded.
Never substitute pristine evaluation truth for missing noisy observations.

After all participants match and the operator has separately authorized radio
operation, use `:on` on LPS and `on` on STM. This is UWB activation, not motor ARM.
No motion commands are prepared here. The operator controls physical trajectories
through their independently authorized setup. Stop RF before the next case.
Retained defaults can be restored with `:off`, wait OFF, `:plan use off`, and the
matching STM commands; verify retained settings/source/mode, then restart if needed.

## Capture and analysis

Before each capture, STM commands `cfg set wifi on`, `cfg set telemetry on` and
`samples on` enable the existing research path (check SHOW/acknowledgements).
Samples opt into raw RX/verified TX; essential vector/range results remain
independent. Preserve complete USB radio diagnostics on all ALRs and STM, plus
raw Wi-Fi logs. From epuck-esp32, the generated capture-command.txt contains the
exact read-only collector invocation for its RLR roster, for example:

```sh
timeout --signal=INT 300 python3 tools/uwb_collect.py \
  --robot 192.168.1.141 --raw --metadata /tmp/mul-campaign-NEW/D-reply20/metadata.json \
  --output /tmp/mul-campaign-NEW/D-reply20/captures --no-live
```

Run that command only during the separately authorized experiment. Collector
sends CONTROL_HB only; it never arms, moves or configures robots. Network/serial
host times are reception diagnostics, not UWB timestamps. For a host USB terminal
that has pyserial installed, a future operator can record one LPS transcript using
`script -q -c 'python3 -m serial.tools.miniterm --raw /dev/ttyACM0 115200' alr132.log`;
select the verified local USB device, one terminal per node. That is a transcript,
not a synchronized pose or radio clock. No USB terminal is opened by preparation.
Keep raw input poses, noisy observations and pristine truth in separate files.

Offline, from epuck-esp32:

```sh
python3 tools/mul_campaign_report.py path/to/raw-or-compact.jsonl \
  --keys /tmp/sample-keys-NEW.jsonl --output /tmp/capture-audit-NEW.json
python3 tools/mul_campaign_report.py path/to/raw-or-compact.jsonl \
  --truth path/to/independently-aligned-truth.jsonl --output /tmp/error-report-NEW.json
python3 tools/ekf_replay.py --sanitize --output /tmp/ekf-comparison-NEW
```

The first report preserves exact keys, separates valid/invalid observations,
counts vector-follower availability/reasons, summarizes apparent clock metrics, per-pair acquisition gaps/age since the previous
valid observed sample and nominal same-node P1-to-P3 TWR spans, and computes within-session/reset-generation AR counter deltas. First snapshots
are excluded; missing attempt denominators stay unknown. Only closed immutable captures should be analyzed. It hashes source files
for stable raw-capture identity. Compare all cases using the same retained inputs.
It never derives acquisition time from reception time, joins independent clocks,
fits calibration or fills missing samples. Only the exact-key truth file enables
error bias/std/RMSE; unmatched truth is counted. Truth rows have
`{"sample_key":"EXACT exported key string","value_m":...}`. Independent evaluation
must justify source-time alignment and compute signed follower-minus-reference
range differences or AR ranges at each acquisition; matching a key alone cannot
establish that justification. Invalid measurements remain invalid.

From LPS, replay recorded REF_RX timestamps through both production clock models:
`python3 tools/mul_clock_replay.py path/to/alr133.log --period-ms 1000 --window 24
--summary`. Repeat with8/32 and archive the input/log configuration. Fitted residual
scatter is not independent error or an oscillator-noise bound. Analyze held-out
prediction residuals and geometry/motion uncertainty separately.

TWR duration must use same-node stamps only: initiator P1-TX to P3-TX (indices0/4),
responder P1-RX to P3-RX (1/5), modulo40-bit with the half-period guard and declared
nominal DTU conversion. Six-stamp ranges mix domains by design; never subtract
arbitrary adjacent stamps. Nominal duration is not a measured cross-clock bound.
AA ownership/duration/misses also require ALR transcripts and raw TX diagnostics;
the JSON report does not fabricate counters unavailable in the capture. Capture
both planned reservations and observed outcomes to assess actual freshness.

For C, fit/compare device and slot effects only after counterbalanced repeats;
keep reference identity fixed for this matrix. A changed reference is a separate
factor requiring another controlled block. Do not fit one arbitrary offset and
call device/slot effects resolved. For G retain quality values and missing/invalid
power flags; never infer LOS or a calibrated covariance from signal strength alone.

## Pre-register acceptance before physical execution

Mandatory correctness gates are zero wrong-slot/wrong-owner exchanges, mixed
fingerprints, accepted invalid/stale/NO_RATE measurements, unexplained TX marker
mismatches, unexpected control/motor changes or unaccounted resets. Any such event
fails that candidate regardless of aggregate accuracy. Persistence must retain
IDs/calibration/defaults; missing peers/reconnects must recover without stale data
or retransmission bursts. Verify task watermarks/heap/late counters under load (`EKF WORKER` exposes ESP
worker minimum free stack bytes and heap/free-largest-block diagnostics);
static builds and host timings cannot establish deadline or stack safety.

Compare each shorter spacing/reply against the same20ms-reply/proven10ms-spacing
baseline. Proposed screening gates: completion fraction no more than5 percentage
points worse, vector availability no more than1 point worse, and paired independent
range-difference RMSE no more than10% worse, with repeated-trial confidence intervals
and adequate denominators. These are proposed experiment thresholds, not passed
criteria or claimed physical performance. Record any chosen replacements before
collecting data. An infeasible/unobservable condition is reported, never silently
removed from the denominator. Assess physical error relative to the user's
reported5–7cm baseline, keeping noisy-pose contribution and truth uncertainty
explicit. No software result closes physical calibration or RF acceptance.

## Kalman missing-reference recovery handoff

Canonical core08db4d8 retains a previously ready same-epoch KF for bounded recovery
through6s; Median-5 stays default. Read `portable/mul_core/CLOCK.md` for the exact
continuity, innovation and uncertainty gates. Mapping still expires after2s.
A valid new real timestamp is mandatory. Longer gaps and rejected return origins
reset; explicit RF OFF/ON still reinitializes the radio owner. No schedule,
calibration, EEPROM, power correction or packet format changed.

October30 raw captures show receiver reference-sequence advances of2/3/4/5/10
in the Kalman run. These are observed-record gaps, not proof of physical RF loss.
The old3000ms liveness/interval gates and adapter resets amplified such gaps into
five-interval warmup. The source of the missing observations itself is not proven.

Identical cold-start receiver replay, original follower relations held fixed:

| Input | Old KF valid / snapshots | New KF | Old/new resets |
|---|---:|---:|---:|
| Kalman physical run |224/263|225/263|8/4|
| Median-A physical run |264/285|264/285|1/1|

On the Kalman input, KF-unapplied evaluations fall27→14. The224 common valid
TDoAs are identical (RMS/max change0mm); all236 common valid clock rates are
identical. Conditional output scatter is46.739→46.665mm, with different valid
selection. Six observed3..6-cycle snapshot-gap returns have ready-wait cycle
counts11/6/9/6/0/7→11/6/0/0/0/0; overlapping losses, unavailable snapshots and
ineligible prior state remain conservative. This is not all294 physical cycles:
recordings begin after prior filter history and contain missing observations.
Neither independent geometry nor ground truth for this run is available here;
no new physical RMSE or accuracy claim follows from this replay.

ALR133's282 recorded real TX/RX pairs, replayed **without propagation compensation**
and with explicitly inferred1Hz coarse time, give250→263 valid mappings and10→6
resets. This isolates numerical gap behavior, not the complete follower RF/TWR
adapter, actual uptime or accuracy. The deterministic real-adapter test separately
checks recovery after two missed references. Synthetic constant-skew tests bound
post-return rate error below2ppb; changing-rate/bursty tests retain the existing
20ppb assertion. Corrupt timestamps/covariance and stale origins remain rejected.

Resources, ARM GCC13: stationary LPS flash130424/131072 B (648 B free),
RAM14236/16384 B; fit state160 B with recovery flag in existing padding. No stack
reservation changed. Static core nested frames total816 B before library frames;
ALR event adds280 B (1096 B together). Both1600 B task stacks and2048 B interrupt
reservation remain; these are not runtime watermarks or WCET measurements.
Universal STM32 flash168480/786432 B, RAM44168/131072 B. The ESP bridge protocol
is unchanged; shared snapshots and host tooling are synchronized.

Reproduce analysis without devices:

```sh
cd ~/Documents/epuck-esp32
python3 tools/compare_clock_recovery.py ../uwb_logs/clock-comparison/kalman/20261030T144807019903Z-robot-0-raw.jsonl --alr ../uwb_logs/clock-comparison/kalman/alr133-kalman.log
python3 tools/compare_clock_recovery.py ../uwb_logs/clock-comparison/median-A/20261030T143930318029Z-robot-0-raw.jsonl
```

The tool compiles old4060df1 and new production C on identical captured inputs;
it asserts byte-for-byte Median output equality. Results are counterfactual and
do not replace the unchanged exact logged-baseline validator.

Operator-only next hardware check (not performed by the agent):

1. Record saved plan, calibration and methods. Build/program both ALRs using
   `make -f profiles/stationary-ch5.mk -j4` and the established operator-only
   `sudo make -f profiles/stationary-ch5.mk dfu`. Keep stationary CH5/age6000/speed0.
   Build/program STM32 with `cmake --preset Universal` and
   `cmake --build --preset Universal`, using the existing configuration-preserving
   SWD procedure in the logging guide. No reset/default/erase commands are needed.
2. With RF OFF, select `:cfg set clock_estimator KALMAN` on follower and
   `cfg set clock_estimator KALMAN` on receiver. Verify saved antenna values,
   plan826414CC and timing budgets unchanged; resume only under operator control.
3. Capture at least3minutes with both ALR USB logs and receiver raw/compact logs.
   Observe natural gaps or, where practical, operator-controlled brief attenuation
   that leaves the reference clock and sequence running. Reference OFF/ON or a
   power cycle restarts sequence and is a discontinuity test, not missing-packet
   holdover. Include1/2/multiple misses and a>6s interruption separately.
4. Require no valid stale mappings, immediate trusted return for eligible short
   gaps, full reset on genuine discontinuity/long outage, unchanged RF slots and
   calibration, exact replay and explicit transport-loss accounting. Inspect LPS
   `hold`, `rc`, `rst`, `rr`, `use`, `fb`, mapping status/age; STM32 CLOCK adds
   `holdover`/`recovered`. `recovered` marks retained gap history, not validity.
   Fit sigma remains the last updated covariance diagnostic during a silent gap;
   holdover never authorizes TDoA. Measure runtime stack high-water and service
   latency before long tests; compare identical independently measured geometry
   against a Median control before claiming retained physical accuracy.
