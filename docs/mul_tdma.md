# Configurable TDMA and one TWR opportunity per cycle

This is an **opt-in software-tested plan**, not an activation or a physical 5 Hz
validation. The saved/live `6B11782D` plan has not been changed. All nodes must
select the identical new plan before RF is enabled. Median-5 remains the default;
Kalman remains selectable and its dense double-precision mathematics is unchanged.

## Timing contract

Codec5 / policy2 uses four controls: `slot_ms`, `kalman_budget_ms`,
`twr_window_ms`, and `cycle_ms`. Broadcast order is roster order, with reference
at index0. Low-level lead, processing, airtime, reply and hardware guard remain
validated. The processing budget is an admission allowance, never a sleep.

For `N` ALRs, all quantities below are in microseconds:

```
broadcast_protected_end = (N-1)*slot_ms*1000 + air_us + process_us + guard_us
twr_start = broadcast_protected_end + kalman_budget_ms*1000 + 1000
poll = twr_start + lead_us + process_us + 1000
twr_end = twr_start + twr_window_ms*1000
remaining_guard = cycle_ms*1000 - twr_end
```

The additional1000us is the existing service/admission margin. The validator
requires at least20000us of remaining guard for next-reference preparation.
There is no new adjustable idle/guard-phase control. `guard_us` remains the
existing non-negotiable low-level radio allowance, not the remaining cycle time.
A complete exchange needs at least
`lead + 2*process + 1000 + 4*reply_ms*1000 + air + guard`.
At lead1000/process2000/air1000/guard100/reply20 that is87100us;90ms is accepted,
87ms is rejected. No reply/protocol message or antenna timestamp changes.

TDMA currently requires slot>=5ms and Kalman budget>=5ms. Existing low-level
lead+process+air+guard accounting still applies (4100us at the example settings).
Moving Kalman out of the critical path does not prove IRQ/SPI/frame preparation
fits3ms: there is no measured worst-case bound justifying that setting. Even
reducing `process_us` cannot bypass TDMA's explicit3ms rejection. Other feasible
slots (including10ms) and cycles200/250/500/1000ms are supported subject to the
complete phase and20ms tail tests.

## Independent worker and radio ownership

Each follower queues one immutable reference observation. Lightweight Begin/Prior
bookkeeping associates the current reference sequence/origins, epoch and validated
prior with that cycle's packet. Full Finish runs after **confirmed own TX** or a
safe TX abort/rearm. Merely arming delayed TX cannot release it. In TDMA it does
not wait for other followers' broadcasts; for example the5ms follower can finish
while the10ms follower broadcasts. Legacy plans retain their existing admission.

Admission reserves `kalman_budget_ms + 1ms`, accounts for rounded waits and time
spent planning, and checks local active/pending TX/TWR, the global TWR start/end,
and the next reference. No new estimator work is admitted inside the global TWR
reservation, including on nonparticipants. Insufficient slack postpones rather
than overwrites work. A worker held through TWR wakes immediately after the
reservation end, so larger configured budgets can use the remaining guard instead
of starving at the final20ms preparation boundary. The existing one-record backlog fails the new observation
closed, preserving order and refusing to label an old origin current. Epoch/reset,
reference loss and stale TX completion safeguards remain. Interrupts are not
masked for Kalman mathematics. No tasks, radio ownership or RF timings are added.

The prior measurement of approximately4.4ms motivated the10ms allowance; it is
not a physical WCET guarantee. Cortex-M0 emulation counts instructions, not board
wall time. At the example's nominal6ms/11ms completion service times, the global
24.1ms boundary leaves18.1ms/13.1ms respectively before TWR. With11ms reserved,
the latter has2.1ms scheduling margin. Excessive IRQ/service delay can still cause
postponement; no software test proves that every physical deadline will be met.

## Rotation and compatibility

Every valid cycle has exactly one operation, index0, successful or otherwise:

1. Reference -> followers in ascending ID order (AA).
2. ALRs in ascending ID order -> RLRs in ascending ID order (AR).

