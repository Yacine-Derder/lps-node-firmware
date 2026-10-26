# MUL software campaign — 2026-10-26

## Active scope and checkpoints

The current user request authorizes a software-first clock/timing/scheduling/TWR/
geometry/odometry campaign across all three repositories. Continue between stages
without hardware requests. No flashing, robot operation or physical experiments.
Existing branches: LPS master, STM32 main, ESP32 main. Preserve the pre-existing
LPS `docs/mul_core.md` edit. No other initial working-tree changes were present.

## Audit and design

- Legacy median clock state is80 bytes on ARM, shared by ALR, passive LPS and
  STM32 rate estimation. Its1Hz interval gate is separate from mapping/propagation
  APIs. Preserve those APIs and defaults; add caller-owned opt-in fit state.
- Regression will fit local-minus-reference phase against unwrapped reference
  time, centered with bounded integer arithmetic. Quantized reference coordinate
  avoids huge absolute counters and64-bit products. No floating-point dependency
  is needed on LPS. Default window24, configurable8..32, explicit outlier/fallback/
  discontinuity diagnostics. Propagation compensation is optional and age-bound;
  arrival-aligned mapping must not silently become a simultaneous mapping.
- LPS has44 bytes static RAM margin. TDoA3's1200-byte context is mode-exclusive
  with MUL and is a candidate for explicit typed workspace sharing; do not reduce
  task stacks/queues or enlarge every legacy clock object. Verify a clean firmware
  link before claiming integrated support.
- Existing schedule schema1/profile1 or2 binds only ALR roster, spacing and1Hz
  period. Numerical timings/optional RLR reservations require an additive versioned
  experimental schedule, retaining old profile defaults and codec meanings.
- Five-message TWR and its raw delayed-TX/antenna contracts remain the foundation.
  Current5ms minimum programming lead prevents honest3ms follower spacing; such
  a profile needs a separately configured, explicitly unverified lead budget.
- Existing Wi-Fi wheel telemetry describes commanded steps/s, not measured wheel
  displacement. An EKF must accept a defined timestamped measured-step input and
  must not interpret commands as odometry.

## Work order

A audit/design; B selectable robust clocks and propagation observation interface;
C numerical timing/versioned settings; D configurable TWR and deterministic
optional ALR/RLR reservation generator; E compatible assigned active RLR ranging;
F timestamped geometry and optional host EKF; G integrated tests/builds/replay and
one consolidated A–G experiment campaign. Mark a stage complete only after its
production integration and stated tests/builds pass. Portable-only pieces are
explicitly intermediate, not completed firmware features.

## Baseline source and evidence

Initial heads: LPS c77c840, STM32 47ad135, ESP32 e83638f. Canonical shared-core pin
fbe9c2e. ALR logs `uwb_logs/alr132.log` through `alr135.log` and compact logs from
2026-10-26 are available locally and must remain unmodified. Observed slot biases
are hypotheses, not proof of a particular clock error. Prior hardware/software
handoff: `docs/mul_multi_handoff.md`.

## Continuation commands

```
cd /home/derder/Documents/lps-node-firmware
sh portable/mul_core/tests/run.sh
python3 tools/test/mul_slots.py --sanitize --prop-age 6000
make clean
make -j4 REV=B BOOTLOAD=0 EXTRA_CFLAGS='-DMUL_HYBRID_CH5=1 -DMUL_PROP_MAX_AGE_MS=6000'
cd ../epuck-stm32
sh tests/runtime_config/run.sh
cmake --build build/Universal
cd ../epuck-esp32
sh tests/run_host.sh
```

Update this document with verified commits, acceptance results and exact next work
at every coherent campaign checkpoint. No new physical acceptance is asserted.

## Clock core checkpoint (portable, adapter integration next)

Added `mul_clock_fit.[ch]`: selectable median/regression, centered integerQ24 slope,
8..32 observations (default24), fit-scatter diagnostics, guarded outliers and
three-rejection transient recovery. Fit coordinates rebase when evicting points;
raw40-bit counters are unwrapped only across bounded accepted intervals. Mapping
origins remain raw even when timestamped propagation is subtracted from fit inputs.
Switching propagation availability resets the window. Fallback retains the median;
rejected samples explicitly invalidate the current mapping instead of publishing a
relation against an old sequence. Added explicit-period clock API; old wrappers
retain1000ms semantics. A fit context costs336 bytes on the host, so LPS integration
must use mode-exclusive storage, not a new global allocation.

Verified shared ASan/UBSan tests and226 native10ms regression tests. New deterministic
clock tests run2000 observations per case at100/500/1000/2000ms, signed skews,
window8/24/32, gaps, noisy timestamps,32/40-bit rollover, outliers, oscillator
transients, and changing propagation. Under this synthetic noise model fit absolute
frequency errors are no worse than median; this is not evidence for the field bias
hypothesis. Full selectable firmware/persistence/replay acceptance is still pending.

## LPS adapter checkpoint

Regression is selectable through RF-OFF settings: `cfg set clock_estimator
median|regression`, `cfg set clock_window 8..32`, `cfg set clock_compensate on|off`,
`clock show` (prefix commands with `:` outside console session). Settings use a
versioned12-byte optional EEPROM TLV; absent means unchanged median/window24/
compensation-off. On applies settings and resets fit state. Invalid data fails
closed. `MUL FIT` lines report window/count/inliers/ppb/RMS/rejections/conditional
fit sigma and compensation/ready flags; physical mapping uncertainty remains
unknown. A rejected clock-fit sample still produces its correctly numbered TDMA
broadcast, publishing unavailable correction rather than an old sequence.

