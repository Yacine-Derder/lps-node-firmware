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
