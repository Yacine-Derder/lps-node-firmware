# Generated AA TWR stage diagnostics

The optional LPS build flag `MUL_TWR_TRACE=1` adds local USB diagnostics to both
ALRs. It defaults to0. Both ALRs use the same image; persisted addresses/roles
still select initiator/responder. No on-air bytes, plan hash, scheduling constants,
reply/deadline values, EEPROM settings, calibration, RX policy or RF-OFF admission
checks change. Legacy fixed-scheduler/AR exchanges retain their existing logs;
AA1 records are restricted to generated AA operations. No hardware was flashed or
RF started while implementing/testing this feature.

## USB admission fix (58f72a3)

Commit afb1d35 contained event creation, the16-record ring and main-task polling,
but its USB integration was broken: `mulTwrTracePoll` formatted a complete AA1
line longer than48 bytes, while `CDC_TryWrite` rejected any length above48 before
checking the512-byte queue. The consumer retained that first record and retried
forever. An empty USB queue, working normal printf logs and an AA1 string in the
ELF/DFU could therefore coexist with no AA1 output. Normal logs use CDC_Write,
which does not have that48-byte admission guard.

The fix gives CDC atomic writes and the AA1 formatting buffer one shared
`CDC_TRY_WRITE_MAX=128` contract. The largest current AA1 line is113 bytes,
including CRLF. Admission still requires the entire record **plus64 spare bytes**;
it never waits or partially enqueues. The queue remains512 bytes, endpoint packets
remain64 bytes, and console pages remain short. Ring overflow and temporary USB
backpressure retain their existing behavior. No RF/protocol/timing/configuration
change is involved. USB enqueue work is bounded to128 bytes per call; software
tests do not establish physical callback latency under diagnostic load.

The regression executes event creation → ring → formatter → real usbcommTryWrite
→ real CDC_TryWrite → bounded simulated RTOS FIFO → real USB packetization →
simulated controller output. It fails with the old48-byte guard and passes after
the fix, including full-queue retry, startup/disconnection gates and USB reinit.
The earlier writer-only stub did not exercise the CDC length restriction.

## Build and test without hardware

From lps-node-firmware:

```sh
python3 tools/test/mul_slots.py --mul-only --sanitize --prop-age 6000 --slot 10 --twr-trace
./tools/build/test
```

The fixture uses the supplied plan826414CC: ALRs132/133, RLR141, AA1000mHz,
AR50mHz per link,10ms beacon spacing,20ms replies, lead1000/process5000/air1000/
guard100us, revision1, fair policy,70% cap and1000ms cycle. It checks the23.1ms
AA poll, both roles' stages, verified TX markers/local deadlines, missing response,
hardware receive timeout, receive failure, late poll/response setup, overflow and
USB backpressure. Existing radio/packet/math tests execute in the same trace build.
CI also runs this focused enabled configuration. A normal build omitting the flag
has no trace buffer or trace service in its image.

The enabled CH5 image has approximately736 additional static RAM bytes and about
1.6KiB additional flash compared with the same CH5 build without tracing. The
reviewed fixed image uses127700 flash bytes (3372 free) and15136 RAM bytes
including the linker stack (1248 free). Existing main/UWB/idle stacks and USB queue are unchanged.
The USB consumer formats outside the UWB task; it is never nested in radio
processing. The fix does not allocate another queue or increase any task stack.
Static image/frame checks do not prove physical deadlines or stack watermarks.

## Exact build, image checks and operator-only programming

Always clean when changing EXTRA_CFLAGS; make does not track every flag change
as an object dependency. Merely finding AA1 in an ELF does not validate USB output.
These commands build/check files only and do not contact a device:

```sh
cd ~/Documents/lps-node-firmware
git log -1 --oneline
git status --short
make clean
make -j6 REV=B BOOTLOAD=0 EXTRA_CFLAGS='-DMUL_HYBRID_CH5=1 -DMUL_PROP_MAX_AGE_MS=6000 -DMUL_TWR_TRACE=1'
python3 tools/test/check_mul_image.py bin/lps-node-firmware.elf
arm-none-eabi-nm bin/lps-node-firmware.elf | grep -E 'mulTwrTracePoll|aaTrace'
arm-none-eabi-strings bin/lps-node-firmware.elf | grep 'AA1 c='
strings bin/lps-node-firmware.dfu | grep 'AA1 c='
sha256sum bin/lps-node-firmware.elf bin/lps-node-firmware.dfu
```

Keep those hashes with the capture. Existing unrelated local edits need not be
removed; do not discard them to obtain a clean Git label. Both ALRs use this
same image; their external EEPROM settings determine address/role/plan.

The following are **operator-only commands, not run during this task**. Connect
only the intended node, repeat for132 and133, and retain the external EEPROM.
The ST-LINK sequence programs/verifies internal flash and requests a halted CPU,
with no reset-run command:

```sh
openocd -f interface/stlink.cfg -f target/stm32f0x.cfg \
  -c 'init' -c 'reset halt' \
  -c 'flash write_image erase bin/lps-node-firmware.elf' \
  -c 'verify_image bin/lps-node-firmware.elf' \
  -c 'halt' -c 'shutdown'
```