The336-byte fit plus settings overlays the inactive1200-byte stock TDoA3 context
through a typed union. The stock context's field layout and algorithms are retained;
only boot-selected MUL mode uses the alternate member. Native tests use separate
host storage. Application and HAL objects use-Os to fit; RTOS/libdw1000 keep-O3.
Clean channel5 build passed: text127872/data1128/bss15244 (`size` includes2048-byte
linker stack reservation); map actual.data1120 +.bss13196 +2048=16364,20 bytes spare.
No stacks or queues reduced. Timing/stack high-water remains hardware-unverified.
Native10ms suite passes228 tests, including settings persistence/RF-OFF guards and
actual50-cycle follower regression/outlier handling. Shared hybrid and multi RLR
now accept the same caller-owned fit configuration, still median by default.

Next: pin this shared core into both consumers; add STM32 schema7 extended clock
settings with old-schema migration, select the estimator in both receiver paths,
add diagnostics and persistence/power-cut/native tests, then offline replay. Clock
stage is not fully complete until these integrations/builds pass. Later stagesC–G
remain as listed above; do not infer they were implemented by this checkpoint.

## Replay evidence and adaptive fallback

`python3 tools/mul_clock_replay.py --summary ../uwb_logs/alr13{3,4,5}.log`
compiles the real production C clocks into a host library and reads existing
REF_RX timestamps without modifying logs. It also compares held-out linear and
quadratic phase forecasts; quadratic remains offline only. Three Python tests
cover the ABI, outlier behavior, parser/reset/domains and held-out polynomial fit.

Field captures contain204/201/199 reference observations. With a24-point window,
plain linear fits lag the changing apparent rate: last fitted-minus-recent-median
is approximately-26/-38/-38ppb. Their one-step phase forecast RMS is about
37898/39681/40449DTU, versus4308/4297/4605DTU for offline quadratics. This is
apparent timing evidence, not identified oscillator drift or a proof of the
15ppb slot-bias hypothesis. Device/geometry/propagation effects remain confounded.

Production regression now falls back to the recent median when their difference
exceeds max(10ppb,6*max(interval median absolute deviation, conditional fit sigma)).
This avoids applying a stale center-of-window frequency to a changing oscillator.
Fit formation and actual application are distinct diagnostic flags. Fallback codes:
0 used,1 warmup,2 changing rate,3 outlier. Synthetic2ppb/s drift tests verify recovery
and fallback; startup-outlier tests verify robust initialization. In these logs the
regression is applied only31/27/27 fitted observations; most remaining observations
correctly choose median. Default estimator is still median. Conditional fit sigma
is not a physical uncertainty bound. Native context remains336 bytes.

Adaptive LPS channel5 build passed: text128216/data1128/bss15244; RAM margin remains
20 bytes with unchanged task/queue reservations. After pin refresh, STM32/ESP must
use this adaptive core revision rather than the earlier d8505db import.

## Research clock telemetry addition

UWB3 type10 is an additive optional clock diagnostic; all prior record types keep
their meanings. It retains152 bytes/309 characters: raw reference TX40..44,
local RX45..49, reference sequence50..53, applied/fitted signed ppb54..61,
RMS62..65, rejected66..69, conditional fit sigma70..73, mapping age74..77,
reference ID78, method79, window/count/inliers80..82, mapping status83,
fit-ready84, applied85, fallback86, compensated87;88..149 are zero/reserved.
Header exchange binds the process-local clock epoch. This is optional queue traffic
and must never displace essential TDoA vectors. Unknown fit sigma isUINT32_MAX,
not a physical uncertainty claim. Consumer publication/parsing follows this commit.

Regression LPS logs now use one FIT line instead of duplicating old CLOCK/MAP
lines, retaining raw REF_RX and reporting mapping status/age in FIT. Median text
output is unchanged. This limits added pressure on the existing USB queue.

## Stage B integrated checkpoint / compatible consumers

LPS shared core29c1f7a is pinned in STM32 commit1da6900 and ESP32 commit96e397c;
all three pushes were verified. LPS source commitsdd5d932/e21f0ed/d8505db/9e30bba/
29c1f7a cover design, core, ALR settings/storage, replay/fallback and diagnostic wire.
Median remains default. Opt-in regression and window settings work persistently on
ALR and STM32; both legacy and multi STM32 receivers use the same production core.
Schema7 adds clock settings to extended256-byte STM32 records with final commit,
old-schema read-only defaults, all word-cut tests, rollback and reserved-byte checks.
`clock show` is read-only. In hybrid mode, `cfg set samples on` plus `tlm on` enables
optional type10 clock records; default samples-off queue behavior remains unchanged.
The RLR native test exercises45 cycles with a persisted30-point window and confirms
that raw clock telemetry matches captured reference TX/RX and radio cycle exactly.

Canonical `mode hybrid[_static]` had a pre-existing transition bug from standalone:
it restored the old standalone protocol role after selecting a passive mode. This
was fixed while preserving the explicit standalone requirement for selecting a
transmitting mode. Clock tests enter passive mode from defaults and remain no-TX.