Only enabled classes participate; all relevant AA/AR pairs are enabled by explicit
TDMA conversion when the roster contains RLRs. No follower/follower AA is added.
The cycle's unsigned32 sequence modulo pair count selects the pair. Losses,
timeouts and radio errors never advance a local rotation counter or add retries.
At sequence wrap the modulo restarts deterministically; for pair counts that do
not divide2^32 a pair can repeat across wrap. All nodes still agree. Zero enabled
pairs are rejected explicitly. Pair order is independent of broadcast slot order.

Old rate-driven codec2/3/4 plans remain supported byte-for-byte; boot performs no
migration or writes. `plan mode tdma` is an explicit RF-OFF transaction. Codec5
reuses the two16-bit rate fields for budget/window, with a distinct version and
policy: old codecs cannot decode these semantics accidentally. Rates, priorities,
utilization and AA start-delay controls are rejected in TDMA. Conversion clears
those fields and chooses budget10 and a window of at least90ms (rounded up if the
retained low-level timings require more). Then set the four values explicitly.
Atomic LPS plan journaling and STM32 schema9 configuration journals retain their
CRC/readback/commit guarantees. All unrelated EEPROM values remain untouched.

RF remains **RF5**, with the existing full semantic plan fingerprint, source/role,
cycle/index checks and40/80-byte broadcasts /90-byte TWR frames. A different codec,
budget, roster, revision or timing produces a different fingerprint; mixed plans
fail closed. Both LPS ALRs and the STM32 RLR require the new firmware and matching
plan. Older firmware cannot safely participate in codec5. There is no automatic
fallback to a legacy rate schedule. For a rollback, keep RF OFF, issue `plan use off`, then `plan set policy fair`
using the new firmware. This explicitly writes codec2 with zero AA/AR rates
and utilization70, retaining roster/timing and all unrelated configuration. A
budget is never reinterpreted as a rate. Restore desired legacy values from the
saved readback, inspect/save and reboot before considering RF5-capable older
firmware. Deselecting codec5 alone does not make its stored bytes readable by an
old decoder. Do not erase calibration/configuration storage or downgrade to RF4.

ESP32 firmware requires no change: it forwards existing UWB3/RF5 data. Host tests
exercise codec5 snapshots through the actual STM32 encoder, ESP32 bridge, TCP,
collector and production-C replay. Configuration blobs remain opaque in transport;
the production decoder performs semantic validation. Existing replay schema3 and
historical recordings remain compatible. Full optional raw/replay instrumentation
has finite UART/queue capacity; 5 Hz with additional followers increases diagnostic
traffic. Dropped optional records must be reported, never treated as RF loss or as
proof of replay completeness. This campaign does not increase UART baud or hide
loss counters.

## Example and first five cycles

`tools/examples/mul_tdma_200ms.json` is the reproducible host definition. Revision1,
ALRs `[132,134,133]`, RLR `[141]`,5ms/10ms/90ms/200ms, lead1000/process2000/
air1000/guard100us, reply20ms gives fingerprint **55ADA235**. A different retained
revision or low-level option legitimately changes it.

| Cycle | Broadcasts (ALR:ms) | TWR pair | Reservation ms | Poll ms | Remaining guard ms |
|---:|---|---|---|---:|---:|
|0|132:0,134:5,133:10|132 ->133 AA|24.1..114.1|28.1|85.9|
|1|132:0,134:5,133:10|132 ->134 AA|24.1..114.1|28.1|85.9|
|2|132:0,134:5,133:10|132 ->141 AR|24.1..114.1|28.1|85.9|
|3|132:0,134:5,133:10|133 ->141 AR|24.1..114.1|28.1|85.9|
|4|132:0,134:5,133:10|134 ->141 AR|24.1..114.1|28.1|85.9|