If using the board's existing DFU workflow instead, manually place only the
intended board in DFU mode, then use the address-bearing DfuSe file from the
BOOTLOAD=0 build above:

```sh
dfu-util -d 0483:df11 -a 0 -D bin/lps-node-firmware.dfu
```

Do not mix ELF/DFU from different builds. Do not mass-erase or reset configuration.
DFU manifestation, a later reset or power cycle may resume the saved RF-enable
state; only perform programming/resumption when that device operation is
separately authorized. This task neither flashed a board nor issued RF ON.

Hardware verification, when authorized:

1. Resume the intended nodes using the operator's normal authorized procedure.
   Record complete USB output, boot identity and read-only `:status`/`:plan show`.
   Confirm132/133 roles and the unchanged826414CC plan; do not edit its timings.
2. If RF is kept OFF, no AA events and therefore no AA1 records are expected.
   For a positive check use a separately authorized existing AA workload; this
   procedure does not prescribe an RF-enable command or a configuration change.
3. Confirm complete AA1 records on **both** USB streams. Match cycle/operation
   and84>85 peers. A WINDOW/TARGET/ARM should no longer remain invisible even
   when the exchange later times out. Normal UWB logs must remain present.
4. Check `lost`, stage/reason/callback fields, and compare verified TX/RX pairs as
   described below. Temporarily stopping host reads can cause bounded backpressure
   and overflow; once reading resumes, whole records should appear again. Do not
   interpret a trace gap with increasing lost as evidence of a missed RF stage.
5. Archive image hashes, status/plan output and both transcripts before drawing
   conclusions about the original AA timeout problem. USB visibility is a
   separate check from physical exchange reliability or ranging accuracy.

## Optional AA start-delay experiment

`aa_start_delay_us` defaults to0. It shifts only generated AA reservation
start/end and POLL; TDoA slots, AR placement/rates, reply and clock conversion
are unchanged. Validation conservatively requires the delayed AA reservation
to fit the minimum spacing at the plan's peak operation count, even if some
actual class orderings would permit a larger delay. Idle delay is not RF airtime.

Zero keeps codec2/3 bytes and fingerprints unchanged. Nonzero uses full-plan
codec4, a uint32 at bytes53..56 covered by the existing CRC; the64-byte encoding
and EEPROM journal layout are unchanged. Older decoders reject codec4. Use the
updated image on **both** ALRs. Other plan consumers need the updated canonical
codec before using this plan; their firmware is outside this targeted experiment.
An absent RLR does not change the generated AR reservations.

For the existing826414CC plan, send these USB console lines to **both132 and133**
(each colon-prefixed line ends with newline). No roster, mode or identity changes
are needed. First stop both nodes:

```text
:off
:status
```

Wait for `RF requested=0 running=0 restart=0` on both. Inspect `:plan show` and
confirm baseline hash826414CC before editing, then on each board:

```text
:plan set aa_start_delay_us 50000
:plan use on
:plan show
:status
```

Require `SAVED`, selected1, codec4, `aa_start_delay_us=50000`, hashE5B7BC80 on
**both**, unchanged ALR offsets0/10ms, and cycle1 preview:
`OP0 A 132>133 66100..159200us`, `POLL op=0 at=73100 us`.
Do not enable if either reports an error, different hash or restart requirement.
Edits autosave and survive reboot; repeat read-only checks after any reboot.

When the operator is ready to run the experiment, enable follower133 first:

```text
:on
:status
```

Then enable reference132 with the same two lines. Require requested1/running1
on each; capture both USB streams and match AA1 cycle/op/peers. Compare POLL TX,
follower POLL RX and RESPONSE stages with the zero-delay baseline. This experiment
has not been run on hardware and does not establish the cause of missing POLLs.
Stop **both** using `:off`, wait for requested0/running0, then use
`:plan set aa_start_delay_us 0` on both to restore hash826414CC. Leave RF OFF
until the next operator-controlled run.

## Collection and record fields

During a separately authorized operator session, retain complete USB transcripts
from **both** ALRs. Record `:plan show`, `:status` and the boot header with each
capture to bind full plan hash, role, IDs, timing and reboot/context boundaries.
No new console/RF command is required to activate tracing in the diagnostic image.
Queued records can drain after RF OFF; flags describe capture time, not print time.
Inspect `AA1` records; keep ordinary beacon/TWR logs as well. Older best-effort
print output can be truncated next to a record, so search for the `AA1` token
within raw lines rather than relying only on line starts. Do not
combine unrelated plan hashes or reboots merely because cycle IDs coincide.

```text
AA1 c=5 o=0 p=84>85 l=84 e=5 s=11 f=2A why=0 n=0123456789 t=0000000000 d=01ABCDEF12 lost=0
```

This line illustrates the format, not a measured device result. Fields:

- `c`: reference cycle ID, decimal; `o`: generated operation index, decimal.
- `p`: initiator>responder node addresses, hexadecimal; `l`: local ALR address.
- `e`: event code below; `s`: message stage in hexadecimal.
- `f`: hexadecimal snapshot flags: bit0 TX pending; bit1 cached SYS_TIME available;
  bits2..4 last entered TWR callback (`0` service timer, `1` packet received, `2` TXFRS,
  `3` receive timeout, `4` receive failure, `7` explicit start/no callback);
  a scheduler-originated cancellation can retain the preceding callback code;
  bit5 exchange active at capture. Decode `(f >> 2) & 7` for the callback.
