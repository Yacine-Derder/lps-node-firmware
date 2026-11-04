# Configurable TDMA and one TWR opportunity per cycle

The operator has deployed **55ADA235** and recorded5Hz broadcasts on November3.
The follower-AR timing correction below is offline-tested and still needs board
validation. This agent has not changed a live plan or accessed hardware. All nodes must
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

## Initial TDMA engineering results (5a49fe3)

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

## Deadline-aware follower AR preparation

The operator's November3 `tdma-200ms-30s` capture uses55ADA235. Across vector
cycles2944..3066, initiator logs show132->13323OK/2timeout,132->13423OK/1radio/
1timeout,132->14124OK,133->14121OK/2late_setup/1timeout, and134->1412OK/
17late_setup/5late. One134 opportunity has no logged outcome; do not invent its
cause from missing text. The122 complete ALR134 worker timing records have
median4394.55us, range4365.50..4528.99us, and finish10113..11401us after reference
RX. ALR133's corresponding median is4342.03us, finishing14994..15495us after RX.
Thus this recording does not support the claim that Kalman consumed its entire
10ms allowance or directly occupied the TWR POLL deadline.

Two software defects are demonstrated: the old AR wait sampled SYS_TIME before
three mappings and then slept the entire old remaining interval, and the AR
operation repeated those mappings at its reservation-start callback. Rounding
and setup then consumed the minimum-lead margin. The logs prove the late-check
failure class, but do not measure every mapping/SPI contribution; the aggregate
latencies below are explicit simulation inputs, not inferred board WCETs.

For codec5 only, a follower now prepares one immutable mapped reservation after
its own TX resolution and deferred Kalman completion. It maps the existing start,
POLL and end plus the existing preceding1ms service interval. It rechecks the
hardware clock after preparation, rather than including CPU work in the sleep.
Subsequent callbacks reuse the mapping. The participating initiator's Kalman
admission also respects its earlier preparation deadline; it still runs
immediately when the configured allowance and service margin fit.

At the example settings, protected broadcast/Kalman time ends at23.1ms. Local
radio preparation may start in the existing23.1..24.1ms service quantum. This
only programs the delayed transaction; **RF transmission remains at28.1ms**.
The exchange phase still starts at24.1ms and ends114.1ms, the same packet bytes,
reply spacing, pair rotation and **55ADA235** fingerprint. No responder accepts a
POLL earlier, and STM32 needs no scheduling/protocol change. No additional idle
period or configuration constant was added. Legacy codec2/3/4 retain their old
three-query mapping and release timing.

The cache is bounded to one reservation and bound to the reference cycle and
clock epoch. Source selection/domain resets or unavailable external geometry
invalidate it. Same-domain fresh poses may arrive while the previously validated
snapshot remains within its checked horizon; explicit withdrawal/unavailable
geometry cancels preparation. The mapper already validates the full reservation
end against propagation/clock age bounds. All live antenna, DX_TIME, setup-lead,
TX-status, absolute deadline and actual-TX-marker checks remain. One served bit
retires an opportunity before arming, including errors and late callbacks.

The driver-level simulation uses the real mapping, Kalman and protocol logic,
with elapsed hardware time injected around production calls. It reproduces the
old path's511.87..511.94us lead with4.4ms Kalman,300us/query and1.188ms aggregate
encoding/configuration/SPI work. The new path has3111.91..3111.98us lead, and
2299.91..2299.98us with2ms aggregate setup. A9ms Kalman case retains2699.91us
or more with2ms setup. Impossible3ms setup/late wakes, stale epochs and withdrawn
external geometry cancel without retransmission. Both AR pairs complete actual
encoded five-message exchanges in the simulator; every follower position for
N2..8 and5/10ms spacing is covered. Independent-clock alignment remains within
the existing512-DTU rule. These results do not prove physical success rates.

Additional debug SL1 events retain the existing four-line envelope:

|e|w|t0,t1,t2,t3|
|---:|---:|---|
|5|0|software begin,mapping end,software end,earliest preparation target|
|5|1|software begin,mapping end,failure time,0 (invalid preparation)|
|5|2|now,reservation start,POLL,0 (epoch/source invalidated)|
|6|0|radio configuration begin,configuration end,raw delayed target,armed time|
|6|1/2|begin,lead-check time,raw target,reservation end (initial/setup deadline)|
|7|0/1|expected POLL,actual POLL,0,0 (exact/mismatch)|

The analyzer reports mapping/software duration, configured/armed lead and POLL
error, using only differences in the emitting hardware clock. It retains partial
capture/drop handling; missing events are not evidence of zero errors. Compact
console help and repeated storage-error strings were consolidated for Flash;
commands and validation are unchanged. The redundant human `uncertainty=UNKNOWN`
suffix was removed, while `distance_m`, `raw_mm`, role and power result fields
remain compatible with the existing archived ALR parser. UWB machine telemetry
and replay framing were not filtered or changed.

