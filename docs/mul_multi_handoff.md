# Multi-ALR software handoff — 2026-10-23

## Result and scope

The software path is connected across all three repositories: persistent ordered
ALR schedules, LPS hardware-delayed slots and serialized TWR, STM32 independent
partial-cycle reconstruction, bounded UART vectors, ESP priority forwarding,
and self-describing Python JSONL. N=2/3/4/8 have deterministic software coverage.
No physical device was flashed, erased, configured or operated. No radio or robot
experiment was performed. User-reported two-ALR10ms accuracy (about5–7cm RMSE)
is a regression target, not a newly established result.

The pre-existing user change in `lps-node-firmware/docs/mul_core.md` was preserved
and excluded from every commit. Work stayed on existing `master`/`main` branches;
only each user's `origin` was pushed, without force.

## Checkpoint commits and push status

Implementation commits below were pushed successfully. The final documentation
commit containing this document follows the listed LPS implementation head; use
`git log -1` to identify that documentation-only head.

| Repository/branch | Commits (newest relevant implementation first) | Status |
|---|---|---|
| LPS/master | `2512a77` schedule controls/alias guards; `fbe9c2e` bounded vector telemetry; `37b424b` actual ALR TDMA/TWR; `a7d79ac` partial-cycle reconstruction/math; `46ef992` radio codecs; `eb0dbd4` EEPROM/CLI; `73f1754` schedule/architecture | Pushed to origin/master |
| STM32/main | `47ad135` RF-OFF endpoint guard; `b6ff9ab` actual multi RLR adapter/vector queues; `f6da579` shared receiver import; `6c3a4c8` persistent schema6/configuration | Pushed to origin/main |
| ESP32/main | `e83638f` vector live display/experiment metadata; `f08ae30` priority bridge/collector/TCP replay; `0f07a27` shared schedule/receiver import | Pushed to origin/main |

Both vendor snapshots pin canonical core
`fbe9c2ee1f39aca5fcc4fbf6d9774155607ced54`. Later LPS changes only affect its
adapter/CLI/docs; they do not change the shared snapshot. `UPSTREAM.json` records
per-file SHA256 hashes. Exporter: `tools/mul_core_vendor.py` in LPS. No build fetches
code or depends on a sibling checkout.

## Architecture and implemented functionality

- Ordered2..8-ID roster, unique stable canonical UWB IDs0..254. Index0 is the
  logical reference; each local ALR derives its role/index from its ID. Addresses
  need not be sequential, and reference need not be0x84. RLR ID must be outside
  the roster. Legacy two-node structures remain intentionally two-node, with
  separate explicit multi APIs rather than changing their old interpretation.
- Profile1 preserves the old34/74/84-byte frames, slot encoding, fifth-cycle TWR
  and signed TDoA math. Two-ID roster selects profile1 by default. Profile2 uses
  reference40/follower80/TWR90-byte frames, excluding2-byte hardware FCS, all
  below127 bytes. Full source identity, common reference cycle, slot, version and
  deterministic schedule CRC bind each new frame. Old decoders reject new types.
- Base spacing10/50/500ms, cycle1000ms, nonzero revision. Feasibility requires
  `(N-1)*spacing+20 <= 630ms`;500ms therefore supports onlyN2. Other periods are
  explicitly rejected, because the clock estimator currently assumes1Hz.
- Each fresh reference triggers one follower delayed TX at index*spacing in the
  follower's own hardware clock. Raw DX_TIME and live antenna delay conventions
  are unchanged. Late slots are skipped; no immediate-TX fallback or catch-up.
- One reference TWR state machine services deterministic owners in windows
 650–800/800–950ms, with preparation at630/780ms. There are at most two distinct
  selected followers per cycle. A failed first window cannot block the second;
  absent owners are skipped. Seven healthy followers are serviced within four
  cycles. Each follower keeps its own clock/committed propagation correction.
  Data preparation at980ms has priority. Use the common6s correction-policy build.
- LPS persistence adds a canonical32-byte CRC32 schedule TLV inside the existing
  known100-byte EEPROM region. Missing TLV derives old settings without writing.
  EEPROM writes retain checksum/readback and fail-closed uncertainty handling;
  **this is not a power-cut rollback journal**. No undocumented EEPROM area is
  assumed available. Invalid edits do not modify the active schedule.
- STM32 old schemas1..5 retain128-byte records. Multi schema6 uses aligned256-byte
  records: old prefix plus32-byte schedule, outerCRC and final commit word. Both
  banks scan mixed record sizes safely; every word-cut/rotation rollback is tested.
  Multi roster does not consume the ADS peer/calibration table. RF OFF is required
  for roster, spacing, period, revision, profile and reference/follower edits.