- `n`: last existing TWR SYS_TIME read, in this local radio's native40-bit clock.
  Bit1 clear means it is unavailable and must be ignored. It is a cached service
  sample, not an exact hardware timestamp of the diagnostic or RX-enable action;
  scheduler-originated cancellation may reuse an earlier sample. No extra SPI
  reads were added for tracing.
- `t`: event-specific local marker below. `d`: existing local step deadline;
  WINDOW instead carries reservation end; TARGET, WAIT_EVENT and initial poll RX have no
  step deadline and display0. Zero markers for LISTEN/DONE are not measurements.
- `lost`: cumulative boot-local trace-ring overflow count, modulo32-bit. A
  nonzero/increasing value means events are missing; absence cannot then prove
  that a radio stage did not occur. This counts AA1 overflow, not losses in old
  printf logs, USB hardware or host recording.

All40-bit values use ten hexadecimal digits, nominal63897600000DTU/s. Compare
only timestamps from the same `l` and boot/context, using signed modulo40-bit
differences with the half-period guard. Never subtract the two ALRs' `n` values.

| e | Event | Meaning of t / stage |
|---|---|---|
|1|WINDOW|Expected local poll marker; d=reservation end. Responder records assignment even without receiving a poll. It does not prove RX hardware stayed enabled.|
|2|ARM|Programmed antenna-referenced delayed TX marker after readback/start checks; not yet TXFRS.|
|3|TX|Actual TX timestamp verified against the programmed marker.|
|4|RX|Decoded/accepted raw RX timestamp before power correction; stage is the received message. Later interval/math checks can still fail.|
|5|LISTEN|RX-enable request issued after verified TX; stage names the expected reply and d retains its absolute deadline. No new hardware RX-state readback is implied.|
|6|DONE|Local exchange completion. Initiator means verified RESULT TX; responder means validated RESULT RX. There is no sixth-message delivery acknowledgement.|
|7|FAIL|Local termination; stage is the awaited message or attempted TX stage. Pending flag distinguishes missing TXFRS from waiting RX. t is the last programmed TX marker and may belong to the prior stage if setup failed; consult TARGET.|
|8|REJECT|Packet/decode/age/window guard rejection; raw RX marker included when already available.|
|9|TARGET|Requested local TX target before timing/register guards and512-DTU alignment. Consult ARM/TX to see whether it was programmed/sent.|
|10|WAIT_EVENT|Responder receive-timeout/failure notification before any poll was accepted; s=10, t/d unused. Callback bits distinguish the two. Not an active-exchange failure.|

Stages: `10` POLL, `11` RESPONSE, `12` FINAL, `13` REPORT, `14` RESULT.
Initiator normal sequence is POLL TX → listen RESPONSE → RESPONSE RX → FINAL TX
→ listen REPORT → REPORT RX → RESULT TX → DONE. Responder is WINDOW → POLL RX
→ RESPONSE TX → listen FINAL → FINAL RX → REPORT TX → listen RESULT → RESULT RX
→ DONE. TARGET/ARM precede each TX. A WINDOW is an expected reservation, not proof
of poll receipt or continuous listening.

`why` codes are:0 normal;1 absolute step timeout;2 existing radio_event;
3 insufficient pre-setup lead/window tail;4 insufficient lead after SPI setup;
5 hardware delayed-start warning;6 TX marker mismatch;7 DX_TIME readback mismatch;
8 antenna config;9 window/epoch change;10 reservation end;11 reservation turnaround;
12 packet/decode;13 exchange metadata/prefix;14 response interval;15 final-interval
math;16 report/range;17 result;18 RX capture age;19 measurement age;
20 mandatory broadcast preemption;21 other caller cancellation;
22 poll duplicate/due/window rejection. The existing cancellation rules and log
reason strings remain unchanged.

In particular, FAIL why1 with pending0/stage11 means no accepted RESPONSE before
the deadline, while pending1 means the TX completion was not serviced/verified.
FAIL why2 plus callback3 is a receive-timeout notification, **not** a PHY-error
classification; callback4 is receive failure. If the absolute deadline already
passed, existing timeout precedence produces why1 even for a later radio callback.
why3/4 identify missed setup margins; why5 is a delayed-TX hardware warning.

## Inspect a failed cycle

Match `(plan context,c,o,p)` across both logs. A reference POLL TX without follower
POLL RX points to poll reception/processing/admission, not a proven missing
response. Check whether the follower has WINDOW and REJECT records, callback age
and its10ms beacon completion. A follower RESPONSE TARGET without ARM/TX separates
late/register/start failures from delivery loss. RESPONSE TX without reference
RESPONSE RX moves the investigation to that leg. Apply the same comparison to
FINAL, REPORT and RESULT. DONE on only the initiator is not end-to-end proof.
Missing records with overflow or incomplete transcripts remain unknown.