At200ms each pair has one opportunity/second; at1000ms each has one/five seconds.
AA propagation age/uncertainty, AR range metadata and antenna calibration are
unchanged; missed AA calibrations can still expire under the existing policy.
Unlike old rate-driven placement, the rotation does not distribute operations
throughout the cycle or promise independently configurable pair rates.

## Build and operator procedure (not executed on hardware)

Offline preview:

```sh
cd ~/Documents/lps-node-firmware
python3 tools/mul_schedule_preview.py tools/examples/mul_tdma_200ms.json --cycles 5
```

Before either operator flashing command, issue OFF on every node with its current
firmware, confirm it has stopped and persisted OFF, and retain configuration and
`cal show` readback. Flashing must not restore an old RF-ON configuration.

LPS production and essential diagnostic builds (both use the explicitly stationary
channel5/6000ms, zero-relative-speed profile; this assumption is not for mobile
experiments without justified bounds):

```sh
make -f profiles/stationary-ch5.mk clean
make -j4 -f profiles/stationary-ch5.mk
# Optional essential timing build:
make -j4 -f profiles/stationary-ch5.mk EXTRA_CFLAGS=-DMUL_SLOT_TRACE=1
python3 tools/test/check_mul_image.py bin/lps-node-firmware.elf
# Operator ONLY, separately on ALR132/133/134; target rebuilds current artifact:
sudo make -f profiles/stationary-ch5.mk EXTRA_CFLAGS=-DMUL_SLOT_TRACE=1 dfu
```

STM32:

```sh
cd ~/Documents/epuck-stm32
cmake --preset Universal
cmake --build --preset Universal
python3 tests/runtime_config/check_elf.py build/Universal/EPuck_STM32.elf
# Operator ONLY, RLR141; load ELF segments, never mass-erase configuration sectors:
STM32_Programmer_CLI -c port=SWD -d build/Universal/EPuck_STM32.elf -v -rst
```

No ESP32 reflashing is required. Keep existing per-device addresses, roles,
antenna calibration and PHY. Build and compare options before connecting devices.

On **every** node first issue OFF and confirm stopped. LPS commands have `:`;
STM32 commands have no colon. Run `plan init` only if no saved plan exists.
Read `cal show` / configuration first and preserve the output. These are example
operator commands, not a script run by the agent:

```
:off
:plan show
# Only if absent: :plan init
:plan set lead_us 1000
:plan set process_us 2000
:plan set air_us 1000
:plan set guard_us 100
:plan set slot_ms 5
:plan alrs 132 134 133
:plan rlrs 141
:plan set reply_ms 20
:plan mode tdma
:plan set aa on
:plan set ar on
:plan set kalman_budget_ms 10
:plan set twr_window_ms 90
:plan set cycle_ms 200
:plan set revision 1
:plan use on
:cfg set clock_estimator kalman
:plan show
```

Each successful command saves atomically; there is no extra `save` command. Check
all replies, codec5/hash55ADA235, budgets, roster,24.1ms start and85.9ms guard.
On STM32 use `cfg set clock_estimator kalman` and the same unprefixed plan commands.
`plan show` previews cycles0..4; operation index is always0. Do not issue ON until
all four nodes agree. `plan use off` preserves the plan and returns to the retained
legacy schedule; that schedule must itself be checked before enabling RF.

## Essential diagnostics and physical acceptance

`MUL_SLOT_TRACE=1` emits nonblocking `SL1` records. Four bounded lines (`k=0..3`)
form one event; `a,b` encode the eight40-bit values. `c` is cycle, `l` local ID in
hex, `i,j` pair IDs in decimal, `e` event, `w` reason, `z` cumulative ring drops.
There are four queued records plus one immutable output record; new arrivals drop
when full, never overwrite a partially written record. Formatting/USB try-write
runs in main; radio code only copies bounded records. Trace-disabled calls evaluate
no arguments. Existing compact TWR range summaries remain for calibration tools;
large TX/KF/CLOCK/SYNC and six-stamp text dumps were removed. AA1 remains optional
for detailed TWR debugging, separate from the essential image.

