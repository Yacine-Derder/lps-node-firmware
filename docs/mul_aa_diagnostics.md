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
