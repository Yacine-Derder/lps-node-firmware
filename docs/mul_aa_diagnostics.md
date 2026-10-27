# Generated AA TWR stage diagnostics

The optional LPS build flag `MUL_TWR_TRACE=1` adds local USB diagnostics to both
ALRs. It defaults to0. Both ALRs use the same image; persisted addresses/roles
still select initiator/responder. No on-air bytes, plan hash, scheduling constants,
reply/deadline values, EEPROM settings, calibration, RX policy or RF-OFF admission
checks change. Legacy fixed-scheduler/AR exchanges retain their existing logs;
AA1 records are restricted to generated AA operations. No hardware was flashed or
RF started while implementing/testing this feature.

## Build and test without hardware

From lps-node-firmware:

```sh
python3 tools/test/mul_slots.py --mul-only --sanitize --prop-age 6000 --slot 10 --twr-trace
make clean
make -j6 REV=B BOOTLOAD=0 EXTRA_CFLAGS='-DMUL_HYBRID_CH5=1 -DMUL_PROP_MAX_AGE_MS=6000 -DMUL_TWR_TRACE=1'
python3 tools/test/check_mul_image.py bin/lps-node-firmware.elf
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
reviewed image uses127692 flash bytes (3380 free) and15136 RAM bytes including the
linker stack (1248 free). Existing main/UWB/idle stacks and USB queue are unchanged.
Reviewed ARM static frames are80 bytes for the trace producer,240 for the USB
consumer, and40 for main; the consumer is never nested in radio processing.
Static image/frame checks do not prove physical deadlines or stack watermarks.

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