|e|Meaning|t0,t1,t2,t3 (then t4..t7 where used)|w|
|---:|---|---|---|
|1|Follower slot path|RX,entry,decode,first check,prepared,armed,raw target,min lead|existing0..10 slot reasons|
|2|TX completion/abort|intended antenna TX,actual TX,0,0|0 exact,1 mismatch,2 timeout,3 radio abort|
|3|Deferred worker|start,end,reference TX,saved RX|0 completed|
|3|First postponement|now,wait slack DTU,next deadline delta DTU,budget ms|1 postponed|
|3|Backlog rejection|arrival,queued RX,0,0|2 backlog|
|2|Reference arm failure|raw target,scheduler entry,slot reason,0|4 failed|
|4|TWR start|poll target,reservation end,0,0|1 started|
|4|TWR complete|poll TX,final RX,corrected ToF,raw ToF|0 committed|
|4|TWR failure|now,deadline,protocol stage,0|2 aborted|

All hardware times belong to the emitting node; never subtract different nodes'
raw clocks. Pair fields on the slot event are zero (roster/own slot supplies identity).
TWR start records denote actual initiator start; a missed responder POLL has no
start event. Completion/failure and optional AA1 expose responder progress.

```sh
python3 tools/analyze_mul_timing.py alr133.log alr134.log > timing.jsonl
```

