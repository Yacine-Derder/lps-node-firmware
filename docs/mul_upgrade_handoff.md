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