Final StageB checks: shared ASan/UBSan core suite;228 native +206 MUL sanitizer tests
at each500/50/10ms;30 host-analysis tests; all available STM32 run.sh suites
(spi_polling,uwb_policy,frequency,phy_interop,epuck_console,esp_link,mul_diag,ads_radio,
runtime_config); STM32 Universal build and reserved-flash ELF check;82 ESP/Python
host tests and ESP-IDF build. Fixed missing portable-header include paths in older
standalone host scripts instead of disabling tests. LPS final channel5 build:
text128312/data1128/bss15244,20 bytes static RAM margin. Compiler stack frames:
clock fitted observer296, solve128, ALR event256 bytes; task reservations unchanged.
These static figures are not runtime stack high-water or timing measurements.
STM32 RAM38744 bytes/128KiB, flash142588/768KiB. ESP app0xC8160/0x100000 fits.

`mul_bias_report.py` compares per-device/per-slot validity, means/spread and optional
measured-geometry residuals. It rank-checks device+slot effects and refuses to infer
a clock slope from a fixed device/slot assignment. The supplied09:26 capture gives
means-142.95/-196.34/-234.93mm (n165/182/177) and SD41.69/53.67/61.00mm; device and
slot are confounded. Permutation/geometry/invalid-entry tests pass. Clock replay
also accepts compact/raw type10 JSONL, correctly using the radio reference sequence
rather than transport sequence, and resetting on clock-domain epoch changes.

## Stage C next implementation design (not yet implemented)

Use an explicit experimental schedule profile3 and schedule codec version2, retaining
32 bytes by assigning old reserved20..27 to minimum TX lead_us, processing budget_us,
packet airtime_us (uint16 each), guard in100us units, and ranging flags. Profiles1/2
retain version1 bytes/constraints/defaults. Profile3 radio frames must explicitly
use protocol version3 plus the full fingerprint; old participants reject them.
Numerical slot spacing remains integer milliseconds; feasible cycles can initially
use10ms resolution within100..2000ms. Legacy profile1 retains10/50/500 and1Hz.

Default experimental budgets should remain conservative;3/5/2ms spacing requires
explicit opt-in lead/processing/airtime budgets, with actual hardware deadline
checks before and after SPI retained. Never change the compatibility5ms guard.
Reference period, clock period and propagation sequence-age guards must all agree.
No fixed1Hz sequence thresholds may silently survive the new variable-period path.
A uint16 period can occupy existing padding in the80-byte clock ABI; investigate
an8-bit period/10ms field in the existing relation padding to keep its48-byte size
and128-byte LPS scratch assertion. Check these sizes in a clean embedded build.

Do not enable ALR–RLR flags before the assigned ranging adapter exists. AA disabled
must reserve no TWR windows; with AA enabled keep compatibility windows until the
StageD deterministic generator is integrated. New STM32 schema8 can carry verified
experimental schedule metadata and full numerical spacing in its extended record;
old128-byte packed slot fields must remain compatibility-only. Extend vector/raw
telemetry with an explicit experimental marker rather than reinterpreting old fields.
After StageC tests/builds/pin refresh proceed throughD–G without physical requests.

## Stage C portable timing checkpoint (firmware activation still blocked)

Implemented shared schema2/profile3 in the existing32-byte schedule payload:
20..21 TX lead_us,22..23 RX/processing budget_us,24..25 airtime_us,26 guard/100us,
27 AA-ranging flag. Profiles1/2 retain schema1 bytes and discrete compatibility
settings. Version3 multi radio envelopes bind the experimental schedule; version2
receivers reject them. Numeric math contexts accept1..65535ms, but legacy scheduled
packet encoding still rejects unrepresentable tags. Legacy raw EEPROM spacing is
still discrete. Profile3 has100..2000ms cycles at10ms resolution and feasibility
checks using explicit budgets;3ms with default5ms lead fails as intended. AA-off
allocates no maintenance owner; unsupported ALR–RLR flags are rejected.

Clock period uses former padding (clock remains80 bytes); period/10ms in relation
padding keeps it48 bytes and LPS scratch128 bytes. Propagation sequence-age guards
now use actual period; legacy decode/guards retain their previous policy. Explicit
period-aware relation decoding is required for experimental frames. The production
receiver core computes signed synthetic geometry at numerical spacings including
2/3/5/7/17/50/200/251ms and variable periods, including offsets above600ms. This is
synthetic relation-provider input, not active ranging/geometry firmware integration.
Shared sanitizer suite and230 native10ms tests passed. Added two stock snapshot
ownership tests. Stock TWR anchor/tag now copy only used address/list fields rather
than whole MUL config snapshots, preserving ownership and recovering SRAM.

Clean intermediate LPS build passes: text129576/data1136/bss15076. Map RAM1128+
13028+2048=16204/16384 (180 bytes margin). Flash image has only about360 bytes spare,
so further integration needs size work, not smaller queues/stacks. Experimental RF
is deliberately rejected in `uwbControlCanEnable` until the new radio adapters and
commands are connected; do not describe Stage C as a completed firmware feature.

Next concrete actions:
1. Wire profile3 classification into ALR/TWR/reference-period/follower-lead code;
   retain before/after-SPI guards using configured experimental lead only, default
   compatibility minimum unchanged. Override estimator period from schedule.
