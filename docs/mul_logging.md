# Runtime verbosity and November 3 validation

The timing correction is recorded in [mul_tdma.md](mul_tdma.md#deadline-aware-follower-ar-preparation).
Plan codec5, policy2 and fingerprint **55ADA235** are unchanged. No RF packet,
clock mathematics, antenna calibration, reply time or roster is changed.
Offline timing simulations demonstrate additional preparation margin; they do
not demonstrate physical ranging reliability or measured board WCET.

## Commands and durability

On each LPS node, use newline-terminated USB commands:

```
:cfg set verbosity quiet
:cfg set verbosity normal
:cfg set verbosity debug
:cfg show
:diag
```

On STM32 use the same commands without the colon, plus `hybrid status`,
`hybrid diag`, `hybrid ar`, `clock show` and `tlm status` for counters.
STM32 retains `summary`=normal and `full`=debug aliases and numeric0/1/2.
Factory/legacy-unset LPS defaults and new STM32 factory defaults are normal.
Existing STM32 saved0/1/2 values retain their meaning; saved2 stays debug.

Changes while RF runs are immediate **RAM-only** and report `ACTIVE verbosity;
OFF to persist`. They do not stop/restart the radio, change epochs, clear machine
telemetry queues or write nonvolatile storage. Returning to the saved level
cancels the pending save. To persist, request OFF and wait for acknowledged radio
stop. LPS main then commits and reports `SAVED verbosity`; inspect `:cfg show`
(`pending=0`). STM32 OFF commits the active configuration through its existing
journal; inspect `cfg show saved`. A reset before persistence restores the old
saved level. Flash programming during a radio deadline is deliberately avoided.
Repeated already-saved requests do not write again. Storage uncertainty never
reports successful persistence or bypasses RF-OFF gates.

LPS reuses the existing24-byte, two-slot antenna journal (384/416,32-byte
stride). Version1 means normal; version2 uses its formerly reserved byte5 for
quiet/debug. CRC, generation ordering, inactive-slot commit and full readback
remain intact. TX/RX values are copied unchanged; the legacy TLV, plan slots,
geometry source and clock configuration are untouched. Boot does not migrate or
write. Interrupted-byte and lost-commit-ack tests retain an old or new complete
record. **Before downgrading LPS firmware, save normal while OFF**: this writes a
version1 record readable by the old implementation. Old firmware cannot decode
version2 and could otherwise select an older antenna record. Never erase EEPROM
as a logging migration step. STM32 uses its existing verbosity field and schema;
no duplicate field or configuration-sector migration is introduced.

## Output policy and accounting

| Level | LPS | STM32 RLR |
|---|---|---|
|quiet|No SL1 production or successful TWR text; errors/control replies retained; `:diag` counters|No optional human packet traces/summaries; on-demand reception/clock/AR/transport counters|
|normal|Concise legacy-compatible TWR result lines;12 small counter lines every5seconds when output drains; no SL1|Periodic reception summary in safe idle phase; concise standalone TWR results; no legacy per-packet OBS text|
|debug|SL1 e1..7 including preparation, mapping, radio arm and actual POLL; normal results/counters|Detailed legacy OBS outside RX dispatch; existing on-demand frame/clock diagnostics; all structured records|

Errors can still be lost under output pressure. Absence of a printed error is
never evidence of absence of failures. LPS `LOG` counters are boot-cumulative:
`tx_ok`, `tx_fail`, `slot_miss` (accepted reference whose follower TX could not
arm), `kalman` (completed worker), `postpone` (once per deferred observation),
`backlog`, `overrun` (worker exceeds configured allowance), `prep_fail`, `twr_ok`,
`twr_fail`, `dropped` (queue overflow), `filtered` (queued record discarded after
level reduction). SL1 `z` now reports aggregate human-log overflow. Timing fields
and the four-part SL1 envelope remain compatible with `analyze_mul_timing.py`.
Counters wrap uint32 and are not cleared by a level change. Quiet does not produce
optional success/timing records; it still counts every outcome. Filtered pending
SL1 parts may form an expected partial record at a level-change boundary.

The LPS existing four-record ring and one pending record are reused for human
TWR results and timing records. All formatting/division/USB output happens in
main, with bounded atomic writes of at most127bytes and retry on backpressure.
No blocking success/error `printf` remains in the TWR owner. Debug-only hardware
timestamps are evaluated only at debug; clock overruns are counted at every
level. Optional AA1 is also runtime-debug gated, with its own existing loss
counter. AA1 and SL1 are separate build variants on this constrained target.

**Verbosity never filters STM32 machine UWB packets, TDoA vectors, raw RX/TX,
clock snapshots, replay fragments, quality records, health/drop reports or
transport sequence numbers.** `samples`, `telemetry` and Wi-Fi settings remain
independent, unchanged controls. Quiet cannot guarantee a replay-complete capture
if mandatory/research telemetry already saturates UART or network queues. Keep
checking the replay validator and transport counters at every level. There is no
ESP32 firmware/protocol change.

## Builds and resources

Production SL1 capability is now present by default, dormant until debug:

```sh
cd ~/Documents/lps-node-firmware
make -j4 -f profiles/stationary-ch5.mk
python3 tools/test/check_mul_image.py bin/lps-node-firmware.elf
```

The profile explicitly retains stationary channel5,6000ms propagation age and
zero separation speed. It is justified only for the stationary experiment.
No active plan or device setting is changed by building.

| Image | Flash /131072 | RAM /16384, including stack reservations |
|---|---:|---:|
|stationary production, SL1 available|130992 (80free)|14876 (1508free)|
|separate AA1 (`MUL_SLOT_TRACE=0 MUL_TWR_TRACE=1`)|130624 (448free)|15508 (876free)|

Both SL1 and AA1 together exceed Flash and are rejected by the linker. For the
optional AA1-specific investigation, explicitly build the separate variant:

```sh
make -j4 -f profiles/stationary-ch5.mk EXTRA_CFLAGS='-DMUL_SLOT_TRACE=0 -DMUL_TWR_TRACE=1'
```

Redundant menu/startup text is shortened, with all commands retained. Targeted
LTO shares helpers among console/storage/logging, radio adapters and driver;
clock-estimator object compilation and numerical flags are unchanged. The image
gate recognizes compiler `.constprop` clones while retaining symbol, stack,
workspace, Flash and RAM checks. No stack, USB queue, trace queue or clock
workspace reservation is reduced. Main/UWB stacks remain1600each, idle800,
linker MSP2048, USB queue512, clock workspace168bytes. Flash headroom is extremely
small; re-run the image gate for any flag/toolchain change.

Post-link stack reports show frames: main456, ALR event248, worker/wait328,
map296, preparation120, TWR event344, send328, success logging168 and console
command264bytes. These are individual frames, not total high-water marks;
caller/callee chains and interrupt stack must be considered. No board stack
watermark or physical timing measurement was performed.

LPS offline verification:288 production ASan/UBSan tests at each500/50/10ms
compile profile (including real-driver5/10ms TDMA tests),313 legacy/USB tests,
54 host tests and240000 bit-identical clock-state comparisons. Added coverage
includes log filtering/backpressure/overflow/maximum atomic line, persistent
migration and interrupted writes, RF-live no-write/no-restart, identical
broadcast bytes and complete Kalman state at quiet/normal/debug.

## Operator-only deployment and 30-second four-robot test

None of these hardware steps were executed by the agent.

1. With existing firmware, request OFF on ALR132/134/133 and RLR141; verify
   acknowledged stop and saved OFF. Retain configuration, plan/clock and antenna
   calibration readback. Do not reset configuration or change plan55ADA235.
2. Build the stationary SL1-enabled image above. Separately on each ALR, the
   operator may run `sudo make -f profiles/stationary-ch5.mk dfu`. This target
   builds the current artifact first. After restart, confirm still OFF, channel5,
   propagation age6000, correct address/slot, estimator and saved calibration.
3. Build STM32 using `cmake --preset Universal` then
   `cmake --build --preset Universal`; run
   `python3 tests/runtime_config/check_elf.py build/Universal/EPuck_STM32.elf`.
   Operator-only RLR programming:
   `STM32_Programmer_CLI -c port=SWD -d build/Universal/EPuck_STM32.elf -v -rst`.
   Program ELF segments; never mass-erase configuration sectors. ESP32 does not
   need reflashing.
4. While OFF, set debug on each ALR and normal on RLR, verify saved values. Check
   plan codec5/policy2/hash55ADA235, roster132/134/133, RLR141,200ms period,
   0/5/10ms slots,10ms Kalman allowance,24.1..114.1ms reservation,28.1ms POLL,
   reply20ms. Do not regenerate or reconfigure a matching plan.
5. Start passive host USB/Wi-Fi recording before the operator starts RF. After
   warmup, record a full30seconds (150cycle opportunities). Stop RF, acknowledge
   OFF, retain ALR logs plus raw/compact RLR JSONL and configuration readback.
6. Analyze ALR logs with `python3 tools/analyze_mul_timing.py --help` for syntax;
   inspect SL1 e3(worker),e5(mapping/software),e6(setup/arm),e7(POLL).
   All five pairs should each receive approximately30opportunities; tally
   successes and each failure separately, especially134->141/133->141
   `late`/`late_setup`. Require >=1000us configured/armed lead, unchanged POLL
   markers, no duplicate opportunity and no Kalman backlog/overrun. Missing
   text/opportunities must remain marked unobserved, not treated as success.
7. Run the existing replay validator on the RLR recording. Check5Hz vectors,
   reference/follower sequence coverage, current relation labels, clock validity,
   exact replay matches and every transport/diagnostic drop/gap. Exclude neither
   connection boundaries nor losses silently. Compare availability with baseline.
   Then repeat at normal/quiet for bandwidth effects, using counters to assess
   missed slots/TWR outcomes. Debug traces themselves may saturate host logging;
   counters and structured telemetry remain the source of loss accounting.