The supplied post-`:on` logs contain121 reference and117 follower beacon TX
markers, all reported delta0;28 reference and27 follower AA success lines, plus
AR results. Both show17 intervening cycles without AA success between68 and86.
The reference has90 timeout lines and one radio_event; the follower has one of
each. Failures lack operation/peer/stage, so these are **line counts**, not an AA
attempt denominator: multiple operations can share an ID. Legacy USB output is
also best-effort. The logs cannot identify the failed leg or prove a root cause.

For plan826414CC the10ms follower beacon precedes the23.1ms poll by13.1ms. Delayed
callback processing, RX readiness, decoding/window rejection and packet loss are
possibilities to distinguish, not established causes. No change to spacing,
process budgets,20ms replies or the old650/800ms scheduler is made here.

## STM32 RLR corroboration

The STM32 RLR is not an AA participant. With the existing AR engine enabled and
`samples on`, its existing raw capture can retain all five90-byte AA frames as
UWB3 type12 RX records before rejecting them as nonlocal AR operations. Capture
with the existing ESP collector's `--raw` option; compact logs omit native frames.
Match the RF cycle/operation, full fingerprint and sender/receiver addresses in
the raw frame. The RLR RX marker belongs to its own DW3000 clock; it does not
prove reception by either ALR. Missing RLR frames do not establish ALR packet loss.

Focused STM32 tests cover samples off/on, all five AA message types, byte-for-byte
native capture, zero RLR transmissions, no false AR result and no radio-error or
timeout increment. Nonlocal AR-rejection counters may increase; those are not AA
PHY failure counters. AA-only plans with AR disabled do not currently expose this
90-byte trace through the AR capture path. No STM32 radio code or wire format was
changed for this diagnostic task. Existing RF-off/control and passive behavior
remain in effect.

## Optional DW1000 RX-state experiment (`MUL_RX_DIAG=1`)

Build both ALRs with the same image (commands below are operator instructions;
no programming, serial access or RF operation was performed for this change):

```sh
cd ~/Documents/lps-node-firmware
make clean
make -j6 REV=B BOOTLOAD=0 EXTRA_CFLAGS='-DMUL_HYBRID_CH5=1 -DMUL_PROP_MAX_AGE_MS=6000 -DMUL_TWR_TRACE=1 -DMUL_RX_DIAG=1'
python3 tools/test/check_mul_image.py bin/lps-node-firmware.elf
```

The checked image uses130356 flash bytes (716 free) and15528 RAM bytes including
the linker stack (856 free). Both flags default off independently; AA1-only
builds retain their existing behavior. No RF protocol, settings, delays, wakeups,
recovery writes or state-machine transitions are changed. No vendor driver edit
is needed. New code only reads hardware and appends bounded diagnostic records.

RX snapshots occur on **existing software-timeout callbacks**, before their
recovery/TX setup, once at the first opportunity at/after POLL minus2ms (`k=1`)
and once at/after window end (`k=2`). There is no timer guaranteeing either
instant. On a successful exchange, the next wakeup can be much later than window
end; use `n-p` to see this. If the software never services a window, its evidence
is missing, not RX-inactive. Snapshot reads are sequential, not atomic: `n` is
SYS_TIME read before SYS_STATE and SYS_STATUS. A hardware transition may occur
between reads. No extra hardware-state/status SPI reads run in IRQ callbacks.
There is one added SYS_TIME read immediately **after each normal RX rearm** and
one RX_FINFO length read on RX callbacks; these measure requests/delivery and
still introduce observer overhead. Compare diagnostic-on/off runs if needed.

Each snapshot produces two deferred USB lines, correlated by `c/o/l/k/n`:

|Field|Meaning|
|---|---|
|`c/o/l`|Plan cycle, operation index, local address in hex; combine with AA1 `p` peers.|
|`k`|1 near POLL;2 first available timeout at/after end.|
|`n/p`|Snapshot and expected POLL, raw40-bit **local** DW1000 timestamps.|
|`h`|Raw32-bit SYS_STATE register0x19, read only.|
|`s`|Live40-bit SYS_STATUS at snapshot.|
|`i`|OR of driver's pre-clear cached IRQ SYS_STATUS since preceding snapshot; not simultaneous with `h/s`.|
|`a`|Activity bits since this operation was selected:1 RX callback delivered a90-byte frame;2 planned-AA TWR handler received an RX event;4 accepted POLL;8 AA1 REJECT. These may concern different packets; inspect AA1 stage/why.|
|`r/b` (RX2)|Most recent rearm request's post-command SYS_TIME and beacon TX's verified hardware timestamp. Zero means none recorded since RX initialization. `r` is not proof RX became active.|
|`t/f/g` (RX2)|Boot-cumulative RX timeout, RX failure and RX callback counts, modulo65536. Compare differences; these are not AA-only.|
|`m` (RX2)|Driver's requested mode:0 IDLE,1 RX,2 TX. It can disagree with hardware.|
|`z`|Dropped snapshot count modulo65536; four-record ring plus one pending USB record.|

RX1 and RX2 are each below the existing128-byte nonblocking admission limit.
They retry intact on backpressure, without another queue or blocking prints.
A capture ending between the lines can leave a partial pair. AA1 has its own
loss count. Driver W1C/reset helpers leave `dev->sysstatus` intact; copying that
cache at callback entry preserves errors already cleared in hardware. The real
libdw1000 regression test verifies this assumption. A software timeout never
reuses stale cached status as a new IRQ event. Live status still reveals pending
bits when a hardware event has not produced a callback.