2. Add RF-OFF scalar setters/profile selection/preview and numerical spacing to
   LPS. Guard unsupported legacy tags. Preserve known100-byte EEPROM region.
3. Add STM32 schema8 extended experimental schedule decoding (restore full spacing
   from the verified extension, not old packed slot bits), matching setters and
   receiver configuration. Distinguish explicit clock defaults in serialized state.
4. Extend type8/type9 validation/collector with explicit experimental marker;
   old records remain restricted to their old constraints. Include actual clock
   sample period for replay. Pin only a committed shared revision into consumers.
5. Test actual adapters at3/5/2ms with simulated setup lag, missing packets, failed
   delayed-TX, period changes and power cuts; rebuild all targets. Then proceedD–G.

## LPS experimental adapter progress (Stage C consumers pending)

LPS now has opt-in `cfg set profile experimental`, numerical `slot_ms`,
`cycle_ms`100..2000 step10, `tx_lead_us`, `rx_budget_us`, `airtime_us`, `guard_us`
(step100) and `alr_alr_twr on/off`, all RF-OFF-only. `cfg roster show` includes
budgets/guard/AA flag and experimental marker. Default experimental budgets are
5ms lead+2ms processing+1ms airtime+100us guard, so3ms is rejected unless explicitly
lowering budgets. Example3ms software model: lead1000/rx500/air1000/guard100us.
Actual minimum-lead checks before and after SPI remain; simulated3ms SPI delay
still skips a short slot without transmitting. Only known fast PHY is permitted
for experimental activation (the CH5 build forces it; non-CH5 long/low-rate modes
fail closed). These are configured software budgets, not measured capabilities.

Actual ALR tests cover2/3/5/7/17ms spacing,100/200/1000/2000ms reference periods,
all8 slot offsets, clock period consistency, after-SPI deadline failure and no TWR
activity at old fixed windows when AA is disabled.234 native tests pass. Default
protocol1/2 behavior is unchanged; experimental metadata has version3 envelopes.
Current LPS build text129104/data1136/bss15076 fits. Application/HAL/USB middleware
and standard kernel C use-Os; architecture-specific RTOS port and radio driver
retain-O3. No stack/queue reductions. Stock TWR config snapshots remain owned and
cover precisely the fields those engines use, preserving mode behavior.

The shared type8/type9 codec now requires quality marker3 for numerical experimental
spacing/cycles; old unmarked records retain their constraints. Type10 clock-record
version2 adds explicit capture-age at90..91, version tag2 at88 (89 reserved), to
avoid replaying invalid RX captures as valid. New decoders must also accept old
version1 records with zero88..149, labeling capture age unknown. Clock sample period
uses the common transaction field in version2 (0 means compatibility1000ms).
Consumer parser/publication update and STM32 schema8 are next; no StageC completion
claim until the complete compatible revision set/builds/replays pass.

Clock replay refinement: production-C replay now honors recorded sample_period_ms
and capture_age_ms. Age>15ms is ignored by the estimator, preserving the previous
mapping without refreshing it (not forcing a reset); old records retain unknown
capture validity and use CLI period. Offline polynomial diagnostics discard stale
captures too. Five replay tests and full portable sanitizer suite pass, including
100/500/2000ms input and explicit type10 v1/v2 codec compatibility.

## Stage C integrated checkpoint (2026-10-26)

This section supersedes the pending-consumer instructions above. Compatible set:
LPS canonical core `a36af2b` (subsequent LPS-only mode6 fit fix uses same wire),
STM32 `80e1ce0`, ESP32 `1373433` (clock parser at220a64f, numerical decode3774790).
All are pushed to their origin branches. Vendor snapshots deterministically pin
`a36af2b02f91010cfe8ee4f819e6b175564d5cb2`; do not hand-edit vendor files.

STM32 now supports profile3 with all RF-OFF scalar timing setters, matching
fingerprints, actual cycle period in receiver/clock config, preserved profile3
roster edits and explicit compatibility return validation. Schema8 uses256 bytes:
schedule128..159, clock160..171, explicit-clock flag172, zero173..247, CRC248,
commit252 last. Old schemas1..7 remain readable with old restrictions. Numerical
spacing is restored from verified extension before alias validation; implicit
clock settings remain implicit after reboot. Tests cover all64-word append cuts,
sector rotation cuts, N2/4/8 implicit/explicit clocks, CRC-valid unknown markers,
malformed/no-write settings and public128-byte decoder rejection.

Production STM32 fake-driver scenarios run2/3/5/7/17ms spacing with100/200/500/
1000/2000ms periods, injected ALR/RLR skew and signed geometry; persisted settings,
fit acquisition, passive zero-TX behavior, full80-byte follower preservation and
bad-capture recovery are checked. Age>15ms at a reference resets the *receiver*
clock, with reacquisition after valid samples. The estimator itself ignores an
invalid observation without refreshing mapping; actual JSON mapping-epoch changes
preserve receiver resets on replay. This distinction matters when extending replay.
No buffer overflow was found: follower broadcasts are80 bytes, TWR is90, existing
84-byte LPS-compatible diagnostic storage remains appropriate for current RX paths.