- STM32 uses fixed storage for8 observations,7 relations/epochs/values and one
  reference receiver-rate model. Association handles missing/reordered/duplicate/
  stale packets and32/40-bit rollover. Each follower retains its independent
  status/reason and numeric validity. Earlier valid siblings are frozen while
  waiting for later slots. Vector closes20ms after the final configured slot or
  on a new cycle. Missing reference invalidates the current differences; no stale
  numeric values masquerade as new measurements.
- `hybrid_static` explicitly assumes stationary receiver rate. Ordinary `hybrid`
  preserves NO_RATE. Multi RLR derives correction from each follower's bound
  relation publication; it does not need to transmit or infer TWR from a lone
  RESULT frame. All radio callbacks re-arm RX before math/telemetry.
- UWB3 type8 carries one N−1 vector in the existing152-byte/309-character bound:
  cycle, roster, revision/hash, RX time, period/spacing, seen/valid masks and all
  follower values/status/reasons. Invalid integers decode to JSON null. Type9 is
  optional raw40/80/90-byte evidence. Types1..7 remain unchanged. STM32/ESP queues
  prioritize vectors over raw packets/summaries. Collector compact vectors include
  their full roster/metadata, and live display shows each follower separately.
- Optional `--metadata experiment.json` retains experiment ID, positions and ground
  truth supplied by the user. It does not influence measurements or devices.

Exact persistence/radio/telemetry byte layouts are in
[mul_multi_alr.md](mul_multi_alr.md). Commands are in
[mul_commands.md](mul_commands.md), STM32 `docs/runtime_uwb.md`, and ESP32
`docs/uwb_logging.md`.

## Relevant changed files

| Area | Main files |
|---|---|
| Canonical portable core | `portable/mul_core/mul_schedule.*`, `mul_multi_protocol.*`, `mul_multi_receiver.*`, `mul_downlink.*`, `mul_localization.*`, `mul_telemetry.*`, corresponding tests/run.sh |
| LPS | `src/mul_alr.c`, `src/mul_twr.c`, `inc/mul_twr.h`, `inc/mul_role_state.h`, `src/cfg.c`, `src/uwb.c`, `src/uwb_control.c`, `src/mul_console.c`, wrappers, Makefile, native MUL/config tests |
| STM32 | `Core/Src/uwb_config.c`, `Core/Inc/uwb_config.h`, `Core/Src/uwb_runtime.c`, `Core/Src/mul_hybrid_radio.c`, CMakeLists, `tests/runtime_config/test_schedule_config.c`, `test_schedule_runtime.c`, `test_multi_hybrid.c`, vendor snapshot |
| ESP32/collector | `main/bridge_transport.c`, `tools/uwb_collect.py`, `uwb_multi.py`, `uwb_logging.py`, `verify_multi_replay.py`, `tests/test_uwb_multi.py`, transport/TCP fixture tests, vendor snapshot |

## Executed automated validation

From LPS:

```
sh portable/mul_core/tests/run.sh
python3 tools/test/mul_slots.py --sanitize --prop-age 6000
make clean
make -j4 REV=B BOOTLOAD=0 EXTRA_CFLAGS='-DMUL_PROP_MAX_AGE_MS=6000'
make clean
make -j4 REV=B BOOTLOAD=0 EXTRA_CFLAGS='-DMUL_HYBRID_CH5=1 -DMUL_PROP_MAX_AGE_MS=6000'
```

Passed: shared ASan/UBSan schedule/codecs/receiver/telemetry tests;226 native tests
and204 MUL ASan/UBSan tests at **each**500/50/10ms spacing, OBS output bounds and
unsupported-slot compile rejection. Actual LPS driver tests cover every follower
slot forN2/3/4/8, independent signed clock skews, both complete TWR windows,
cycle/timestamp wrap, missing first-window responses and next-broadcast priority.
Both normal channel2 and heterogeneous channel5 firmware builds passed. Existing
compiler warnings in HAL/FreeRTOS/startup/packed legacy code remain.

From STM32:

```
MUL_MULTI_TRACE=/tmp/multi.uart sh tests/runtime_config/run.sh
cmake --build build/Universal
python3 tests/runtime_config/check_elf.py build/Universal/EPuck_STM32.elf
```

Passed: legacy/current configuration/radio/USB/Wi-Fi/runtime regressions, schema6
N2/3/4/8 persistence, all64-word append and rotation power-cut positions, reboot,
invalid/no-write and rollback; actual multi RLR hardware-register adapter with
signed/zero geometry, independent clock rates, partial vectors, no-rate, cycle32/
uptime32/native40 wrap, Wi-Fi queue identity/priority and zero transmissions.
ELF checker confirms all file-backed load segments remain below0x080C0000 and
configuration sectors0x080C0000..0x08100000 remain reserved.

