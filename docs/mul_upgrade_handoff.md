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