Type10 v2 publication includes actual period and age (UINT16_MAX for invalid
hardware capture); ESP preserves v1 unknown age/period and v2 explicit validity.
Type8/9 quality3 marks experimental timing; compact vectors include profile3 and
actual spacing/cycle. Full85-test ESP host suite passes. Bounded software bridge
replay passes87 experimental clock/raw/vector records through actual ESP parser,
queues, TCP reconnect and compact logger, including invalid capture preservation;
old80-vector replay for N2/3/4/8 also passes. Full175-reference simulation replay
recovers injected15000ppb; this is simulation, not hardware accuracy evidence.

LPS mode6 legacy RLR now also uses configured regression via the existing typed
workspace (default median), retaining no-TX behavior; new actual-adapter45-cycle
window30 test passes. LPS235 native10ms tests pass after this addition; immediately
prior full matrix234 native+210 sanitizer tests per10/50/500 passed. Run full matrix
again if changing radio code. Portable sanitizer suite and31 Python tool tests pass.
All STM32 suites passed (runtime_config,spi_polling,uwb_policy,phy_interop,frequency,
epuck_console,esp_link,ads_radio,mul_diag); Universal RAM38800/128KiB,
flash145636/768KiB; ELF load segments/symbols clear config sectors. ESP build fits.
LPS clean CH5/6s latest text129160/data1136/bss15076; before mode6 fit addition,
clean default PHY text128848/data1136/bss15076. No stack/queue cuts. Resource
margin remains tight: more LPS features require explicit size/map review.

Reproduce adapter traces (software only, no serial/radio endpoints):
```
# epuck-stm32
MUL_TIMING_TRACE=/tmp/timing.uart MUL_MULTI_TRACE=/tmp/compat.uart sh tests/runtime_config/run.sh
# epuck-esp32
python3 tools/verify_multi_replay.py --experimental /tmp/timing.uart
python3 tools/verify_multi_replay.py /tmp/compat.uart
```
Logs used this run: /tmp/mul-c-stm-final-check.log,
/tmp/mul-c-experimental-bridge.log,/tmp/mul-c-compat-bridge.log,
/tmp/mul-c-lps-matrix.log,/tmp/mul-c-lps-final2-build.log,
/tmp/mul-c-esp-numeric.log. Never operate hardware based on test harness output.

Next: StageD configurable20/10/5ms TWR reply and deterministic scheduler. Actual
five-message radio state machine currently has fixed20ms replies,5ms TX lead,
50ms step deadline,20ms final-window guard and150ms650/800 windows. Native scheduler
must derive all reservations, report infeasible per-link targets and preserve
compatibility; adapter changes must not lower lead implicitly. LPS EEPROM remains
known100-byte region; source uses16-bit I2C addresses/32-byte pages but does not
identify the physical capacity. Do not enlarge allocation without evidence.

StageF audit discovery: real step counters already exist in STM32 epuck_link.c:
fields20/21 populate candidate.steps_left/right (lines411..412), SENSOR output
exports them (around819). Inspect source PIC time_ms, rollover and transport
before defining EKF input. Commanded motor steps/s remain distinct from measured
step counts. Remaining stagesD/E/F/G are not complete; no hardware stop requested.

## Stage D portable planner checkpoint (live integration NOT complete)

Added canonical mul_ranging_plan.[ch], tests/test_ranging_plan.c, production-C
preview bridge tools/test/ranging_plan_preview.c, tools/mul_schedule_preview.py,
example JSON and three Python tests. The planner/config codec is separately
versioned64 bytes, not exported into current consumer pins or EEPROM. No live
radio path uses it yet; no reply-delay firmware setter is claimed implemented.
Default deployed timing remains20ms replies/fixed windows. The shared core has
new optional APIs only, so the StageC compatible revision set above remains valid.

Planner supports8 ALRs/8 RLRs, independent AA/AR flags, reference-follower AA pairs
and all ALR×RLR pairs,20/10/5ms candidates, per-pair integer milliHz, fractional
aggregate quotas and weighted class/pair rotation. Reservations include configured
initial TX setup, four replies, RESULT airtime/processing/guard; spread over usable
cycle, bounded by reference preparation, peak demand,10..80% utilization and32 ops.
Rates exceeding capacity are rejected, not silently degraded. Disabled types have
zero ranging reservations. Priorities beyond requested rates remain pending.

Native matrix1200 profiles:996 feasible/204 explicitly rejected. Tests check every
owner/overlap/cycle bound/utilization, deterministic rebuild, uint32 cycle rollover,
codecCRC and CRC-valid invalid fields, unchanged output on failure.2000-cycle
fairness gives exactly1000 AA measurements/link and4000 AR measurements/link for
AA0.5Hz/AR2Hz example; AA gaps<=2.2s in that simulation. Example20ms reply target
is infeasible and rejected. No actual radio state machine is exercised by this new
planner test; existing radio fault tests remain separate until adapters are wired.

Full canonical portable ASan/UBSan suite and34 Python tests pass. Latest LPS native
matrix after mode6 fix is235 tests+211 sanitizer tests at each10/50/500ms (check
/tmp/mul-d-lps-final-matrix.log final completion). Object-only Cortex-M0 -Os build
of planner: text2280/data0/bss0, largest individual function stack136 bytes;
call chains/other radio task usage still need integration review. Dependencies are
integer ABI division/multiply/memory/schedule only (no new FP). Current LPS image
has about776 flash bytes spare, so simply linking this planner cannot fit. Do not
claim embedded integration/resource acceptance from the object/native tests.