From ESP32:

```
sh tests/run_host.sh
python3 tools/verify_multi_replay.py /tmp/multi.uart
source ~/.espressif/tools/activate_idf_v6.1.sh
python3 "$IDF_PATH/tools/idf.py" build
```

Passed: C protocol/transport sanitizers,600-second saturated-queue simulation
(including vectors/invalid observations and uint32 wrap),80 Python tests, old log
compatibility, malformed CRC-valid new records, mixed validity, metadata/live
projection. Cross-repository replay passes80 production STM32 vectors through the
**real** ESP UART parser/owner/priority queues, loopback TCP reconnect, actual
collector and compact JSONL, comparing all IDs/masks/follower values. Firmware
build passes; app0xC7EA0 fits its0x100000 partition with22% free.

The simulation uses real production modules and fake hardware registers. Synthetic
relations/geometry drive the RLR; LPS TDMA/TWR is tested separately. It is not a
single RF/channel model running eight complete LPS instances together, and does
not establish physical synchronization or measurement accuracy.

## Memory and remaining limits

- LPS channel5 `size`: text128128, data1120, bss15228 (bss includes the2048-byte
  linker stack reservation). Link map: actual RAM .data1112 + .bss13180 +
  reserved2048 =16340/16384: **44 bytes static margin**. Initialized flash image
  fits128KiB. No stack/queue reservation was reduced; mode-exclusive RAM is reused.
  All application objects use-Os; HAL, RTOS and libdw1000 retain-O3. This is tight:
  future globals require a fresh map review. UWB task stack remains1600 bytes,
  main task800. A compiler stack-usage build was inspected (largest new frames:
  TWR event304, send280, broadcast encode264 bytes); this is not a measured runtime
  high-water guarantee. Verify stack/timing on hardware before extended operation.
- STM32 Universal: RAM38008/131072 bytes, FLASH137248/786432 bytes, no CCM use.
- N8/50ms data ends350ms; maximum guarded data end is630ms. All participants must
  use matching profile/roster/timing/revision and6s correction policy. Only1Hz is
  supported. A failed refresh may expire one follower's correction until recovery.
- LPS mode6 is intentionally legacy-only and fails closed for profile2. Multi RLR
  is STM32. ESP profile2 raw evidence currently emits accepted40/80-byte broadcasts;
  the type9 codec also fits90-byte TWR for future optional tracing.
- Last-reference/follower diagnostic snapshots do not retain all eight frames;
  use raw UART/JSONL evidence for deeper capture. Complete RF silence produces
  health/silence rather than fabricated measurement cycles.
- LPS EEPROM interrupted writes may require manual configuration recovery; unlike
  STM32 it cannot promise the previous committed settings survive. Uncertain or
  corrupt data is not silently accepted or automatically erased.
- No antenna recalibration, mobile receiver rate solution, bounded uncertainty,
  2D localization, dynamic membership/reference failover or larger-N transport is
  claimed. Those are outside this milestone. Current AVAILABLE is conditional on
  the stated stationary/model/age assumptions.

## Exact next steps and manual deployment sequence — NOT executed

No required software adapter remains intentionally disabled. The next required
acceptance is physical regression and three/four-ALR validation. Use a controlled
stationary setup, retain the existing firmware/settings backups, and keep RF OFF
while programming/configuring. Do not erase persistent configuration sectors.

1. Review `git status` in all three repos; retain the user's LPS doc edit. Re-run
   the commands above if rebuilding from a later revision. Use the channel5 LPS
   image and STM32 Universal image with identical6s correction policy. STM32 build
   can be configured with:

   ```
   cmake -S . -B build/Universal -G Ninja \
     -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake \
     -DCMAKE_BUILD_TYPE=Release -DMUL_UNIVERSAL=ON -DMUL_UWB_ONLY=ON \
     -DMUL_TWR_ROLE=0 -DMUL_HYBRID_SLOT_MS=10
   ```

   Then build/check as above. `Universal` is a build directory, not a named CMake
   preset. Inspect the existing cache before changing radio options.
2. Manually program the three images with the established board programmer:
   LPS `bin/lps-node-firmware.elf`, STM32 `build/Universal/EPuck_STM32.elf`, ESP32
   `build/comm_with_stm32.bin`. For an existing STM32CubeProgrammer setup,
   `STM32_Programmer_CLI -c port=SWD -d build/Universal/EPuck_STM32.elf -v -rst`
   programs the ELF load segments; **do not add mass erase**. For ESP32, activate
   IDF and run `idf.py -p PORT flash`, replacing PORT with its verified port.
   For an established LPS ST-Link/OpenOCD setup, `make flash REV=B BOOTLOAD=0`
   uses the existing ELF write/verify/reset target. It erases only internal image
   sectors, not the external configuration EEPROM. Do not run `make erase`.
   Verify board/bootloader layout before writing the BOOTLOAD=0 image at0x08000000;
   use the board's existing procedure if it has a different bootloader layout. No programmer
   command was issued in this task. After programming, inspect saved settings
   before enabling RF; older RF-enabled settings retain their migration behavior.