Interpret `h` using [Decawave APS022 sections4.3–4.6](https://forum.qorvo.com/uploads/short-url/e3uoGFpxCLpnXC6dQYyfPW4LsBl.pdf):
PMSC state is bits23:16 (1 IDLE,2 TX wait,3 RX wait,4 TX,5 RX), receiver state is
bits15:8 (5 searching for preamble), transmitter state is bits3:0. For example,
`00050500` indicates RX/preamble search and `00010000` indicates IDLE. Preserve
unknown state values for inspection. SYS_STATUS events alone do not establish
RX enable state. Use the repository's DW1000 bit definitions: TXFRS7,
RXDFR13/RXFCG14, RXPHE12/RXFCE15/RXRFSL16/LDEERR18/RXOVRR20/RXSFDTO26/AFFREJ29,
and RXRFTO17/RXPTO21. Status history lacks individual event timestamps.

### Source audit and current evidence

The most suspicious boundaries, intentionally preserved, are:

- `followerOnEvent()` in `src/mul_alr.c`: an armed beacon suppresses ranging
  delivery/rearming until TX completion or its100ms watchdog. A lost completion
  could span the73.1ms POLL. However every failed cycle in the supplied capture
  has a follower beacon TX completion, so that scenario is not demonstrated.
- `plannedFollowerOperation()` selects using **handler-time** phase against the
  reservation, before TWR checks hardware packet timestamps. A delayed callback
  outside the window follows the unselected90-byte discard/rearm path with no
  AA1 REJECT. RX activity1 without handler activity2 helps expose this boundary.
- Successful RX/TX callbacks and error recovery all depend on normal
  `mulRxRearm()` (`dwNewReceive` → `dwIdle`/clear → defaults → RXENAB write).
  Driver RX mode expresses intent, not confirmed hardware reception. An
  ineffective command or subsequent RX stop needs the hardware `h` evidence.
- `dwHandleInterrupt()` in `vendor/libdw1000/src/libdw1000.c` clears status and
  soft-resets RX before failure/timeout callbacks; errors take precedence over
  receive-done. `src/uwb.c` services a binary IRQ semaphore and drains while the
  IRQ pin remains asserted. Callback counters and live pending status help
  separate hardware errors from missing dispatch, but do not prove an IRQ edge
  was lost. No driver or recovery fix has been made.

The supplied `alr132_delay50ms.log`/`alr133_delay50ms.log` show63 verified AA POLL
TX attempts:29 DONE on both roles and34 initiator RESPONSE-stage timeouts. None
of the34 has a follower POLL RX trace. Longest failure runs are four observed
attempts (cycles35–38 and55–58); all failed cycles have follower beacon completion
and no follower recovery text. There are5 successes/5 failures with fit `apply=1`
and24 successes/29 failures with `apply=0`; this does not establish a fit cause.
These are observed log associations, not hardware-state measurements. The fair
plan sometimes has two AA operations in one cycle or an AR-only cycle: always
correlate **cycle AND operation**, not one outcome per cycle. Existing traces
cannot establish whether RX was enabled during missing POLLs.

### Operator-only programming, configuration and60-second collection

First use the existing console on each node to issue `:off`, then `:status`;
require `RF requested=0 running=0 restart=0` before programming. Disconnect other
programmers/boards. Connect only132 to ST-LINK and run the programming commands
in the earlier “Exact build” section; repeat for133 using the **same** ELF.
Those commands verify and leave the CPU halted, preserve external EEPROM and
never mass-erase it. Resume each only after its saved RF-off state was confirmed:

```sh
openocd -f interface/stlink.cfg -f target/stm32f0x.cfg \
  -c 'init' -c 'reset run' -c 'shutdown'
```

Alternatively follow the earlier operator-only DFU instructions after saving RF
off. Never mix image files from different builds. On **each** USB console:

```text
:off
:status
:plan set aa_start_delay_us 50000
:plan use on
:plan show
```

Require RF OFF acknowledged, correct identity (132 reference/133 follower),
selected1, codec4, hashE5B7BC80 and cycle1 `66100..159200us`/POLL73100us. Stop on
any error or restart requirement; do not reset configuration or alter the roster.
Close interactive serial programs before collection. Resolve the stable device
paths (`ls -l /dev/serial/by-id/`) to the verified identities, then replace the
placeholders below. The following command **will enable RF** when an operator
runs it; it is not a read-only log command. It starts133 before132, captures60s,
then requests OFF on both and records status:

```sh
export ALR132=/dev/serial/by-id/REPLACE_WITH_VERIFIED_132
export ALR133=/dev/serial/by-id/REPLACE_WITH_VERIFIED_133
python3 - <<'PY'
import os, select, serial, time
ports, files = [], []
def collect(seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        ready, _, _ = select.select(ports, [], [], min(.05, max(0, end-time.monotonic())))
        for port in ready:
            data = port.read(max(1, port.in_waiting))
            files[ports.index(port)].write(data)
try:
    for node in (132, 133):
        ports.append(serial.Serial(os.environ[f'ALR{node}'], 115200, timeout=0, exclusive=True))
        files.append(open(f'alr{node}_rxdiag.log', 'wb'))
    ports[1].write(b':on\n'); ports[0].write(b':on\n')
    collect(60)
finally:
    for port in ports: port.write(b':off\n')
    if len(files) == len(ports):
        collect(.3)
        for port in ports: port.write(b':status\n')
        collect(1)
    for file in files: file.close()
    for port in ports: port.close()
PY
python3 tools/test/analyze_aa_rx.py alr132_rxdiag.log alr133_rxdiag.log
```

Confirm both final status lines show requested0/running0. Keep the full logs,
image hashes and pre-run plan/status with the report. If collection fails or
OFF is unconfirmed, use the consoles to stop both before further work.

|Observed pattern|Inference and limit|
|---|---|
|Near-POLL `h` IDLE/TX, old `r`, no accepted POLL|Supports inactive/interrupted RX at that sample; compare `b`, driver mode and AA1 TX events.|
|Near-POLL `h` RX search, recent `r`, no POLL RX|Weakens persistent inactive-RX theory; cannot exclude a stop after the sample, PHY loss or interference.|
|RX errors/timeouts in `i/s`, rising `f/t`|Hardware/recovery evidence; inspect exact masks. A timeout counter is not a TWR RESPONSE deadline.|
|Live RXFCG/RXDFR pending, no rising `g` or delivered activity|Supports dispatch delay/loss; a later callback may still account for it.|
|Activity1 but not2|90-byte delivery bypassed planned-AA TWR handler; inspect selection phase and armed-beacon path. Not proof that it was a valid POLL.|
|Activity8 or AA1 REJECT|Software rejected a delivered packet; AA1 why22 indicates window/due/replay gating, why18 stale RX timestamp.|
|Activity4 and AA1 POLL RX followed by failure|POLL delivery worked; investigate RESPONSE scheduling/TX and later legs.|
|Large/missing `n-p`, loss counts rising, incomplete pairs|Insufficient evidence at the expected time; do not infer RF failure or receiver inactivity.|

Subtract40-bit timestamps modulo2^40 within a node; divide DTU differences by
63897.6 for microseconds. Never compare raw timestamps across the two radios.
The existing origin/clock prediction is unchanged; deviations in expected `p`
versus decoded POLL arrival and AA1 rejection evidence can expose a timing
mismatch. Sparse state snapshots cannot conclusively eliminate transient RX
stops, RF/PHY loss or every form of event loss. No reception fix or physical
reliability claim is made by this experiment.

## Pending-RX race investigation

The newer `alr132.log`/`alr133.log` capture contains62 verified AA POLLs,
42 initiator DONE and20 FAIL. All six operation1 attempts (cycles9/19/29/39/49/59)
fail, but this alone does not prove a separate operation-index bug. The shared
planner and follower selector support both indices; operation1 has a different
phase relative to millisecond software wakeups. Reference diagnostics report
loss6, so absence of individual reference records is not conclusive.

Two source-level hazards are now reproduced by real-driver tests: `followerOnEvent()` falls
through to `mulRxRearm()` on an idle software timeout, and `uwbTask()` checks the
IRQ pin only in the semaphore-success branch. `dwNewReceive()` forces TRXOFF
and clears good-RX status. A good frame completing after semaphore timeout but
before that rearm can therefore be discarded; an already-high IRQ with no
remaining semaphore token is not drained before the next timeout callback.
The UWB task is the highest-priority application task; USB output uses zero-wait
queue writes. A higher-priority application worker is not present in this build.
Binary semaphore coalescing by itself is safe only if pending hardware events
are drained; it cannot count frames or preserve an overwritten receive buffer.
The pending-POLL regression failed on the original code because a software
wake cleared RXFCG before delivery; it now passes at both operation indices.
The correction leaves a listening generated follower untouched on software
wakeups (including a frame arriving between IRQ-pin check and timeout callback).
RX/TX/error callbacks, active TWR cancellation, reference-loss recovery and
mandatory broadcasts retain their existing recovery/preemption. Fixed/legacy
follower behavior is left unchanged. `uwbServiceIrq()` tests the actual pin as
well as the token, drains coalesced events, and stops when RF is no longer
requested. It adds a GPIO read, not a status-SPI poll. A stale token with no
hardware event produces no fabricated RX callback. The normal callback path
still uses the driver's packet/error checks; no deadline or validation relaxed.

These hazards explain a possible IDLE/good-RX signature, not the identity of the
pending frame nor every hardware failure. IDLE is normal after a completed
single-buffer RX; the defect is allowing pending evidence/frame delivery to be
lost during a software-only rearm. Persistent good-RX with no token is now
serviced on the existing timeout path. A status snapshot preceding POLL cannot
identify that pending frame as POLL. Operation1 is exposed to the same1ms wake
loop at another deterministic phase; no index-specific acceptance failure has
been reproduced. It would be unjustified to claim that all six observed failures
have one cause or that these fixes improve measured RF reliability without a
new capture. Preamble-only and later-leg errors remain separate hypotheses.

A separate optional-build defect is also corrected: RX-only diagnostics now
poll independently of `MUL_TWR_TRACE`, and trace-only cancellation metadata is
guarded when AA1 is disabled. RX-only native/sanitizer coverage is added to CI.


### Hardware verification of the pending-RX correction

Use the exact clean CH5/6000/AA1/RX build command above, and program **both**
boards with that same image using the earlier ST-LINK commands (reset halt,
write/verify ELF, halt, shutdown). Before programming each board, save RF OFF
with `:off` and verify `:status`. Resume with the documented reset-run command
only after its saved RF-off state is confirmed. External EEPROM is preserved.
No board has been programmed or enabled as part of this software verification.

For this comparison use the original plan, not the separate50ms-delay experiment.
On **each** verified132-reference/133-follower console, with RF OFF acknowledged:

```text
:off
:status
:plan set aa_start_delay_us 0
:plan use on
:plan show
:status
```

Require requested0/running0/restart0, selected1, codec2, hash826414CC, period1000,
AA1000/AR50 milliHz, reply20, offsets0/10ms. Cycle1 must show window16100..109200us
and POLL23100us. Do not change any other timing, roster, radio or RLR setting.
Cycle9 has AA operations0 and1; operation1 window498050..591150us and POLL505050us
are generated unchanged. Preserve the STM32 RLR image/configuration.

Close the interactive consoles, set the verified ALR132/ALR133 stable serial
paths, and run the **exact60-second collection Python command above**, which
starts133 then132 and requests OFF/status on both in `finally`. Use this section's
zero-delay configuration instead of the preceding50ms setup. Both final status
lines must show requested0/running0. Analyze with:

```sh
python3 tools/test/analyze_aa_rx.py alr132_rxdiag.log alr133_rxdiag.log
```

Before the correction, a pending complete frame could disappear across an idle
software wake with no RX callback; preamble search was also interrupted by the
periodic rearm. Afterward, idle wakeups leave `r` unchanged until a real RX/TX/error
callback or required recovery, and an asserted IRQ is drained even without a
semaphore token. Thus a persistent live RX-good event should now lead to a
callback/AA1 acceptance or rejection instead of being silently cleared by that
wake. A snapshot preceding POLL still cannot identify a pending packet as POLL.

Compare operation0 and operation1 separately, including all six secondary
opportunities near cycles9/19/29/39/49/59 when captured; do not assume one AA
attempt per cycle. Record valid POLL RX, RESPONSE TX and subsequent failure
stages on both nodes. If operation1 remains systematically absent, inspect its
RX-good/handler/reject evidence and actual packet timestamps before changing
its schedule. Residual preamble-only, searching-RX or later-leg failures need
new evidence; success in host tests is not a physical reliability measurement.

Local checks include real-driver pending POLL/preamble preservation at both
indices, a complete operation0→operation1 exchange sequence without remote
context mutation, coalesced/no-token IRQ dispatch and RF-off interruption.
Existing tests continue to cover absolute deadlines, radio errors, broadcast
preemption, plan validation/hash, AA/AR ownership and legacy behavior. No vendor
library source or packet codec was changed.

The simultaneous RXRFTO+RXFCG regression characterizes, rather than changes,
the driver's existing timeout-before-good-RX precedence. Such a combined status
still invokes timeout recovery and discards the buffer; sticky flags alone
cannot establish which frame they describe. Treat this as distinct from the
fixed software-only timeout race, and inspect `i`/`f`/`t` before blaming dispatch.
Standalone libdw1000 tests also pass (41). Their unmodified old Rake harness
needs process-local YAML/File API compatibility shims on Ruby3.2; direct `rake`
fails before testing. No vendor dependency or source was edited for that issue.

The old vendor harness was run without repository edits using:

```sh
(cd vendor/libdw1000 && ruby -ryaml -rrake -e 'module Psych; class << self; def load(yaml, **options); unsafe_load(yaml, **options); end; end; end; class << File; alias exists? exist?; end; load "Rakefile"; Rake::Task[:default].invoke')
```

This process-local compatibility shim loads only the checked-in test config;
it does not install gems or alter vendored code.


## Latent defects and evidence limits

The following read-only findings refer to LPS585378b and nearby STM32b97c790
(core pin506913d). No firmware, tests, settings or logs were changed. These
findings do not establish the cause of the observed late-run deterioration.

- **Confirmed full-plan fingerprint defect:** `mulRangingPlanEncode()` embeds
  the32-byte CRC-protected schedule at4..35, then CRCs0..59; fingerprint returns
  that outer CRC. A complete inner message+CRC has a fixed residue, cancelling
  the inner schedule's influence on the outer checksum. A temporary program
  linked to production core created valid revision1/slot10 and revision23/slot20
  plans with equal outer hash826414CC, distinct schedule hashesCDD12282/B46F05A1,
  and POLLs23100/33100us. A reference frame from the first plan decoded under
  the second. This affects timing/roster/revision consistency admission on both
  platforms, not merely a theoretical random CRC collision. Existing tests
  mutate outer RLR/rate fields but miss this class. Future tests should mutate
  each embedded schedule field with its checksum recomputed, require different
  identity, and reject cross-plan broadcast/TWR. A remedy requires a coordinated,
  explicitly versioned fingerprint migration; do not silently change one node.
- **Confirmed driver stack over-read:** `dwClearAllStatus()` at
  `vendor/libdw1000/src/libdw1000.c:802` gives a4-byte `uint32_t` to a5-byte SPI
  write. A temporary ASan executable linking the real driver reproduced READ of
  size5 beyond stack object `reg`. UWB startup and RF control transitions call
  it. The fifth status byte is indeterminate; no stack overwrite was shown.
  Tests should exercise this production helper with a size-checking SPI sink.
  It is not an explanation for cycle112 without evidence of a new reset/control
  transition. A future fix should provide a correctly sized explicit W1C buffer.
- **Confirmed conditional IRQ non-progress:** defaults enable RXPTO alongside
  RXRFTO, but `dwIsReceiveTimeout()` tests only RXRFTO. A temporary real-driver/
  production-drain fixture latched masked RXPTO and observed4 repeated status
  reads, zero callbacks and IRQ still high; a test-only request cutoff bounded
  the loop. An actual latched event can starve the lower-priority console in the
  unbounded UWB drain. Normal MUL does not deliberately configure nonzero
  preamble-detect timeout, and no RXPTO appeared in this capture's snapshots;
  field activation is unproven. Zero disables this timer according to the
  [Decawave driver guide](https://forum.qorvo.com/uploads/short-url/9wTyf4vAaBkReECig7wIppI9Ek9.pdf).
  Future coverage should assert every enabled event is handled/acknowledged,
  plus bounded recovery on a non-progressing IRQ. Existing coalescing tests
  cover good-RX/TX, not this masked bit.
- **Strong broadcast-preemption race:** the generated follower cancels active
  TWR before copying an incoming broadcast (`src/mul_alr.c:464`);
  `mulTwrCancel()` rearms RX (`src/mul_twr.c:199..204`). The TWR broadcast path
  does likewise before returning the buffer to its caller. A temporary fixture
  using the existing fake radio injected an overwrite at that early re-enable:
  reference sequence6 remained5. This demonstrates the ownership/order hazard;
  the injection is not a measured RF timing reproduction for the two-node plan.
  It needs an old active exchange and another reception during the copy gap.
  A future regression should model airtime/SPI latency and buffer replacement;
  preserve bytes/stamp before cancellation can re-enable RX. STM32's broadcast
  path already copies its frozen frame before rearming.

The current static-geometry capture was independently matched to the latest ALR
session by109/109 exact reference TX timestamps, not host reception times.
Discard startup/interleaved pre-reinitialization lines before counting: AA is
110/113 before cycle115 and2/10 afterward, excluding AR and startup attempts.
RLR vectors are102/116 valid; cycles112/113 first lack reference observations.
Reference TX logs continue through128; late ALR histories include RXRFSL/SFD
errors, and the follower logs reference loss. No deterministic112-cycle limit
was identified. Raw40 wraps recur much earlier and existing wrap tests pass.

For valid vectors at cycles>=20, n=91, mean=-103.66mm and sample SD=70.84mm.
Across all102 valid vectors, mean=-85.44mm and SD=109.07mm; the settling cutoff
must accompany accuracy claims. Mean AR132/133 ranges are977.6/875.83mm; their
-101.77mm difference is not independent truth. AA corrected/raw means are
2076.77/2009.11mm for112 successful AA initiator records. Power-table application
is not per-device calibration. No calibration adjustment is justified here.

RLR clock records show102/109 valid mappings,9 applied regression fits and93
changing-rate fallbacks; propagation-compensated is0/109. Replaying measured
raw TX/RX through production clock code reproduces all79 rate/fit/apply decisions
at cycles33..111 exactly. The fallback compares a long-window slope with a recent
median, retaining the latter when they disagree; mapping-valid is not a physical
error bound. `mulMultiReceiverReceive()` passes NULL delay to fitted observation,
so the generated RLR path cannot activate propagation compensation even with
that setting enabled or successful AR results. This is an implementation limit;
constant propagation does not itself bias a stationary rate estimate. Future
motion-aware compensation needs correct identity/age/domain binding.

AA1/RX dropped counters are boot-cumulative and constant within the selected
latest session:132 AA11082/RX751;133 AA9516/RX1474. Large totals do not mean
ongoing trace loss in that segment. Normal printf uses zero-wait per-byte queue
writes and can lose text independently without updating those ring counters.
AA1 can also attribute an unrelated90-byte rejected packet to its retained old
AA context. Likewise AR rejected counters include overheard AA traffic. Treat
such records as diagnostics, not additional failed assigned exchanges.

Local audit checks passed250 LPS MUL tests under ASan/UBSan (runners/binaries only
in `/tmp`), the portable sanitizer suite, and STM32 runtime/config/AR sanitizer
suite. `tools/test/mul_core.py` fails current canonical/vendor byte agreement:
only `mul_ranging_plan.[ch]` and its test differ after the LPS-only delay option;
the STM32 pin verifies independently. That tooling failure is not evidence of
incompatibility for the current codec2 plan. Codec4 remains unsupported there.
The existing combined-diagnostics ELF passes image checks with716 flash bytes
and856 RAM bytes free; this audit did not rebuild or measure stack watermarks.