Immediate continuation, in order:
1. Review planner model/API and extend priority/freshness reporting if needed.
   Compare reservation formula against all actual mul_twr.c transitions; add
   simulated SPI/IRQ-delay/lost-message cancellation at reservation boundaries.
2. Reclaim LPS flash with measured build changes (consider optional LTO or carefully
   size-optimized C driver units; preserve TX lead checks, stacks, queues and modes).
   Review Makefile/linker + full clean builds/map/stack before wiring new objects.
3. Resolve compact persistent ranging settings under known100-byte LPS region,
   preserving existing fields. Source does not prove EEPROM capacity; do not extend
   blindly. Current64-byte planner wire may need a compact storage codec.
4. Replace fixed-window reservation selection in ALR owner and mul_twr.c using
   planner intervals, add configurable replies/timeouts/guard/lead that preserve
   legacy20/5/50ms behavior. New ownership requires schedule/RF versioning; current
   profile3 consumers reject unsupported flags. Do not merely run both schedulers.
5. Integrate persistent RLR roster/targets in STM32, deterministic matching plan,
   explicit radio ownership. Implement StageE compatible five-message ALR–RLR
   DW1000/DW3000 exchanges in assigned slots, quality/raw telemetry, fault recovery.
6. StageF real steps exist in epuck_link.c; trace sensor timestamp/rollover/transport
   then geometry/range provider and optional PC EKF. Finish StageG unified campaign.

Exact next smoke commands (read-only software):
```
python3 tools/mul_schedule_preview.py tools/examples/mul_ranging_plan.json --cycles 3
python3 -m unittest discover -s tools/test
sh portable/mul_core/tests/run.sh
```

At this context checkpoint, StageD is partial; E/F/G remain pending. Goal remains
active. No flash, robot motion, physical experiment or external message was sent.
Preserve unrelated docs/mul_core.md edits; STM32/ESP32 trees are committed/clean.

Final checkpoint verification: full LPS matrix completed235 native+211 sanitizer
checks at all10/50/500ms, with unsupported compile-time slot rejection. Clean
default-PHY build after legacy mode6 fit passes text128872/data1136/bss15076;
current bin artifacts are this default build, NOT the CH5 campaign image. Latest
CH5/6s build text129160/data1136/bss15076 also passed. No binary was deployed.
STM32 HEAD/origin main80e1ce0 and ESP32 HEAD/origin main1373433 agree and trees
are clean. The only intentionally unstaged file in LPS is the pre-existing
unrelated docs/mul_core.md edit. Planner APIs/tool/tests are committed in the
checkpoint containing this section; obtain its exact revision with git log -1.

## User clarification: MUL-only production and MCS-based Stage F

Source request: attachment8bbb0bdb-a15e-495e-9f24-4b5c58a8f385/Pasted text.txt.
The user's explicit new instruction supersedes earlier stock-mode preservation in
production. Keep original sources/history; prioritize cleanup before more D/E radio
features. No hardware authorization was added. StageF is MCS/ROS poses in a known
common frame, plus configurable reproducible synthetic noise and supplied known
covariance. SLAM/map alignment/uncertainty inference are OUT OF SCOPE.

Cleanup implemented: Make production is MUL_ONLY=1, excludes stock TWR anchor/tag,
sniffer, TDoA2/3, LPP, pressure sensor startup and factory-test handlers. No upstream
source deleted. Original combined build remains Git fd44002 (already published).
Current target rejects MUL_ONLY=0: when tested, a combined build with explicit new
workspace overflowed SRAM by164 bytes. Do not reintroduce the inactive-context
memory overlay to make this optional build fit. Use the historical revision for
that image; current native stock-source regression tests still pass independently.

Explicit344-byte static MUL workspace replaces all TDoA3 overlay use. TDoA3 source
now owns its original plain ctx and is absent from production. Production config
omits old anchor-list/position fields; EEPROM data/field IDs remain readable and
are not deleted or migrated. Stored0..4 and invalid modes including200/255 cannot
request RF; mode edits reject removed values with no write. New uninitialized
config defaults idle rather than a stock transmitter. Main menu lists5/6 only;
old stock-mode aliases/binary/factory commands are compiled out. PHY/power controls,
antenna timing, drivers, diagnostics, MUL TWR and passive RLR remain.

Task audit: only shared main/console and UWB owner tasks plus kernel idle are
needed and retained. No stock-mode-specific task was present. Timers were already
disabled; unused callback is excluded. UWB1600/main800/idle800 stacks, USB TX512
queue and useful startup diagnostic1024 buffer remain unchanged. No stack or queue
cut accounts for savings. Explicit production image checker verifies symbol
absence/presence, owned workspace, retained allocations and linked flash/RAM bounds.

Final clean CH5/6s build: text116336/data1056/bss11780; actual flash117392 and
RAM(data+bss+2048 linker stack)12828, free flash13680/RAM3556. Against prior combined
CH5 image: savings12904 flash and3376 RAM bytes. Clean default PHY image also passes:
text116048/data1056/bss11780; flash117104, free13968. No LTO needed. These measured
margins remove the prior planner-integration flash blocker; continue D/E now.

MUL production matrix:212 native and212 ASan/UBSan tests each at10/50/500ms, real
radio/EEPROM/console code, OBS bounds and invalid compile-time slot checks. After
adding final invalid-mode200/255 guards, production10ms native+sanitizer rerun
passes212/212. Original/native combined API suite236 tests at10ms also passes.
The test tool now cleans result files between profiles: the previous unclean
filtered summary included24 stale stock results; reported production counts above
are from isolated clean runs. Compiler build/profile/header dependencies avoid
retaining old combined-image objects. No hardware was operated.