3. Existing ALRs132/133 already have their local IDs. A newly enrolled node0x86
   without an explicit roster can be prepared manually with `:off`, `:addr 0x86`,
   `:mul setup follower 0x84 0x86`, `:mode hybrid`, then a manual reboot. Address/
   mode changes require reboot; RF OFF persists. If a node already has an explicit
   roster, use roster commands instead of old setup/role aliases, and ensure its
   new local ID belongs to that roster before changing address. Read back ID and
   mode5 before continuing. Never guess an unknown board's local identity.
4. **Two-ALR regression:** On both LPS nodes, send each line separately:

   ```
   :off
   :status
   :cfg set slot_ms 10
   :cfg roster set 0x84 0x85
   :cfg set profile legacy
   :cfg set cycle_ms 1000
   :cfg set schedule_revision 1
   :cfg roster show
   ```

   Wait for `requested=0 running=0` before edits. On STM32:

   ```
   off
   peer add 0x84
   peer add 0x85
   mode hybrid_static
   off
   cfg roster set 0x84 0x85
   cfg set profile legacy
   cfg set slot_ms 10
   cfg roster show
   wifi on
   tlm on
   show
   ```

   Canonical `mode` selects mode without implicitly enabling RF; old `cfg set mode`
   retains its historical auto-start behavior. Reboot all three UWB participants
   while OFF and verify persisted roster/spacing. Start the read-only collector:
   `python3 tools/uwb_collect.py --robot 192.168.1.141:3333 --no-live` in ESP repo.
   Then manually enable the passive RLR (`on`), follower (`:on`), reference (`:on`).
   Allow clock warmup, inspect1Hz old `tdoa` JSONL and repeat the user's three known
   stationary positions. Compare sign, bias/spread/invalid bursts with the prior
   about5–7cm RMSE baseline. Verify TX packet/actual delta0 and no RLR TX. Stop all
   participants before proceeding if behavior regresses.
5. **Three ALRs:** Stop all with off; verify acknowledged OFF. On every ALR and
   STM32 set `cfg roster set 0x84 0x85 0x86` (prefix `:` on LPS), spacing10,
   cycle_ms1000, schedule_revision1. Use `cfg roster show` everywhere: profile2,
   count3, identical fingerprint/revision, offsets0/10/20ms, reference0x84. Do not
   select a legacy ALR role afterward. Reboot while OFF and read back again.
   Enable RLR, followers, reference in that order. Observe at least120 seconds:
   coherent `tdoa_vector`, two independent followers, expected1Hz cycle and
   warmup/recovery. Measure actual ALR/RLR positions; compare each signed value to
   `norm(RLR-ALR_i)-norm(RLR-reference)`, not ALR–ALR distance.
6. **Partial/failure checks atN3:** Manually stop only0x86. Its entries must become
   missing/invalid/null while0x85 remains independently usable. Resume it and verify
   recovery after synchronization. Stop/restart reference and check loss/warmup,
   with no stale valid vector. With RF OFF, temporarily change only0x86 revision
   to2, then resume it: its frames must be rejected without corrupting0x85. Restore
   revision1 with RF OFF. Record raw evidence if diagnosing any invalid bursts.
7. **Four ALRs:** Enroll0x87 analogously, stop all, set identical ordered roster
   `0x84 0x85 0x86 0x87`,10ms/1000ms/revision1. Verify offsets0/10/20/30ms and identical
   fingerprint after reboot. Start RLR/followers/reference and log at least five
   minutes, including a missing middle follower. Confirm three independent entries,
   fair serialized TWR ownership, timestamp rollover and bounded per-follower age.
   Repeat signed known-geometry checks. Do not infer2D position from this milestone.
8. Only after the above passes, considerN8 with the same binary and procedure,
   offsets0..70ms and four-cycle refresh coverage. Save logs plus optional
   experiment metadata, exact firmware hashes, roster/hash, settings, geometry,
   valid fractions, signed errors and TWR/clock diagnostics. End with all RF OFF.

If a physical test fails, the next software investigation should replay captured
40/80-byte frames/RX stamps through `mulMultiReceiverReceive`, compare relation
age and schedule CRC, then inspect actual delayed TX/maintenance timing and task
stack high-water. Do not tune offsets/calibration merely to force a geometry fit.