The timing milestone passes the280-test production sanitizer matrix (three
compile profiles),280 diagnostic tests,305 legacy tests,54 host tests, and the
240000-observation bit-identical clock comparison. The additional external-validity
regression brings the diagnostic suite to281 tests. The diagnostic image is
130904/131072 Flash and14892/16384 RAM including reserved stacks, leaving168/1492
bytes. Compiler frames are296 bytes for mapping,128 for preparation,168 for the
worker/wait and248 for the ALR event handler; this is a static audit, not a board
stack watermark. No stack or USB queue reservation is reduced. Runtime logging
and final deployment validation are documented separately below.

Runtime verbosity, final image limits and the operator-only four-robot30second
procedure are documented in [mul_logging.md](mul_logging.md). SL1 capability is
now included in the default production image, gated at runtime by debug.

## Reference OFF/ON recovery validation (operator only)

The recovery fix addresses scheduler/clock sequence disagreement, including the
3s-loss/6s-Kalman holdover interaction. See the
[verified mechanism and compatibility limits](../portable/mul_core/CLOCK.md#reference-restart-evidence-and-epoch-ownership).
Deploy the corrected ALR image to ALR132/133/134 and the matching vendored-core
Universal STM32 image to RLR141. With all RF stopped, update RLR141, both followers,
then the reference; keep every device's saved calibration/plan. No ESP32 update is
needed. Use the existing build/ELF checks above; do not mass erase configuration.
The following commands are instructions for the operator, not actions performed
by the coding agent.

Use the existing serial console, or run `python3 send_uwb_config.py` from
`~/Documents` and select the intended node. Before startup inspect:

|Node|Read-only commands|
|---|---|
|Each ALR|`:status`, `:plan show`, `:clock show`, `:diag`|
|RLR141|`status`, `plan show`, `clock show`, `hybrid status`, `hybrid ar`, `hybrid diag`, `tlm status`|

Require codec5/hash **2359421F**, period100ms, slots5ms, Kalman budget10ms,
reply10ms and TWR window50ms on all four. Preserve the established roster order
(the fingerprint checks it). Do not copy the older 200ms/reply20 example above.
Enable RF with `on` on RLR141, `:on` on each follower, then `:on` on ALR132.
Use the existing collector/logging workflow without changing live settings.

1. **A — startup:** wait at least10s, then observe30s of normal operation.
2. **B — short restart:** on ALR132 only send `:off` then `:on` with measured
   RF downtime approximately1s **total**, including command/status overhead.
   Use a direct serial terminal for this short test: observe `:status`
   requested=0/running=0 promptly, then issue `:on` within that1s interval.
   The interactive sender's blocking response waits can extend the interruption
   beyond3s and would miss this regression. Record actual stop/resume times.
   Leave followers and RLR running; observe at least30s after restart.
3. **C — medium restart:** repeat B with approximately5s OFF.
4. **D — long restart:** repeat B with approximately10s OFF.

For each test save before/after `:diag` on both followers and the RLR diagnostics
above. Two coherent post-restart reference packets establish the new session;
then allow estimator warmup and a complete five-cycle TWR rotation. Require:

- Both follower `tx_ok` and `kalman` counters resume increasing; no sustained
  five-cycle `prep_fail` growth after warmup. The first warmup opportunities may
  be unavailable. `slot_miss`, `backlog` and `overrun` must not grow persistently.
- With debug `SL1` capture already enabled, `e=1,w=0` arms the assigned slot,
  `e=2,w=0` confirms the exact target, and `e=3,w=0` completes fitting. Decode
  local timestamps only within their own clock domain. Warmup followed by fresh
  valid mappings must be visible in RLR `clock show` (`ready/applied`, map/age).
- `twr_ok` resumes, and RLR `hybrid ar` completed increases. Over five cycles the
  opportunities remain 132→133, 132→134, 132→141, 133→141, 134→141, irrespective
  of missed opportunities. Require fresh calibration, not old retained values.
- RLR vector valid masks/TDoA become available again after fresh follower epochs
  and synchronization. During loss/warmup, measurements must be invalid/NA rather
  than frozen numerical distances. Check `hybrid diag` and `tlm status` to
  distinguish RF/model unavailability from transport drops.

At normal verbosity the counters remain available through `:diag`; detailed
`SL1` records require debug verbosity (`:cfg set verbosity debug`) and can increase
capture load. Restore the previous verbosity after detailed timing capture.
Archive the raw outputs with the OFF/ON times. Run the existing timing/replay
analysis on complete captures, and report any unavailable intervals and transport
loss separately. Offline tests are not evidence that these physical tests passed.

Offline verification of this recovery change: `tools/test/mul_slots.py --sanitize`
passes316 native tests and291 MUL ASan/UBSan tests for each500/50/10ms compile
profile; the explicit production trace profile passes291 tests plus sanitizers.
The canonical core sanitizer suite and54 host Python tests pass. The existing
`compare_deferred_clock.py --sanitize` reports240000 bit-identical observations
against its frozen baseline. The stationary CH5 production/SL1 image passes
`check_mul_image.py`:131008/131072 Flash and14924/16384 RAM, with168-byte clock
workspace and all original stack/queue reservations. Flash headroom is only64 B
with GCC13.2.1; additional diagnostics/build flags need their own linked image
check. Plan/codec modules join the existing LTO group solely to fit recovery;
clock-fit flags and all wire codecs/math are unchanged. Board stack watermark,
radio WCET and physical OFF/ON validation remain operator work.

The production compiler frame audit reports296 B for mapping,120 B for
preparation,328 B for worker/wait and256 B for the ALR handler. Begin/Finish use
96/104 B; restart evidence uses80 B plus the40 B shared interval gate. These are
individual static frames, not a whole-program stack proof or board watermark.

The exact100ms restart matrix also executes `prepareFollower` and the real AR
initiator path after warmup: preparation succeeds on both followers, `prep_fail`
stays unchanged, one POLL is armed and its target differs from the immutable
mapped reservation only by the existing0..511-DTU alignment rounding.

## Opt-in complete follower graph (codec6)

`plan set ff on` adds every unordered pair of followers, ordered lexicographically
by ID with the lower ID initiating. It is valid only in TDMA with at least two
followers. AA remains reference→sorted followers, then FF, then the existing
sorted ALR→RLR operations. `ff off` preserves codec5 bytes, fingerprint and pair
ordering; old codecs2..5 remain readable with no migration. The explicit FF bit
is4 in the existing enable byte; codec6 is mandatory when it is set. No reserved
byte is reused. RF5 framing and the five-message exchange remain unchanged.
Older decoders reject codec6; every participant must run matching plan support.

Codec6 fingerprints bind both the semantic plan and the actual ordered
`(kind,initiator,responder)` triples. Starting with the codec5-style semantic CRC,
fold each triple in rotation order as CRC32 of the little-endian prior CRC plus
those three bytes. The storage CRCs remain unchanged. This uses bounded storage
and no roster-sized packet buffers. Changing either broadcast order or enabled
pair graph changes the full-plan radio fingerprint.

For ALRs `[132,134,133,135]`, RLR `[141]`,100ms cycles,5ms slots,10ms Kalman
budget,5ms reply and30ms TWR window, the fingerprint is **EC2B4B2A**. One operation
occurs per cycle, with reservation29.1–59.1ms, POLL33.1ms and40.9ms guard:

|cycle mod10|kind|initiator|responder|
|---:|---|---:|---:|
|0|AA|132|133|
|1|AA|132|134|
|2|AA|132|135|
|3|FF|133|134|
|4|FF|133|135|
|5|FF|134|135|
|6|AR|132|141|
|7|AR|133|141|
|8|AR|134|141|
|9|AR|135|141|

The two followers execute the existing initiator/responder state machines in
exchange-local roles. FF POLL preparation maps the reservation into the
initiator's local clock after its broadcast and protected Kalman work. Its epoch
is checked even while it acts as exchange initiator. The responder never compares
the initiator's P1 to the unrelated reference-clock origin. All packet identity,
fixed-target, delayed-TX, antenna, setup lead, prefix, interval, age and deadline
checks remain. FF RESULT never commits an AA propagation estimate or changes the
clock fit. The separate `aa` owner retains its reference/follower binding through
FF successes and pair-local failures; an actual reference epoch loss still
invalidates synchronization through the existing owner.

At normal/debug verbosity, successful planned AA and FF ranges retain the human
`MUL TWR` line and add one atomic, machine-readable `MR1` line (at most128 bytes):

```
MR1 c=43 i=133 j=134 l=133 t=0123456789 e=0000000000000012 p=EC2B4B2A b=4 s=0 mm=1500 raw=1500 q=0 why=ok
```

`c` is the reference cycle; `i/j` are exchange initiator/responder; `l` identifies
the emitting radio; `b` is1 for AA or4 for FF. `t` is a native40-bit local DW1000
marker: P1 TX at the initiator or raw P2 RX at the responder. `e` is that local
clock's64-bit boot-local epoch; `p` is the full-plan fingerprint. Neither time nor
epoch can be compared across different radios without a clock model. On full
reboot epoch tokens may repeat: the PC must retain capture/boot-session context.
`s=0` supplies corrected/selected `mm`, uncorrected `raw` millimetres and power
indicator `q` (1 applied,0 unavailable). Both endpoints report the same pairwise
range in initiator DTU converted with the existing formula. On `s=1`, `t` is the
failure marker, `why` is the existing failure reason, and no numerical range is
supplied. Unstarted/missed preparations remain visible in `prep_fail`/SL1;
missing records never imply success. Quiet mode suppresses successful range
text; finite USB backpressure uses the existing bounded ring and dropped counter.
No ALR networking stack or extra on-air traffic is added.

The existing native harness now runs two independent production follower owners
with separate clocks, pending workers and calibrations through all three FF pairs,
including signed skew and native counter rollover. It checks the five-message
exchange, fixed broadcast slots, preserved AA exports and subsequent Kalman
progress. Codec/persistence tests cover all-byte cuts and opt-in/rollback. The
console dispatchers are kept out of their callers to fit Flash without changing
clock mathematics, stack reservations, queues, radio timing or validation.
Physical validation and matching STM32 support are required before deployment.

The PC export interface is `python3 tools/mul_ranges.py alr132.log alr133.log
alr134.log alr135.log --session CAPTURE_BOOT_SEGMENT --output ranges.jsonl`.
Add `--alrs 132 134 133 135` to select only valid Stage A geometry inputs; this
never admits AR ranges or TDoA. JSONL retains pair/cycle/plan identity, selected
and raw range, power flag, failure status/reason and a local acquisition object
with node,40-bit DTU marker,64-bit epoch and capture-session provenance. It does
not substitute serial arrival time, unwrap unrelated clocks, average endpoint
reports, assume simultaneous geometry or solve for positions. The two endpoint
reports are intentionally preserved; future estimators must partition sessions,
handle duplicates/clock conversion and asynchronous measurements explicitly.
Split capture segments at full reboots because local epoch tokens can repeat.
Malformed/partial machine lines are rejected, unrelated human log lines ignored,
and failed measurements export null numerical ranges. Synthetic noncollinear
four-ALR fixtures feed the production C formatter and verify all six K4 distances
can be exported within integer-DTU quantization, without any RLR geometry factor.

The paired-follower regression additionally covers failed RX, a mismatched TX
marker, dropped replies/timeouts and a missed reference without losing a healthy
filter. A genuine reference restart during an armed FF exchange cancels it,
invalidates AA and queued work, then warms both estimators automatically. Repeated
wakeups cannot retransmit a retired opportunity. Its production KF calls include
a modeled4.4ms execution cost; this checks admission/slot interactions and does
not establish a hardware WCET. Synthetic exported acquisitions advance in their
declared local modular clocks with the scheduled cycle, rather than host time.

## Four-ALR operator configuration

These are manual deployment instructions, not actions performed by the agent.
Keep the existing three-ALR100ms/reply5/window30 configuration and captures as the
rollback baseline. Its codec5 fingerprint is **2AED8A85** with ordered ALRs
`[132,134,133]`, RLR `[141]` and revision1. The approximately98% physical TWR
success is user-reported; this change has only been tested offline. Earlier
200ms/reply20 and100ms/reply10 sections describe other profiles, not this target.

1. Stop RF on every ALR (`:off`) and RLR (`off`); require status requested/running
   both0. Save `status`, `plan show`, `cfg show`, `clock show`, `cal show` and
   diagnostics from each device. Preserve address, role, power/antenna calibration,
   retained legacy profile and configuration sectors. Keep known-good firmware
   artifacts for rollback. Do not issue defaults or mass erase.
2. Build the same stationary CH5 image for all four ALRs and Universal for RLR141
   using the existing build commands above. Check each ELF before operator-only
   programming. The LPS artifacts are `bin/lps-node-firmware.elf/.dfu`; STM32 is
   `build/Universal/EPuck_STM32.elf`. Update LPS and STM32 together, retaining RF OFF
   across programming/reset. No ESP32 firmware update is needed. Update the PC
   replay tools from the sibling ESP repository.
3. Confirm ALR132 is reference and ALR134/133/135 have their own distinct existing
   IDs in mode5. RLR141 retains address141 and `hybrid_static`. If ALR135 has not
   been provisioned, use the established [identity/roster commands](mul_commands.md#persistent-multi-alr-schedules)
   while OFF before the common plan below; verify its saved/boot ID and mode after
   the required restart. Do not copy another board's address or calibration.
4. On each ALR, issue the following complete lines, inspecting every reply. If a
   saved plan exists, retain it and skip `plan init tdma`; initialize only if absent.
   The temporary1000ms period permits safe changes without bypassing validation.
   Setup lead/process must be reduced before setting5ms slots.

```text
:off
:status
:plan show
# Only if the plan is absent:
:plan init tdma
:plan set ff off
:plan set lead_us 1000
:plan set process_us 2000
:plan set air_us 1000
:plan set guard_us 100
:plan set slot_ms 5
:plan set cycle_ms 1000
:plan alrs 132 134 133 135
:plan rlrs 141
:plan set reply_ms 5
:plan mode tdma
:plan set aa on
:plan set ar on
:plan set kalman_budget_ms 10
:plan set twr_window_ms 30
:plan set cycle_ms 100
:plan set revision 1
:plan set ff on
:plan use on
:cfg set clock_estimator kalman
:plan show
:clock show
:status
```

On **RLR141**, send the same block without the leading `:`. It changes the plan
and selected estimator only; retain existing samples/telemetry/Wi-Fi verbosity
settings unless deliberately preparing a capture. `plan init` is conditional and
the comment line is not a command. Every successful mutation is autosaved; no
extra save is needed. The ALR native test executes this command sequence and
checks the final fingerprint, legacy storage preservation and reboot persistence.

All five devices must report selected codec6/hash **EC2B4B2A**, ordered ALR slots
132@0,134@5,133@10,135@15ms, RLR141, cycle100ms, Kalman budget10ms, reply5ms and
window30ms. A representative ALR `plan show` excerpt follows (generation varies):

```text
PLAN selected=1 gen=<saved-generation> codec=6 p=2
PLAN hash=EC2B4B2A ALR=4 RLR=1
PLAN slot=5 cycle=100 reply=5
PLAN AA=1 AR=1 FF=1 util=0
TDMA kalman_budget_ms=10 twr_window_ms=30
TDMA start_us=29100 guard_us=40900
ALR 0 id=132 offset=0 ms
ALR 1 id=134 offset=5 ms
ALR 2 id=133 offset=10 ms
ALR 3 id=135 offset=15 ms
RLR 0 id=141
AIR reserve=30000 capacity=1 peak=1
PREVIEW reference_us; TDMA full rotation
C0 A 132>133 29100..59100us
POLL cycle=0 at=33100 us
C1 A 132>134 29100..59100us
POLL cycle=1 at=33100 us
C2 A 132>135 29100..59100us
POLL cycle=2 at=33100 us
C3 F 133>134 29100..59100us
POLL cycle=3 at=33100 us
C4 F 133>135 29100..59100us
POLL cycle=4 at=33100 us
C5 F 134>135 29100..59100us
POLL cycle=5 at=33100 us
C6 R 132>141 29100..59100us
POLL cycle=6 at=33100 us
C7 R 133>141 29100..59100us
POLL cycle=7 at=33100 us
C8 R 134>141 29100..59100us
POLL cycle=8 at=33100 us
C9 R 135>141 29100..59100us
POLL cycle=9 at=33100 us
```

Only after agreement, start RLR141 (`on`), then all three followers (`:on`), then
reference132 (`:on`). Inspect status and allow warmup plus at least one full1s
rotation. A failed command, unsupported version or mismatch must be resolved
while OFF; do not substitute a legacy schedule to bypass the check.

For rollback, stop **all** RF first. On the new firmware set FF off everywhere
before loading older images; restore the saved codec5 plan, including the original
three-ALR roster and timing, and verify **2AED8A85** if using the baseline above.
To construct that baseline with the common block, leave FF off, replace the ALR
roster by `132 134 133`, and omit `ff on`; all other target values stay the same.
Keep135 OFF after removing it from the roster. Preserve each board's calibration
and identity and verify the selected plan again after programming. A frozen
pre-FF decoder from `b8205b1` accepts codec5 and rejects codec6. Its journal can
fall back to a previously supported record; rejection alone does not guarantee
RF remains OFF on an older image. Explicit saved OFF and post-boot inspection
remain required. `plan use off` retains the plan but selects a separate legacy
profile, which must be inspected independently before any ON.

### Optional inter-ALR-only schedule

No new planner policy is needed. With all five radios OFF, `plan set ar off`
(`:plan set ar off` on ALRs) on every participant leaves exactly the first six
AA/FF pairs, one per100ms cycle: each edge gets an opportunity every600ms
(approximately1.67Hz). The target fingerprint becomes **68648141**. RLR141 stays
in the roster and continues receiving all four broadcasts at10Hz; it simply has
no assigned AR responses. Native median/Kalman replay with AR off still yields
all135 TDoA/vector comparisons exactly. Leave AR on for the requested initial
ten-pair configuration. Toggling AR back on restores **EC2B4B2A**.

### Physical acceptance, still outstanding

Use fixed, noncollinear ALR geometry and the existing calibration first. Capture
all four ALR serial streams at normal verbosity plus RLR essential telemetry and
replay, retaining capture/boot-session IDs. Split export sessions at full resets.
Do not tune offsets or gates to force agreement. Before testing FF, repeat the
original three-ALR FF-off baseline and compare success/error counters against the
user's prior experiment under the same geometry and capture load.

For the target plan, after warmup collect at least10min. Check exactly one TWR
opportunity per cycle and all ten pairs per rotation, with each pair at1Hz. Count
successes, failures and missed opportunities separately. Require all six unique
inter-ALR edges in `MR1`/JSONL, correct endpoint/plan/epoch provenance and no
numerical range on failures. Compare the six distances to independently measured
geometry; quantization tests do not establish physical accuracy. Endpoint reports
are duplicates of one exchange, not independent factors. Four AR pairs remain
available for diagnostic comparison but must not enter Stage A geometry.

Require ALR broadcasts at0/5/10/15ms, continuous follower Kalman/tx counters and
fresh RLR three-component TDoA vectors at100ms cycles. Capture debug SL1 for a
short timing interval if needed; inspect its dropped counter and restore normal
verbosity afterward. Verify actual setup lead, delayed-TX alignment, reservation
end before59.1ms and preparation guard40.9ms, without sustained prep_fail,
slot_miss, backlog or overrun growth. Check reference AA delay ownership/age before
and after FF cycles: FF must not refresh or replace it. Invalid/missing data must
remain explicitly unavailable. Record USB/UART/Wi-Fi drops separately.

Repeat reference-only1/5/10s OFF/ON recovery using the procedure above, leaving
followers/RLR running; keep the1s test's measured total downtime below3s. Require
automatic warmup, fresh AA correction and resumed TDoA/FF without follower reset.
A failure or missed broadcast may retire one opportunity but must not cause an
extra POLL. Board stack high-water, interrupt/radio WCET, true4th-slot fit and
sustained serial/telemetry throughput remain hardware acceptance requirements.
The future PC solver must handle asynchronous edges, clock conversion, NLOS and
geometry degeneracy; it is deliberately outside this firmware task.

### Final offline verification and resource limits

All320 native tests and295 MUL ASan/UBSan tests pass at each500/50/10ms compile
profile; the production SL1 profile passes295 plus sanitizers. Canonical/vendor
byte agreement, independent golden tests, both DW roles and configuration rejection
pass. All58 ALR host tests pass, including exact ten/six-pair preview, old bytes,
persistence and synthetic K4 C-formatter export. The frozen clock comparison
passes240000 bit-identical complete-state observations. STM32 passes all nine
native suites, its runtime/power-cut/replay/FF-overhearing checks and nine
coexistence scenarios. ESP host tests pass121; accelerated full transport replay
matches all135 four-ALR TDoA/vector results exactly. Intentional paced UART stress
still reports queue drops rather than hiding them.

Clean GCC13.2.1 linked images, checked by `tools/test/check_mul_image.py`:

|image|Flash bytes /131072|RAM including linker stack /16384|free Flash|free RAM|
|---|---:|---:|---:|---:|
|published default (CH2, without local Makefile selection)|130508|14928|564|1456|
|default build (existing local CH5 selection)|130808|14932|264|1452|
|stationary CH5 production/SL1|130856|14932|216|1452|

The published default was built using an unmodified Git copy of the Makefile,
without editing the existing local CH5 selection. The stationary artifact is
restored in `bin/`. The clock workspace stays168 B,
radio and main task stacks1600 B each, idle800 B, linker stack2048 B and USB
queue512 B. No safety allocation, guard, validation or estimator math is reduced.
Individual production compiler frames: mapping296 B, preparation120 B,
worker/wait328 B, ALR event handler256 B, TWR event handler488 B, fingerprint160 B,
plan command224 B, serial dispatch40 B and main task448 B; Begin/Finish96/104 B.
These are individual frames, not a complete call-chain bound or board watermark.
STM32 Universal uses172420/786432 B Flash and44344/131072 B RAM, CCM0, with
reserved-sector ELF checks passing. ESP firmware is unchanged. Small Flash
headroom makes the linked-image check mandatory for any changed build flags.

## Four-ALR TWR turnaround investigation

User captures show a physical reliability regression at unchanged100ms/5ms
slots/reply5/window30: many endpoint-paired failures are responder `late_setup`
with initiator `timeout`. This is not evidence that every timeout has that cause.
Normal and debug captures have different execution load and capture boundaries.

An offline Cortex-M0 reproduction is `tools/test/twr_cost.py` (use the optional
Unicorn/Capstone/pyelftools environment used by `clock_cost.py`). With the production
-Os/LTO codec path, valid AA packet encoding/decoding costs14398/15031 executed
Thumb instructions for3ALR FF-off,19392/20025 for3ALR FF-on,14495/15128 for4ALR
FF-off and22887/23520 for4ALR FF-on. The full plan fingerprint is recomputed on
every packet, including AA in an FF plan; the extra pair-fold work therefore
follows the plan rather than just the FF role. The radio encoder runs inside
`sendPacket` between its two setup-lead checks. These are CPU instruction counts,
not cycle simulation or a measurement of IRQ/SPI latency. They establish
avoidable deadline-path work, not proof of the complete physical failure cause.

The corrected TWR owner binds a validated immutable pair/cycle/index/fingerprint
snapshot before packet turnaround. Repeated callbacks reuse it; a new cycle or
operation rebinds, initialization invalidates the cached identity, and an active
exchange cannot switch plan pointers. `mulPlanTwrEncodeBound/DecodeBound` use the
same RF5 bytes and base decoder as the full-plan APIs. The old APIs remain for
callers that need validation against a mutable plan. Every header, endpoint,
cycle, operation, fingerprint, unused-stamp and power check remains; the state
machine still checks the prefix, role, intervals, epoch, setup lead and deadline.
No plan, fingerprint recipe, packet format, timing or calibration changes occur.

For the same four-ALR FF-on Cortex fixture, bound encode/decode execute1548/1952
instructions, independent of the four tested3/4-ALR FF-off/on plans, versus
approximately22.9k/23.5k previously. Binding costs about21.7k but occurs once at
reservation preparation rather than per packet. This removes repeated full-plan
validation/fingerprinting from RESPONSE and REPORT, and from FINAL/RESULT too.
It does not optimize away power correction, change its values, or promise a
hardware turnaround time. The synthetic codec test checks all five message types,
40 wrapping cycles per configuration and576000 one-bit wire mutations against
the original decoder verdict. Existing actual-radio-owner/native/sanitizer tests
pass, retaining AA/FF isolation and no retries. The bound-codec stationary build uses14964 B RAM, including the32-byte prepared
identity. TDMA responders bind after protected broadcast/Kalman work, refresh
hardware time before calculating sleep, and keep legacy multi-operation order.
The stationary image uses130952 B Flash (120 B free); original stacks/queues and168-byte clock workspace remain. The original mutable-plan
codec entry points retain their existing source and early header rejection;
only the ALR exchange owner selects the prepared path.

### Evidence boundaries and remaining failures

Offline correlation of the supplied FF1/FF2 normal-verbosity ALR `MR1` records
(last report per local endpoint, matching plan/cycle/pair) finds246 responder
`late_setup` + initiator `timeout` cases among256 failures with both endpoint
reports. This approximately96.1% cohort supports investigating turnaround; it is
not an opportunity-based physical success rate. Capture boundaries, missing
records, warmup and unstarted opportunities need separate accounting. The
three-ALR FF-on validation capture also contains `late_setup`, consistent with
fingerprint work scaling with the plan graph rather than exclusively with FF
packets. The earlier approximately99% baseline and66.4/67.1/90.5% experiment
figures remain user-reported physical results, not new firmware results.

The root `uwb_logs/alr*.log` four-ALR FF-off capture has no recorded `late_setup`,
but includes10 paired `radio_event`/`timeout` failures and an endpoint `ok` versus
`radio_event` disagreement. Its normal records cannot identify the hardware RX
status, interrupt latency or failed stage. A timeout means no accepted next stage
before its absolute deadline; it does not identify whether RF, RX error, delayed
TX, callback latency or the peer caused that absence. A locally confirmed RESULT
TX does not establish that the other endpoint received it. These failures remain
unresolved; no RX error is ignored, no retry added and no incomplete exchange is
promoted to a complete pair measurement.

The state audit found no demonstrated reference-calibration overwrite from FF.
The prepared identity adds an explicit active-plan pointer guard and is rebuilt
on operation/cycle change; the separate AA owner remains the only propagation
calibration source. `plannedFollowerOperation/Wait`, `plannedReferenceService`,
`mulTwrConfigurePlan/OnEvent/Cancel` retain their absolute reservation/epoch,
attempt retirement, broadcast priority and delayed-TX validation. Native tests
exercise both real follower owners, while the new persistent-owner matrix runs
120 cycles for each3/4-ALR FF-off/on configuration through AA/FF/AR, corrupt POLLs,
RX failures, interrupted exchanges and RESPONSE/REPORT post-setup lead boundaries.
It models1.27ms setup,2.6ms admissible versus2.8ms rejected RX callback latency,
and4.4ms KF work; these are injected costs, not measured firmware timings. Both
hardware clocks advance during modeled setup. Completed AA/FF/AR roles, no retry,
AA isolation and continuing estimator observations are checked. Existing
compensation warmup/age gates can retire an unavailable preparation explicitly;
the test never invents a successful range for it.

`mulPowerCapture` still reads power and the vendor correction reads it again,
including duplicate soft-float `log10f` and SPI diagnostics. This is pre-existing
baseline work, not a demonstrated FF-dependent defect. It remains unchanged to
preserve frozen correction behavior and avoid patching the upstream driver or
introducing another cache tied to RX lifetime. Packet math, antenna validation,
SPI setup/verification, raw/corrected timestamp handling and driver error/reset
semantics are unchanged. The fix removes only repeated plan work. Its instruction
measurements exclude IRQ/RTOS latency, SPI time and power/math work, so they do
not establish total callback WCET or restored near99% physical reliability.

No continuous diagnostic stream, new timer, SPI read in packet turnaround or
firmware instrumentation is added. Existing normal `MR1`, `:diag` and bounded SL1
remain available. The responder's extra hardware-time refresh occurs only in
preparation after protected work so binding time cannot extend its requested
sleep. Static frames remain488 B for the TWR handler,256 B ALR handler,328 B
worker/wait,296 B mapping and120 B preparation; these are individual compiler
frames, not a stack watermark or a complete call-chain proof.

### Minimal operator validation for the turnaround fix

Do not change reply5/window30/cycle100/slot5, PHY, antenna calibration, Kalman
settings, geometry or capture verbosity between comparisons. Build the stationary
CH5 image from the new `origin/main`, run `check_mul_image.py`, and retain the
source SHA and artifact hash with each capture: unchanged plan fingerprints do
not identify which firmware binary is running. Flashing is operator-only. Keep
RF OFF during programming and inspect retained IDs/calibration/selected plans
before startup. RLR/ESP wire compatibility is unchanged; no live receiver setting
or ESP firmware change is required. The STM32 vendor update synchronizes source.

1. Screen for60s each at normal verbosity:3ALR FF-off (`2AED8A85`),4ALR FF-off
   (`617AE520`), then4ALR FF-on (`EC2B4B2A`). Stop all RF before every plan/roster
   change and require matching plans everywhere; keep135 OFF when excluded.
   Start the RLR, followers, then reference. Separate initial warmup from the
   settled interval; keep raw ALR logs and RLR essential telemetry.
2. Compare inter-ALR pairs against the same-duration old firmware capture. Count
   expected opportunities, starts, completed endpoint pairs, one-sided results,
   failures by reason, unavailable preparations and USB/log drops separately.
   Require no persistent late_setup, duplicate attempts, slot misses, KF overruns
   or stale AA/epoch ownership. Do not infer success from absent records. Keep AR
   results separate because the receiver's changed physical distance is a
   confounder in the earlier experiment.
3. If the screen passes, collect10min4ALR FF-on at unchanged normal verbosity.
   Assess near99% per inter-ALR pair over the settled interval, retain all six
   edges and verify the four100ms broadcasts/fresh three-component RLR vectors.
   This is the physical acceptance gate, not a claim from the software tests.
4. If late_setup persists, take a short separate SL1 debug capture to compare
   local-clock setup duration and post-configuration lead. Do not include that
   capture in reliability comparisons: ALR134 debug perturbation is established
   by the user's experiment. For an unperturbed failing exchange, the smallest
   additional measurement is a passive logic capture of DW1000 IRQ/CS/SCLK/MOSI/
   MISO: identify the RF5 packet stage from TX_BUFFER, the DX_TIME target and
   TXSTRT/SYS_STATUS traffic. This distinguishes IRQ/service delay, setup latency,
   missing completion and actual RX error without adding console traffic. Do not
   relax the1ms lead or modify the error masks to make it pass.

If only radio_event/timeouts remain, obtain that status/stage evidence before
changing radio ownership/error handling. Preserve the old image and EEPROM for
rollback, with RF OFF and plan/status inspection after reboot. No physical radio
was accessed, configured, flashed, moved or operated during this fix.

### Verification of the final turnaround source

The full matrix passes321 native tests and296 MUL ASan/UBSan tests at each
500/50/10ms compile profile. The explicit stationary production SL1 profile
(`--slot 10 --mul-only --slot-trace --sanitize --prop-age 6000 --speed 0`) passes296 plus sanitizers. All59 host tests,
canonical sanitizer/golden/fuzz checks, independent576000-mutation frozen-codec
comparison, canonical/vendor byte agreement, both DW roles and configuration
rejection pass. Clock/Kalman comparison remains240000 bit-identical complete
states. STM32 passes its full runtime/configuration/AR/replay suite and all other
native suites/coexistence scenarios; ESP passes121 host tests and full transport
replay matches135 TDoA/vector results exactly. Deliberate UART-loss stress retains
its explicit queue-drop accounting.

Clean GCC13.2.1 images remain within the established limits:

|image|Flash used|Flash free|RAM including linker stack|RAM free|
|---|---:|---:|---:|---:|
|published default CH2|130604 B|468 B|14960 B|1424 B|
|stationary CH5 / SL1|130952 B|120 B|14964 B|1420 B|

The published default build used a temporary Git copy of the Makefile, leaving
the user's local CH5 selection intact. The stationary `.elf`/`.dfu` artifacts are
restored in `bin/`; `check_mul_image.py` validates physical regions, all original
stack reservations, USB queue and clock workspace. Static frame inspection is
performed on production compiler output, not inferred from native tests. Board
stack high-water, interrupt/service/SPI latency and physical ranging reliability
remain unmeasured. The added exchange binding consumes32 B static RAM; no
safety allocation or deadline has been reduced. The matching receiver core
source pin does not require reflashing existing wire-compatible RLR/ESP firmware.