Reproduce:
```
make clean
make -j4 REV=B BOOTLOAD=0 EXTRA_CFLAGS='-DMUL_HYBRID_CH5=1 -DMUL_PROP_MAX_AGE_MS=6000'
python3 tools/test/check_mul_image.py bin/lps-node-firmware.elf
python3 tools/test/mul_slots.py --mul-only --sanitize --prop-age 6000
python3 tools/test/mul_slots.py --slot 10 --prop-age 6000
```
Logs: /tmp/mul-only-final-clean-build.log,/tmp/mul-only-final-resources.json,
/tmp/mul-only-default-build.log,/tmp/mul-only-matrix.log,
/tmp/mul-only-guards-tests.log,/tmp/mul-only-reference-native.log.
Current bin is CH5/6s build; it was not flashed. Unrelated docs/mul_core.md remains
untouched/unstaged. STM32/ESP32 remain clean at80e1ce0/1373433, same wire pin a36af2b.

Updated Stage F acceptance/work plan:
- ROS-compatible timestamped ALR pose ingress, known robot/frame IDs, explicit
  freshness/validity. Same replaceable observation interface for future pose sources.
- Deterministic seeded independent Gaussian noise initially; optional correlated
  noise. Publish supplied covariance and noise metadata; preserve clean MCS truth
  in a strictly separate evaluation/log stream. No truth fallback on missing noise.
- Noisy geometry -> pair distances/delay with covariance, timestamp consistency,
  stale/missing rejection. Explicit source none/TWR/MCS-noisy. No fusion required.
- Measured wheel steps from epuck_link (fields20/21) need timestamp/rollover audit.
  Portable host-tested state[x,y,theta] EKF should execute on ESP32 if practical;
  keep STM32 timing-critical path lightweight. Raw UWB/steps retained, default off.
- Measurement uncertainty includes supplied noisy ALR covariance and common-anchor
  correlations; asynchronous/gated updates, missing/outlier handling, deterministic
  replay, compare TDoA-only/TDoA+TWR/EKF. Do not estimate frame alignment or SLAM error.
- Continue scheduler/reply/active ALR–RLR integration first, then MCS-noise/geometry/
  odometry/EKF and consolidated A–G campaign. No intermediate hardware checkpoint.

## Persistence prerequisite after production cleanup

Commit188927f (pushed) is the MUL-only cleanup and StageF clarification checkpoint.
Subsequent EEPROM fix removes a real error-masking bug: old eepromWrite could fail
an early32-byte page, continue, then return true when a later page succeeded.
Writer now stops immediately on first failure; tests verify no following bus write.
Read/write guard negative/out-of-range/overflow/NULL inputs before bus access,
zero-length no-ops and last-device-byte operations. No larger config image is used.
New TestMulEeprom uses the real production driver with a simulated I2C device and
fault injection at all4 page boundaries, including unaligned1/32/32/1 splits.
216 production native+216 ASan/UBSan tests pass at10ms; firmware builds and image
checker pass. Latest CH5 image after driver fix flash117384/free13688, RAM12828/
free3556. No new global allocation; no radio or hardware operation occurred.

Primary hardware evidence found (supersedes earlier "capacity unknown" limitation):
- Bitcraze Rev-D schematic labels U6 as24AA64FT-E/OT:
  https://www.bitcraze.io/documentation/hardware/loco_node/loco_node_revd.pdf
- Microchip datasheet specifies8K x8-bit memory and32-byte pages:
  https://ww1.microchip.com/downloads/aemDocuments/documents/MPD/ProductDocuments/DataSheets/24AA64-24FC64-24LC64-64-Kbit-I2C-Serial-EEPROM-DS20001189.pdf
This supports an explicit board-profile storage design, not a physical inspection
of the user's nodes or blanket proof across unknown revisions. Header driver bound
is8192; current cfg.c still reads/writes only original100 bytes. No extension area
has been touched. Do not confuse firmware REV=B toolchain selection with verified
physical PCB revision. A future extended scheduler record should use its own
version/CRC/commit-last/readback/rollback contract and preserve0..99 unchanged,
rather than blindly growing the old config buffer and its main-task stack copies.

Continue with D/E radio integration now that flash/RAM are available. A useful
next coherent unit is actual profile3 TWR reply20/10/5 selection with explicit
lead/processing/airtime/guard validation, preserved compatibility20/5/50 defaults,
and real adapter delayed-TX/fault tests. Then bind the deterministic planner,
RLR roster/targets and versioned persistent plan to the radio owner instead of
running fixed and generated reservations together. No pause for hardware required.

## Stage D actual reply-interval integration

LPS actual five-message TWR now uses profile3 configured20/10/5ms replies; the
schedule stores selector in formerly reserved flags bits1..2 (code0/1/2,3 invalid),
bit0AA retained. This is fingerprint-bound and old parsers reject new bits. Codec2
and RF envelope3 remain; no silent old-node compatibility claim. Native tests
prove defaults20, invalid selectors, budget rejection and fingerprint/codec binding.
LPS RF-OFF `cfg set twr_reply_ms` persists through existing32-byte schedule TLV;
AA on/off preserves reply bits. Preview/short USB help lines report the interval.