Operator physical validation: first select **codec5 TDMA with cycle_ms1000** on
all nodes (otherwise identical to the example; fingerprint436DB1FB), and verify independent
post-own-TX worker execution (ALR134 before ALR133's broadcast). The retained
legacy plan intentionally keeps its old worker admission and is not this test.
Then, with RF OFF
on all nodes, select the identical200ms example. Capture three minutes on all
ALRs and RLR141. Check actual=intended TX, minimum lead>=1000us at both checks,
worker duration<=configured budget and finish before24.1ms, no unexplained ring
loss/backlog/postponement, one rotating opportunity per cycle regardless of outcome,
no RF overlap, valid AA ages/AR results and unchanged calibration. Count expected
900 cycles (allow explicit capture boundaries), separate missing RF from expired
relations/clock invalidity/transport losses, and run the production replay validator
on complete captured snapshots. Confirm UART diagnostics can sustain the selected
capture settings. Repeat at1000ms as a control. Do not claim5Hz validated until
these board timing, throughput and RF checks pass. Do not try3ms in this iteration.


Operator capture/replay commands after every node's plan/firmware has been checked:
issue `:on` on each LPS and `on` on RLR141, then capture (adjust only the endpoint
if the operator's network changed):

```sh
cd ~/Documents/epuck-esp32
# Operator ONLY: connects to the already-enabled RLR, records180s, then stops.
timeout -s INT 180s python3 tools/uwb_collect.py --robot 192.168.1.141:3333 --output ../uwb_logs/tdma-200ms --no-live
# Set CAPTURE to the exact file path printed by the collector, then:
python3 tools/validate_tdoa_replay.py "$CAPTURE" --stm32 ../epuck-stm32 --sanitize
```

Retain ALR USB terminal logs concurrently and analyze them with the SL1 command
above. The timeout deliberately ends a capture; retain and inspect its boundary
records rather than hiding an incomplete final snapshot. Check `mismatches`,
`replay_complete`, `transport_complete`, missing vectors and observed diagnostic
drops independently. After recording, issue `:off` / `off` and confirm stopped.

## Offline engineering results

The named stationary profile and actual linker map give:

| Image | Flash /131072 | RAM including all reserved stacks /16384 |
|---|---:|---:|
|Previous ordinary (`c9bf6c5`)|130592|14332|
|New ordinary|128928 (2144 free)|14324 (2060 free)|
|Previous SL1|131024|14484|
|New essential SL1|130552 (520 free)|14828 (1556 free)|

Removing redundant ALR printf formatting and the large TWR timestamp/power dump
pays for the planner/console changes: the combined ordinary image saves1664 bytes
of Flash. Expanded essential tracing costs1624 Flash /504 RAM versus the new
ordinary image; the complete diagnostic variant still saves472 Flash versus the
old one. No estimator, calibration, safety branch, task stack or queue was removed.
The optional AA1+RX extras are host-tested but are not promised to fit together
with every diagnostic flag; build the essential variant above for timing work.

`check_mul_image.py` verifies actual regions, the168-byte clock workspace,
1600-byte main/UWB stacks,800-byte idle stack,512-byte USB queue and2048-byte
linker interrupt-stack reservation. `-fstack-usage` reports240 bytes for the ALR
event frame,168 for wait/worker,112 for Finish,368 for the unchanged dense update,
224 for prediction,176 for plan-operation selection,112 for validation,104 for a
trace event and208 for the main trace formatter. A conservative nested TWR
encode/plan-validation path totals about1.3KB of reported C frames before RTOS,
library and interrupt overhead; compiler-inlined paths must not be summed twice.
This is a static audit, not a measured task watermark. Validate runtime watermarks
and interrupt latency on boards; no reservations were reduced to make this fit.

The240000-observation sanitizer comparison against the original pre-deferral
implementation remains bit-identical, including200/1000ms, both estimators,
propagation changes, gaps/recovery, wrap and corrupt-state rejection. Actual
Cortex-M0 instruction maxima for regular pre-TX clock processing (excluding IRQ/SPI and
plan framing) are21462 at200ms and
20891 at1000ms, versus138101/137018 before deferral. Deferred full updates reach
135348/135404 instructions respectively; final Cortex-M0 states are bit-identical.
These are emulator instruction counts, not microseconds or board measurements.


Verified offline commands and results:

| Check | Result |
|---|---|
|`bash portable/mul_core/tests/run.sh`|Full ASan/UBSan suite, codec2/3/4 preservation, codec4->5 interrupted journal writes, canonical rosters/rotation/wrap and RF5 checks passed|
|`python3 tools/test/mul_slots.py --mul-only --sanitize --prop-age 6000`|277 tests for each of three compile profiles; actual DW1000 driver simulations included|
|Same command with `--slot-trace --slot 10`|277 tests passed, including bounded trace backpressure/overflow and post-TWR wakeup regression|
|CI variants `--slot 10 --twr-trace --rx-diag` and `--slot 10 --rx-diag`|277 tests each passed|
|`./tools/build/test`|302 legacy/USB tests passed; local environment supplies the `python`->Python3 alias installed by CI|
|`python3 -m unittest discover -s tools/test`|53 tests passed; production-C preview timing matrix and SL1 reassembly included|
|`python3 tools/test/compare_deferred_clock.py --sanitize`|240000 complete state comparisons bit-identical|
|`tools/test/clock_cost.py --period 200` and `--period 1000` in the emulator environment|Cortex-M0 instruction/terminal-state checks passed|
|STM32 `bash tests/runtime_config/run.sh`|Sanitizer configuration, actual radio responder, replay, queue pressure and new TDMA fixture passed|
|STM32 Universal build + `check_elf.py`|170244 Flash /44168 RAM; load segments do not touch configuration sectors|
|ESP32 `bash tests/run_host.sh`|119 tests passed; ESP32 firmware unchanged|
|`python3 tools/verify_tdoa_replay.py --kalman --tdma` in ESP32|90/90 exact TDoA and vector comparisons, zero gaps/drops, complete replay|
|`python3 tools/verify_tdoa_replay.py --paced --kalman` in ESP32|140/140 exact interior pairs, deliberate optional transport losses reported, no observed replay drops|

Both committed Makefile CI images also build and pass the image gate: default
128672 Flash and long-range128664 Flash, both14324 RAM including reservations.
The local unrelated channel5 Makefile addition was excluded using a temporary
copy of the committed Makefile for those two checks and remains untouched.
The retained stationary diagnostic artifact is the last build. No generated
binaries, stack reports, raw logs, EEPROM images or secrets are committed.