Actual adapter min lead follows explicit experimental txLeadUs, checked before
and after SPI; default/legacy profiles retain5ms. Experimental step deadline is
reply+ceil(airtime+processing+guard), and final-window guard is that rounded tail.
Historical20/5/50ms timing remains exactly for profiles1/2. Fixed150ms AA windows
still own operations; automatic planner/ALR–RLR integration NOT complete.

Audit found extra fixed20ms assumptions: math interval acceptance15..30ms and
RESULT interval30..60ms. New mulTwrCalculateReply/mulTwrResultsReply scale those
bounds for5/10/20 while old API wrappers call20 and retain acceptance. Equations,
antenna timestamps and power correction unchanged; interval<=30ms still bounds
64-bit products. New fake-radio tests exercise all five real messages at20/10/5ms
with0/±150000ppb independent clocks, ToF, exact reply markers, configured500us lead,
2ms processing budget,4ms SPI injected failure, timeout and next-cycle recovery.
5ms math is accepted only by explicit API, rejected by old20ms wrapper.

Portable sanitizer suite passes;221 production native+221 sanitizer tests pass10ms.
Clean CH5 image text117168/data1056/bss11780; flash118224/free12848,
RAM12828/free3556. No new state/stack/queue allocation. Logs:
/tmp/mul-reply-final-tests.log,/tmp/mul-reply-core-final.log,
/tmp/mul-reply-build.log,/tmp/mul-reply-resources.json.
Next: commit/export shared core to STM32/ESP, add STM RF-OFF matching reply setter/
preview/persistence/guards so its passive schedule fingerprint follows ALRs. Then
connect versioned persistent plan and generated radio ownership, plus StageE RLR TX.

## Reply timing compatible revision set and final verification

LPS `e7f3518` is published; STM32 `09f7300` and ESP32 `b1353a9` import its canonical
core deterministically (verified UPSTREAM pin e7f351836c74a88453d843825365393b16f21e50).
STM32 adds matching RF-OFF twr_reply_ms scalar, preview, budget validation and
schema8 persistence. AA toggles preserve selector. All64-word append/rotation
power-cut tests exercise5ms settings; actual passive RX adapter uses persisted5ms
fingerprints across2/3/5/7/17ms broadcasts and100..2000ms cycles, with0 RLR TX.
ESP imports shared codec/math/planner without changing telemetry layouts;85-test
host suite and IDF build pass. Actual cross-platform bridge replay passes87 raw/
clock/vector records including invalid captures, reconnect and config fingerprints.

Latest verification: LPS221 production native+221 ASan/UBSan tests at each10/50/
500ms; portable suite passed. The full legacy/native-source API suite245 tests
at10ms passes after correcting old TestEeprom read fixtures: address0x4711 was
outside the8KiB device and its4711-byte length exceeded its96-byte array; successful
I2C mocks now use valid address0x0711 and sizeof(data). Out-of-range inputs remain
covered explicitly in TestMulEeprom and guards were NOT relaxed to satisfy mocks.

Clean CH5 LPS: text117168,data1056,bss11780; linked flash118224/free12848,
RAM12828/free3556. STM32 Universal RAM38800,flash146100 and ELF sectors checked.
ESP app0xc8260 in1MiB partition (22% free). These are software builds, not hardware
performance or measured short-reply interoperability. No device was opened/flashed.

Logs:/tmp/mul-reply-final-tests.log,/tmp/mul-reply-matrix.log,
/tmp/mul-reply-reference-native.log,/tmp/mul-reply-core-final.log,
/tmp/mul-reply-stm-final-tests.log,/tmp/mul-reply-stm-build.log,
/tmp/mul-reply-esp-tests.log,/tmp/mul-reply-esp-build.log,
/tmp/mul-reply-bridge.log. Current bin artifacts remain CH5/6s; no flash action.

Immediate next work is the rest of D/E, not another reply-only refactor:
- Define versioned persistent full ranging plan (RLR roster, per-pair target rates,
  priorities/freshness), preserving old100-byte EEPROM image. External chip design
  evidence now supports a separately bounded region with explicit board assumption;
  implement commit-last/CRC/readback and interrupted-write rollback in native tests.
  Avoid growing cfg.c's100-byte buffer and its stack-local copies blindly.
- Bind full plan fingerprint and generated reservation ownership into actual ALR
  radio owner/mul_twr, with fresh version/envelope rejection by old participants;
  replace fixed windows, do not overlap two schedulers. Carry per-operation start,
  poll,end and participant pair through TWR guards/timeouts. Cache/precompute bounded
  timing outside deadline-critical callbacks where useful; native planner APIs are
  already available in all three core snapshots but NOT used by live firmware.
- Implement scheduled DW3000 RLR participation and compatible five-message protocol,
  multiple RLRs, raw/quality telemetry, lost-packet/deadline/CRC/cancellation recovery.
- StageF must follow user clarification above: MCS+seeded noise known common frame,
  separate pristine truth, geometry correction source none/TWR/MCS-noisy; measured
  step counters and optional portable ESP EKF, no SLAM/frame alignment. Then G.

Goal remains active and overall work incomplete. This is a verified context
checkpoint; user did not request a pause or a hardware checkpoint. Preserve the
unrelated docs/mul_core.md edits. Other worktrees are clean once pushes complete.
