# MUL architecture

Durable design, constraints, and current implementation for Mobile UWB Localization.
See [the roadmap](../PLAN.md) for milestone status and
[AGENTS.md](../AGENTS.md) for repository operating rules.
The code defines implemented behavior; planned extensions below are not firmware features.

## Hardware and portability

- ALRs are WaveRover robots equipped with Bitcraze DW1000 LPS nodes.
- RLRs are e-puck robots intended to use custom STM32 + DWM3000 UWB turrets.
- Current firmware prototyping uses DW1000 LPS nodes for both roles.
- This firmware currently configures channel 2 in `src/uwb.c`.
- The normal channel-2 configuration cannot interoperate directly with the
  e-puck channel-9 DWM3000 configuration. Stage 3F adds an explicit opt-in
  diagnostic channel-5 profile with user-validated bidirectional CRC/payload
  reception. Packet delivery and combined UART timing retain the limitations
  recorded in the PHY experiment report.
  DW3000 hardware supports channels 5/9, with limited DW1000 compatibility on
  channel 5; the families are not register/API interchangeable.
  See [Qorvo's DW3110 specification](https://www.qorvo.com/products/p/DW3110)
  and [migration guidance](https://forum.qorvo.com/t/dwm1000-to-dwm3000-migration/10120).
- [Stage 3F evidence, canonical packet and hardware procedure](phy_interop.md)
  define the reversible packet-only experiment. PHY_INTEROP_ROLE=0/1/2 selects
  off/TX/RX at build time; normal defaults, EEPROM and MUL math stay unchanged.
  Diagnostic state overlays the same 128-byte mode-exclusive reservation. There
  is no portable MUL core, mixed-radio ranging or timestamp-model port yet.
- Future cross-platform support requires a deliberate hardware/PHY/protocol
  strategy and physical validation; blanket incompatibility is not a chip-family fact.
- Keep wire definitions and timestamp arithmetic independent of the hardware driver.
  Do not add DW3000 support or a generic radio abstraction ahead of a concrete task.

## Localization architecture

- Downlink TDoA: ALRs transmit; RLRs receive and eventually localize.
- ALRs and RLRs may all move, and ALR positions are uncertain.
- The long-term system is infrastructure-free and decentralized.
- A reference ALR is an initial development simplification, not a final centralized
  architecture commitment or an absolute clock reference.
- The two-slot reference/follower arrangement is defined in PLAN.md.
  Follower TX requires a fresh reference observation; it does not free-run.

## Current firmware boundaries

| Component | Responsibility |
|---|---|
| `src/main.c` | USB configuration menus and mode/address persistence requests |
| `inc/uwb.h`, `src/uwb.c` | Algorithm interface, registry, PHY setup, IRQ/task dispatch |
| `src/cfg.c`, `src/eeprom.c` | Existing persistent configuration |
| `inc/mul_common.h`, `src/mul_common.c` | Wire encoding/validation and modular timestamp utilities |
| `inc/mul_alr.h`, `src/mul_alr.c` | Reference delayed TX and reference-following follower slots |
| `inc/mul_rlr.h`, `src/mul_rlr.c` | Passive RX and bounded per-source observation state |
| `inc/mul_rx.h`, `src/mul_rx.c` | Shared DW1000 RX setup, bounded reads, and re-arming |
| `inc/mul_power.h`, `src/mul_power.c` | Optional Bitcraze RX power correction with raw preservation |
| `inc/mul_observation.h`, `src/mul_observation.c` | Bounded RLR observations and ordered association |
| `inc/mul_relation.h`, `src/mul_relation.c` | Versioned simultaneous clock transport and inverse |
| `inc/mul_downlink.h`, `src/mul_downlink.c` | Pair/model binding and explicit rate-domain TDoA |
| `inc/mul_propagation.h` | Explicit propagation-age and relative-motion policy |
| `inc/mul_localization.h`, `src/mul_localization.c` | Caller-owned localization observation and compact OBS output |
| `inc/mul_role_state.h`, `src/mul_role_state.c` | Mode-exclusive fixed scratch storage |
| `inc/mul_clock.h`, `src/mul_clock.c` | Clock rate, arrival mapping, and delay-aware correction |
| `inc/mul_twr.h`, `src/mul_twr.c` | Directed TWR protocol, bounded radio exchange and live delay input |
| `vendor/libdw1000`, `src/dwOps.c` | Existing radio driver and board SPI operations |

Both MUL roles implement `uwbAlgorithm_t.init` and `onEvent` callbacks.
The UWB task waits on its IRQ semaphore with the timeout returned by the role.
USB configuration remains in the existing main task; no MUL timer subsystem exists.

Mode IDs remain: 0 TWR Anchor, 1 TWR Tag, 2 Sniffer, 3 TDoA Anchor V2,
4 TDoA Anchor V3, 5 MUL ALR, and 6 MUL RLR.
The serial `m` menu lists/selects modes; stored changes take effect after reset.
Node addresses and modes use the existing EEPROM layout with no MUL-specific fields.
Mode 5 selects roles via `mulAlrRoleForAddress()`, comparing all eight address bytes.
`84:00:00:00:00:00:cf:bc` is reference TX; `85:00:00:00:00:00:cf:bc` is follower TX.
Unsupported mode-5 identities remain idle. There are no new EEPROM/menu fields.

## Current packet protocol

Reference and legacy broadcasts are 34 bytes; versioned follower broadcasts are
74 bytes, excluding the two-byte hardware-generated FCS.
Multibyte fields are explicitly encoded little endian; C struct padding is not on wire.

| Byte offsets | Width | Current meaning |
|---|---|---|
| 0–1 | 2 | FCF `0xdc41`: data frame, compressed PAN, extended addresses, no ACK |
| 2 | 1 | MAC sequence: low byte of MUL sequence |
| 3–4 | 2 | PAN `0xbccf` |
| 5–12 | 8 | Broadcast destination, all `0xff` |
| 13–20 | 8 | Configured source address |
| 21–23 | 3 | Protocol identifier ASCII `MUL` |
| 24 | 1 | Legacy/reference type `1`, extended follower type `2` |
| 25–28 | 4 | Unsigned MUL sequence number |
| 29–33 | 5 | Expected antenna-referenced TX timestamp, radio time units |

The eight-byte source comes from `uwbConfig_t.address`; byte 0 is the EEPROM node ID.
The existing address base has bytes 6/7 equal to `0xcf`/`0xbc`, with middle bytes zero.
Diagnostics show the node ID; future source tracking should retain full identity.
The sequence restarts at zero on role initialization and wraps as an unsigned 32-bit value.
Decoding checks exact length, FCF, PAN, destination, identifier, type, and MAC-sequence
consistency before extracting fields. Stage 1's shorter frame is not accepted now.
The type-2 extension below supplies clock-model metadata. No position fields exist.

## Hardware timing and synchronization

Each DW1000 has an independent hardware clock. Preserve the source clock domain
with every observation; similar numeric timestamps do not imply a shared origin.
DW1000 timestamps are 40-bit values stored in `uint64_t` and encoded as five bytes.
They count at nominally `499.2 MHz × 128` and wrap approximately every 17.2 seconds.
`mulTimestampDifference(a, b)` is signed modular `a - b`; interpretation is
unambiguous only within half a counter period, with the half-period case negative.

The reference ALR uses hardware-delayed TX at approximately 1 Hz.
Raw targets are aligned to 512 ticks, normally prepared 20 ms ahead, and advanced
by one hardware-clock second rather than one second after callback completion.
The packet timestamp is `(verified DX_TIME + live TX_ANTD) modulo 2^40`.
`DX_TIME` specifies the raw RMARKER, not the start of the preamble; no arbitrary
preamble or software-latency correction is added.
Late setup, delayed-start error flags, and missing completion cause skip/abort/recovery.

On TX completion, diagnostics include scheduled, packet, and actual TX timestamps.
The signed delta compares the actual hardware TX timestamp with encoded packet bytes.
The RLR captures `dwGetRawReceiveTimestamp()` before re-arming RX and logs both
embedded ALR TX time and local raw RX time. It does not transmit or estimate range.
Malformed frames, RX timeouts, and RX failures also re-arm reception.

Stage 2 timing is hardware validated (user-reported); software tests alone do not
establish physical accuracy or a diagnosis for earlier timestamp discrepancies.
Configured antenna-delay compensation is not proof of physical antenna calibration.

A transmitted ALR timestamp cannot be directly subtracted from another robot's
RX timestamp to obtain propagation time or distance. Relative offset, skew, and
propagation delay are distinct effects; one-way observations alone cannot identify
exact clock offset independently of propagation delay.
Coarse TDMA alignment uses a reference ALR and one follower ALR.
Delay-corrected conversion is conditional on ranging assumptions; decentralized
operation and independently bounded synchronization accuracy remain future work.

## Reference-following slots and bounded source observations

The follower starts in RX and accepts only the full reference identity. A first
valid packet acquires; three seconds without forward sequence progress loses acquisition.
RTOS ticks provide only this wrap-safe coarse watchdog.
A stale stream may restart from a different sequence; replaying the last sequence
alone cannot reacquire. Duplicate, old, unknown, or malformed packets never arm TX.

Each fresh reference sequence permits one attempt in the follower's local clock:
`antenna target = local reference RX + MUL_FOLLOWER_SLOT_MS`; `raw DX_TIME = align_up(target -
live TX_ANTD, 512 ticks) modulo 2^40`. The encoded time is verified DX_TIME plus
TX_ANTD, so intended antenna-referenced separation is the configured local slot
to within 511 ticks.
The reference's embedded TX time is stored/logged, never used as a local TX target.
Follower packet sequences are independent of reference sequences and survive reacquisition.

RX turns directly into delayed TX through `startDelayedTx()`, shared with the reference.
The just-received reference updates the bounded clock estimator before metadata
encoding; estimation does not steer the hardware target. All logging remains
after radio programming.
No RX is enabled while the delayed TX waits in idle. Completion captures TX_STAMP,
re-arms RX, and logs role, scheduled, packet, actual, and signed packet delta.
RX errors/unexpected radio events cancel a pending slot, except stale TWR RX IRQs,
which cannot displace its already-armed broadcast. An absolute local-hardware
watchdog aborts if completion is missing 100 ms after the target; early wakeups
leave TX armed. Late setup (under 5 ms lead), register mismatch, or delayed-start
errors skip the attempt and return to RX. No slot is moved forward or retried.
Declared reference loss aborts pending TX; no missing superframe is extrapolated.

This is coarse TDMA, not clock synchronization: the slot uses the follower's
nominal clock rate and includes propagation/clock-rate effects. The follower
cannot receive during its configured delayed-TX wait.
RLR slot-separation tests compare its two local RX timestamps, not remote TX clocks.

RLR state is fixed to two supported full identities. Each slot counts valid received
packets, skipped sequence positions, latest-sequence duplicates, and out-of-order
packets independently. Forward advances use modulo-2^32 ordering below half a period.
Only accepted observations update the source TX/local RX timestamp pair.
After three seconds without accepted progress, a different sequence rebases that
source (e.g. a reboot), without changing other sources. Counters persist and wrap
as uint32 values; late arrivals do not undo counted gaps. No epoch field exists,
so a delayed different packet after staleness is indistinguishable from a restart.
Duplicate/old packets are counted quietly; the next fresh log reports the counters.
There is no independently calibrated offset or geometric TDoA measurement.
Stage 3C retains and associates these observations as described below.
See [the focused reuse audit](mul_stage3_reuse_audit.md) for implementation decisions.

## Configurable spacing experiment (Stage 3D.1)

`inc/mul_timing.h` is the shared compile-time authority. All three nodes must use
the same `MUL_FOLLOWER_SLOT_MS`; unsupported values fail compilation. Packet
sizes/types, EEPROM, PHY, antenna/power corrections and mathematical clock/rate
conversions remain unchanged. Default stationary-rate policy is still disabled.

| Slot (ms) | RLR raw RX / converted TX window (ms) | Reference presence (ms) | Candidate / rate age (ms) |
|---|---|---|---|
| 500 (default) | 450..550 | 450..600 | 700 |
| 50 | 45..55 | 45..60 | 250 |
| 10 | 8..12 | 8..14 | 210 |

Windows are inclusive: +/-10%, with a minimum 2 ms tolerance. Coarse age retains
the original additional 200 ms margin; it does not override hardware ordering,
independent 900..1100 ms cadence, source/generation checks or relation lifetime.
The publication helper's generic 600 ms maximum future query remains a safety
cap, not a scheduled offset. Origin/delay age fields are computed at actual TX.
TWR remains at 650..730 ms, cutoff 800, preparation 630: no supported slot
collides, and moving the exchange would add an unnecessary experimental variable.
A stalled old exchange loses ownership to broadcasts; it cannot move a slot.

Ten-ms timing is experimental. At 48 MHz CPU and normal 12 Mbit/s SPI, a generous
1 KiB transfer allowance costs 0.69 ms (2.74 ms at initialization's 3 Mbit/s).
Allowing up to 2 ms from RMARKER to completion for the supported reference PHY
choices leaves roughly 2.3 ms CPU/dispatch budget before the existing 5 ms
post-setup lead guard. This is an engineering budget, not measured WCET. Follower
processing uses bounded integer clock/model calculations and 74-byte encoding;
late setup skips safely, never transmits at an invented later target. RLR captures
34-byte reference RX and optional power correction before rearming, then performs
association/logging. Rearm should finish conservatively within 8 ms to precede the
follower preamble even with the long preamble option. Highest task priority and
nonblocking serial help, but HAL_MAX_DELAY SPI and IRQ/float-library latency mean
there is no finite source-proven wall-time bound. Host tests verify guards and
state transitions, not loss-free RF operation. Measure skips/loss on-device; if
10 ms fails, use 50 ms without changing guard, PHY or clock mathematics.

Build baseline: `make clean && make`. For the stationary comparison, use:

```sh
make clean && make EXTRA_CFLAGS="-DMUL_EXPERIMENTAL_STATIONARY_RLR_RATE=1 -DMUL_FOLLOWER_SLOT_MS=500"
make clean && make EXTRA_CFLAGS="-DMUL_EXPERIMENTAL_STATIONARY_RLR_RATE=1 -DMUL_FOLLOWER_SLOT_MS=50"
make clean && make EXTRA_CFLAGS="-DMUL_EXPERIMENTAL_STATIONARY_RLR_RATE=1 -DMUL_FOLLOWER_SLOT_MS=10"
```

Initialization reports slot_ms; RLR reports rate_unit=ppb. Existing adjacent
Stage 3E consolidates paired diagnostics into OBS, retaining sequences, signed
DTU/mm, rate, delay/relation age, power availability and UNKNOWN uncertainty.
`python3 tools/test/mul_slots.py --sanitize` reproduces native/sanitizer coverage
for all slots; add `--slot 500 --default-rate` to check default unavailable-rate
policy. Existing GCC/Ruby/Rake tooling and initialized submodules are required;
configs, binaries and logs stay temporary, with no dependency changes.

The independent physical oracle constructs absolute SI-time/metre observations
for three oscillators and checks production output against analytical geometry,
including wrap. +/-10 ppb (0.010 ppm) receiver-rate error gives approximately
-/+1.499, 0.150, 0.030 m bias at 500/50/10 ms. A deliberately arrival-aligned
follower origin gives approximately +2 m at every slot in its 2 m geometry.
These are diagnostic examples, not an explanation fitted to hardware. Compare
fixed and spacing-dependent components without fitting an online correction;
rate-estimator noise and scheduling/environment changes can confound the test.
Hardware comparison and unresolved physical results are tracked in PLAN.md.

## Diagnostic relative clock rate

For consecutive accepted reference observations, `ratio = delta_local_RX /
delta_reference_TX`, using signed 40-bit modular differences in each clock domain.
The local affine approximation uses unwrapped clock coordinates over short intervals
with approximately constant propagation delay; its intercept combines delay and offset.
Positive `skew_ppm = (ratio - 1) * 1e6` means the follower clock runs faster.
Motion/path-delay changes can also affect the measured slope.

The estimator computes `(delta_local - delta_reference) * 1e9 / delta_reference`
with bounded signed integer arithmetic and nearest-ppb rounding (0.001 ppm).
It avoids near-unity floating-point cancellation and uses no float operations.
The median of the latest five plausible intervals suppresses up to two outliers.
State becomes VALID only with five intervals and at least three within 1 ppm
of the median; otherwise it is WARMUP. This is a convergence heuristic, not a
statistical confidence bound. Accepted-sample count saturates rather than wrapping.

Full reference identity and packet validity are required. Duplicate/old sequences
do not update history or age. Actual measured intervals handle missing packets;
the current 1 Hz reference cadence must agree with sequence advance within 100 ms.
Intervals must be positive, at most 3.1 hardware seconds, and have coarse elapsed
time strictly below the existing three-second liveness deadline. This bounds them
below the 40-bit half period and rejects long-gap full-wrap aliases. Coarse uptime
only rejects stale data; it never supplies the ratio. A ±200 ppm plausibility gate
is deliberately generous for initial hardware investigation, not a device guarantee.
Invalid intervals/rates clear history and seed a new baseline. Initialization,
reference loss, and subsequent reacquisition start cleanly; five new valid intervals
are required. Without an epoch field, restart/replay ambiguity remains: backward
sequences are ignored until existing liveness declares loss and permits rebasing.

The follower logs CLOCK after arming TX or rearming RX, at most once per
fresh reference plus loss transitions, in the UWB task. Only skew is printed,
with sign and three ppm decimals. Broadcast hardware targets remain unchanged; no estimator or mapping output
steers the radio clock or slot scheduling.

## Arrival-aligned affine mapping

Let `a = 1 + skewPpb/1e9`. One-way broadcasts observe `r_f ≈ a*t_ref + c`, where
`c = b + a*tau`, `b` is true clock offset and `tau` is one-way delay in reference
units. Neither `b` nor `tau` is separately identifiable. A VALID arrival-aligned
mapping is always UNCALIBRATED for true synchronization; no large raw intercept
is computed or reported.

The existing estimator's latest accepted pair is the origin `(tx0, rx0)`:
`forward(t) = rx0 + a*diff40(t, tx0)`; inverse is
`inverse(r) = tx0 + diff40(r, rx0)/a`, with outputs reduced modulo 2^40.
`mulClockMapRefToFollower()` maps the full allowed reference identity to the full
follower identity; `mulClockMapFollowerToRef()` maps arrival-aligned coordinates
back. The inverse inherits the same delay ambiguity and must not be applied to
arbitrary follower TX times as if they were simultaneous-clock coordinates.

No mapping state duplicates the skew estimator: accepted observations re-anchor;
duplicate/old/invalid packets cannot refresh it. Skew invalidation, discontinuity,
loss, or reset immediately invalidates the mapping until convergence returns.
Queries require valid slope and estimator, origin age <=2000 coarse ms, and signed
reference displacement within +/-2 nominal hardware seconds. Inverse checks the
equivalent reference displacement. Both age and coordinate horizon apply separately;
past queries are supported. Long/half-period displacements are rejected. Callers
must know the intended counter epoch: a 40-bit timestamp alone cannot expose an
extra full wrap. There is no shared absolute timeline.

Integer conversion scales only bounded displacements. The inverse computes
`d_local - round(d_local*skewPpb/(1e9+skewPpb))` to avoid multiplying the entire
interval by 1e9. Rounded output preserves one-DTU resolution; round trips differ
by at most one tick. The origin is an actual observation, not a fitted/calibrated
intercept, so re-anchoring also follows RX measurement and path-delay changes.

Mapping results distinguish these uncertainties with explicit UNKNOWN flags:
physical rate error, reference TX measurement, follower RX measurement, origin,
absolute propagation delay, propagation changes, extrapolation, and total error.
No finite physical bound is established, even at the origin with zero sample spread.
`observedRateSpreadPpb` is sample max deviation, not a confidence/error bound.
Separately, `numericalBoundDtu = 1 + ceil(abs(d_ref)/1e9)` conservatively covers
conversion rounding plus the 1-ppb slope representation allowance, in destination
ticks. It grows with extrapolation but excludes unknown physical errors. Timestamp
encoding resolution is one DTU; scheduling alignment is not measurement accuracy.
Age and displacement are reported separately; no unjustified noise variance is used.

MAP logs follow clock updates after radio setup, with slope, state, age/horizon,
`sync=UNCALIBRATED`, and `bound_ns=UNKNOWN`; loss logs INVALID. Future TDoA work
must retain source domains, validity, delay ambiguity, and uncertainty, and obtain
independent delay information before treating this mapping as true clock conversion.

## Externally supplied propagation delay

The arrival-aligned APIs above remain unchanged. Separate APIs
`mulClockMapRefToFollowerSimultaneous()` and
`mulClockMapFollowerSimultaneousToRef()` accept a `mulClockDelay_t` record.
Writing the arrival mapping as `F`, corrected mapping is `S(t)=F(t)-a*tau`,
and inverse is `S^-1(l)=F^-1(l+a*tau)`. Input `t` and output `l` represent the
same physical instant under the supplied delay/model assumptions. Delay is
reference-clock DTU; `a*tau` is follower DTU. Code rounds that correction once
and applies the matching modular subtraction/addition. Independent calls do not
mutate the model or arrival APIs, and must never re-correct an already converted
coordinate. Forward/inverse consistency does not establish physical accuracy.

The producer must supply an interpreted ONE-WAY estimate for the full directed
reference `84:00:00:00:00:00:cf:bc` to follower `85:00:00:00:00:00:cf:bc` link,
reference-DTU units, validity, measurement uptime, latest reference sequence,
current local epoch, age policy, measurement error bound and motion-change-rate
bound. `UINT64_MAX` explicitly means UNKNOWN for bounds; zero asserts a bounded
ideal component and is not a default calibration. A raw RTT or a delay expressed
in follower units cannot be passed unchanged. The live TWR producer below supplies
this contract; synthetic distances exist only in host tests.

Initial application policy permits nonnegative delay <=63898 reference DTU
(approximately 1 us/300 m) and a caller-selected age limit of 1..6000 ms.
The live default remains 1000 ms; continuous operation explicitly selects 6000. These are prototype applicability limits, not hardware range
or motion guarantees. Validity checks wrap-safe age, full ordered identities,
units, and epoch. At the established 1 Hz cadence the sequence guard allows
three advances for policies <=2000 ms; longer policies allow ceil(maxAge/1000)+2
advances. The original uptime age still applies and full-wrap aliases fail closed.
Both the affine origin and query must be covered: application span is the maximum
of measurement age and `abs(measurement_age-origin_age)+ceil(abs(d_ref)/DTU_per_ms)`.
It must fit the supplied age limit in addition to the existing affine horizon.
This conservative envelope supports past/future queries without assuming an old
stationary measurement stays valid after motion.

Each clock reset (initialization, loss, detected restart/discontinuity) issues a
new process-local 64-bit epoch, invalidating old records even after reconvergence.
Ordinary re-anchoring retains the epoch; invalid affine state always rejects
correction. Tokens are scoped to this firmware boot/UWB task, never persisted;
producers must discard records on reboot and acquire fresh data, never retag old
measurements. Token exhaustion fails closed. Reference broadcasts still lack an
epoch; Stage 3D transports only the follower acquisition token. Undetected remote
restart remains subject to existing sequence/loss policy.

Results keep affine uncertainty separate from supplied delay uncertainty.
Motion uncertainty grows as declared `motionBoundDtuPerMs * applicationAgeMs`;
UNKNOWN inputs or overflow remain UNKNOWN. `conditionalDelayBoundDtu` scales
measurement-plus-motion bounds upwards into destination units (by `a` forward,
unity inverse), conditional on the estimated slope. Rate uncertainty, affine
origin/measurement errors and extrapolation remain UNKNOWN, so the total physical
bound remains UNKNOWN even with an ideal delay. Numerical rounding and slope
representation allowances are reported separately from physical error bounds.
Arrival VALID, delay VALID, corrected conversion AVAILABLE, and fully bounded
synchronization accuracy are distinct; success currently establishes only availability.

Live SYNC availability requires both this contract and a valid affine model;
CLOCK/MAP diagnostics retain their original meaning and `sync=UNCALIBRATED`.
Ranging acquisition does not implement Stage 4's online ranging-error calibration
or independently identify exact offset from broadcasts alone.

## Live inter-ALR ranging

Use asymmetric double-sided TWR, following the six-marker method in Bitcraze's
`uwb_twr_tag.c`/`uwb_twr_anchor.c` and [Decawave APS013](https://forum.qorvo.com/uploads/short-url/x34DrF7EW5fQP9wY3aNESqPKz8z.pdf).
Single-sided TWR with 20 ms replies would require trusted drift compensation;
symmetric DS requires matched replies. Asymmetric DS handles independent rates
and unequal replies without feeding the broadcast skew filter into ranging.

| Message | Direction | Six-marker ownership |
|---|---|---|
| POLL | reference → follower | `P1` reference TX, `P2` follower RX |
| RESPONSE | follower → reference | `P3` follower TX, `P4` reference RX |
| FINAL | reference → follower | `P5` reference TX, `P6` follower RX |
| REPORT | follower → reference | Echo all six; RESPONSE TX stamp already verified |
| RESULT | reference → follower | Confirm all six and accepted reference-unit ToF |

All messages use a separate versioned 84-byte MUL frame (excluding FCS). The 21-byte
extended-address header is directed, without ACK; bytes 21–23 are MUL, byte 24
is type 0x10..0x14, bytes 25–28 are the reference broadcast sequence/exchange ID,
29–58 hold six five-byte markers, 59–63 the message TX timestamp, 64–67 RESULT
ToF as uint32 reference DTU (zero otherwise). Byte 68 is extension version 1;
69–83 hold three five-byte P2/P4/P6 records: signed int16 centi-dBm, signed int8
correction DTU, uint8 status, uint8 table. Fields are explicit little endian; no
C padding goes on wire. Unused future markers/records are zero. Both ALRs must
upgrade: old 68-byte TWR packets are explicitly rejected, not misinterpreted.
Legacy and current TWR RX IRQs still cannot displace an armed follower broadcast.
The POLL marker is echoed as a nonce; full identities, type/stage, ID, prefix
timestamps, correction metadata, lengths and reserved values are checked. Reference
broadcasts remain 34 bytes; follower broadcasts are now 74 bytes;
the passive RLR rejects every TWR frame and continues independent source tracking.

Define reference intervals `Ra=diff40(P4,P1)`, `Da=diff40(P5,P4)` and follower
intervals `Db=diff40(P3,P2)`, `Rb=diff40(P6,P3)`. Under constant clock rates and
reciprocal constant delay, `a=(Rb+Db)/(Ra+Da)`. Thus the asymmetric numerator
`Ra*Rb-Da*Db`, divided by `2*(Rb+Db)`, yields ToF in REFERENCE units. This is the
domain-specific equivalent of the usual mixed-clock DS denominator, avoiding an
implicit nominal-clock-unit approximation. No absolute cross-clock subtraction
occurs. Each interval must be 15..30 ms, inferred rate within ±200 ppm, numerator
nonnegative and rounded ToF <=63898 DTU. Bounded integer products fit uint64.
Distance diagnostics use `tof*299792458/63897600000` metres (vacuum convention).

Initiation is once per eligible reference sequence (nonzero multiples of five),
only after receiving the follower broadcast in the configured presence window
(default 450..600 ms) into that local frame.
Prepare at 630 ms, hardware POLL at 650 ms, then each next message 20 ms after
its local hardware RX. Expected markers are near 650/670/690/710/730 ms; follower
accepts POLL only 600..700 ms after both its own reference RX and remote reference TX.
An absolute 800 ms frame cutoff and 50 ms step deadlines bound the exchange;
targets need 5 ms setup lead and 20 ms margin before the cutoff. The next reference
is prepared near 980 ms. All deadlines are local-domain modular hardware times;
RTOS waits merely wake the event loop. No holdover or shared clock is assumed.

Broadcasts own the radio: no TWR dispatch during an armed broadcast; a new
broadcast cancels ranging. Reference preparation preempts TWR even after stalled
IRQs. Priority-cancellation logs are deferred until broadcast radio setup finishes.
Timeouts, invalid packets, RX/TX errors, timestamp mismatch or late setup abort
without same-frame retry and restore RX. Each TX uses aligned DX_TIME, live TX_ANTD,
readback, error flags and actual TX_STAMP verification. RX reads RX_STAMP, already
hardware compensated using configured antenna delays, before re-arming. No
legacy hardcoded antenna offset is added; optional power correction follows the
receiver ownership policy below. Scheduling guards/replies still use raw RX_STAMP. RX setup writes only RX/IRQ
controls, avoiding full driver commits that overwrite live antenna calibration.

The follower publishes delay only after matching RESULT, not after its REPORT:
this confirms the initiator verified its own actual POLL/FINAL TX stamps. REPORT
echo checks also retain the responder's verified RESPONSE TX. There is no delivery
ACK for the last confirmation; either node can report a failure without peer
delivery certainty, but an incomplete six-marker exchange cannot publish delay.
Measurement time is committed after RESULT, backdated to raw POLL RX by rounded-up
hardware age; RESULT delivery and broadcasts cannot refresh it. Epoch and acquisition
reference ID remain bound to that measurement. Strict 1000 ms policy makes SYNC
intermittent; Stage 3E explicitly permits bounded six-second reuse. Expiry timers
run even without RX, without cancelling armed broadcasts.

Reciprocity, nearly constant propagation/rates during the exchange, and correct
per-device antenna-delay estimates are assumptions, not calibration evidence. Multipath,
NLOS, motion and rate/antenna bias remain unbounded. Delay measurement and motion
bounds stay UNKNOWN, as does total corrected physical error. A plausible ToF and
AVAILABLE correction do not establish centimetre accuracy or exact synchronization.

## Antenna reference and initial calibration

Stage 3B.2b.3 found a real omission: `uwbInit()` explicitly cached zero antenna
delay and its full commit wrote zero into both TX_ANTD and LDE_RXANTD. The
original Bitcraze TWR modes add half their `ANTENNA_OFFSET=154.6 m` equivalent
to TX and subtract half from RX in software. MUL omitted that software adjustment
while assuming the hardware registers supplied it. Consequently six-marker TWR
included analog path delay; equal scheduled/actual TX stamps only proved scheduling.
The previous architecture's antenna-reference wording overstated physical calibration.
The tests had assumed already-corrected RX and antenna emission timestamps.

The [DW1000 manual](https://fcc.report/FCC-ID/2AAXVTNTMOD1/2787937.pdf),
register descriptions for DX_TIME, TX_TIME, TX_ANTD and LDE_RXANTD distinguish
raw aligned DX_TIME (low nine bits ignored), TX_STAMP = TX_RAWST + TX_ANTD, and RX_STAMP with LDE_RXANTD
already subtracted. `dwGetRawReceiveTimestamp()` reads RX_STAMP, not RX_RAWST;
“raw” here excludes only the driver's optional received-power bias table.
No preamble scheduling offset, extra timestamp correction or DS equation change
is needed. Markers remain 40-bit modular values with separate clock domains.

Only mode 5 now calls `mulAntennaConfigure()` after the final radio full commit.
`inc/mul_rx.h` centralizes the initial estimate: round(154.6 * 63897600000 /
299792458) = **32951 DTU aggregate**, approximately 515.684 ns. This reuses the
same-board/channel historical [Bitcraze TWR estimate](https://github.com/bitcraze/lps-node-firmware/blob/master/src/uwb_twr_anchor.c),
not an empirically fitted subtraction of the reported range. TDoA3's same constant
is a minimum-ToF gate, not a calibrated correction to copy blindly.
[APS014](https://esdocs.com/doc/468243/application-note--aps014), table 4, apportions
aggregate delay 44% TX / 56% RX for wireless synchronization: rounded TX_ANTD
**14498 DTU**, remainder LDE_RXANTD **18453 DTU**. This is consistent in scale
with the Qorvo DecaRanging initial estimate 515.371 ns (about 32930 DTU), but
neither value establishes calibration for these individual boards or other PHYs.

The unchanged driver caches one delay and full-commits it to both directions.
MUL caches TX only, writes both registers explicitly, and uses RX/IRQ-only setup;
`dwNewConfiguration()` does not read antenna delays, and RX/TX `dwSetDefaults()`
does not change them. Before each ranging TX and event, live values and TX cache
must match the policy. Missing, zero, changed or full-commit-overwritten settings
abort with `reason=antenna_config`, invalidate the delay and restore RX. Reboot
restores initialization; no silent mid-exchange repair is attempted. Original
modes 0–4 retain zero registers and their existing compensation; mode 6 remains
passive with its prior RX configuration. PHY and EEPROM are unchanged.

After RESULT each ALR logs actual local registers (`ant=TX/RX`, `cal=LPS`),
six transported markers (`P` in P1..P6 order, ten hex digits), four local intervals
(`I` in Ra,Da,Rb,Db order), and correction metadata (`Q`, described below). ToF,
selected distance and raw distance are explicit. The maximum compact D/OK/SYNC
burst is <=512 bytes when the USB queue is otherwise empty; output is best-effort
and nonblocking. Logging starts after exchange scheduling finishes near 730 ms,
before 980 ms broadcast preparation. Outside-exchange RX failures/timeouts are
identified/re-armed; active TWR faults still abort.

Independent analog-delay tests reproduce 33300 DTU with zero registers and
349 DTU with matching compensation for the same synthetic 1.637 m path.
At ideal 1 m and 5 m paths expect about 213 and 1066 DTU; with zero registers
and this modeled aggregate, about 33164 and 34017 DTU. These are model predictions,
not new physical measurements. Board variation, signal power/range bias, NLOS,
temperature and the TX/RX split still require physical characterization. TWR
validity is protocol consistency; distance accuracy and corrected synchronization
uncertainty remain distinct, with physical bounds UNKNOWN. The pair's aggregate
can be checked using known separations; per-device and per-direction calibration
requires a calibrated reference or APS014 multi-device procedure.

## Received-power correction

`mulPowerCapture()` reuses the pinned Bitcraze `dwGetReceivePower()` and mutating
`dwCorrectTimestamp()` from `vendor/libdw1000/src/libdw1000.c`. No tables are
copied or fitted. It preserves raw RX_STAMP, applies correction to a copy, masks
40-bit wrap and returns signed `delta = corrected - raw` in local DTU. Both raw
and corrected timestamps already include configured hardware RX antenna delay;
there is no second antenna correction. The driver converts interpolated millimetre
bias to truncated signed DTU, then subtracts it from RX_STAMP. For example its
500 MHz / 64 MHz PRF table near -80 dBm gives about +28 mm bias and -5 DTU RX
correction. This is empirical compensation, not a physical accuracy bound.

Supported channels are 1/2/3/5 (500 MHz), PRF 16 or 64 MHz. Tables are
`BIAS_500_16` (table 1) and `BIAS_500_64` (table 2). Current channel 2 / PRF 64
uses table 2; configured bitrate/preamble are unchanged. Valid input is finite
-95..-61 dBm inclusive. The vendor's endpoint-bin holds (-61..-63 and -93..-95)
are retained exactly, with linear interpolation in interior bins; no extrapolation
outside the table. Channels 4/7 (900 MHz) are explicitly unsupported because the
unchanged driver shifts negative signed biases left. Invalid PRF/channel also
falls back. The original modes and vendor implementation remain untouched.

`Q` records are centi-dBm / signed correction DTU / status / table, in P2,P4,P6
order. Status 0 MISSING, 1 APPLIED, 2 INVALID, 3 OUT_OF_RANGE, 4 UNSUPPORTED;
table 0 means none, power -32768 means unavailable. Capture occurs before radio
re-arm while diagnostics are frozen, so the driver's second power read refers to
the same reception. See [APS011 section 3](https://manuals.plus/m/2ca85a466f2a54e526ff6375cbc67b18852e790e001f500e28d63af17bd58d20)
for received-power bias and design/calibration dependence. Multipath, orientation,
NLOS, power-estimate error and board variation remain unresolved. Current table 2
bias magnitudes are only about 0.11 m; they must not be assumed to explain the
entire earlier user-reported 5.51–7.24 m range at approximately 5 m. The newer
4.962 m mean is recorded in PLAN.md; the excerpts do not identify how much of
the improvement came from power correction versus other RF conditions.

The follower corrects P2/P6; the reference corrects P4. TX P1/P3/P5 are unchanged.
Only the receiving owner captures power/corrects; later messages echo markers and
metadata with prefix checks, never applying correction again. Each record carries
the exact integer delta, allowing both peers to reconstruct raw RX markers by
subtracting it. The unchanged DS equation is evaluated on reconstructed raw
markers and, only if all three statuses are APPLIED, on corrected markers.
Unavailable correction or invalid corrected arithmetic (e.g. negative quantized
near-zero ToF) preserves valid RAW ranging. A partial corrected set is never
reported as fully corrected. RESULT confirms the selected reference-unit ToF.
Logs expose `raw_tof`, `corrected_tof` (or UNAVAILABLE), `raw_mm`, selected
`distance_m` and `power=APPLIED/UNAVAILABLE`. The existing delay producer uses the
selected estimate, with unchanged epoch/age rules and UNKNOWN physical bounds.
Broadcast clock estimation and TDMA continue using raw observations; its rate
may include propagation/RX-bias changes and is not independently calibrated.

## Passive RLR observations and association

Two fixed `mulObservation_t` slots reuse existing per-source counters. Each stores
source TX/raw local RX, corrected RX, full source/receiver clock identities, power
metadata, accepted uptime, validity and a receiver-local generation. Full identity
validation precedes storage. Generations advance on first acquisition or a changed
stale sequence; duplicates/old packets cannot refresh timestamps, metadata or age.
Freshness is <3 seconds. Type-2 follower metadata carries its acquisition-local epoch, reflected in
`remoteEpochKnown`; reference/legacy observations still have no transmitted epoch.
These tokens are not persistent boot UUIDs, and local generations do not invent
remote restart certainty.
Same-sequence restarts and delayed packets after staleness remain ambiguous.

A single `mulAssociation_t` candidate is armed only by a consecutive reference
sequence with 900..1100 ms progress in its source TX clock, RLR raw RX clock and
coarse uptime. A follower must independently show the same consecutive cadence,
arrive after that reference within the configured hardware window and coarse
candidate age (default 450..550 and 700 ms), with compatible local generations
and identical receiver clock identities.
Sequence numbers are independent: equality is never an association rule. First
observations warm up; gaps or changed generations require fresh cadence. Accepted
followers consume candidates once, even when rejected; newer references replace
older candidates. A bounded timeout expires missing followers without transmitting.
All hardware differences are signed modular 40-bit; coarse age rejects whole-wrap
aliases. The window follows this prototype's established TDMA order, not a unique
wire-level superframe identity or a general association algorithm.

`mulPair_t` returns both complete observations, raw local arrival difference and
corrected difference only if BOTH RX corrections are APPLIED. Original TX markers
remain in their own clock domains and are never subtracted from each other.
Stage 3E OBS consolidates association diagnostics into sequences, validity, local
time, selected TDoA and configured slot; raw/corrected arrival differences remain
in the pair object for firmware consumers. Reference/unpaired RX diagnostics keep
raw/corrected timestamps and Q metadata.

## Transported simultaneous clock relationship

The follower sends `F(t)=f0+(1+skewPpb/1e9)*diff40(t,t0)`, reference -> follower.
`t0` is the just-received reference TX; `f0` is
`mulClockMapRefToFollowerSimultaneous(clock,t0,delay)` at predicted follower TX
uptime. This subtracts the scaled one-way TWR delay exactly once from the arrival
origin. The inverse API checks the actual scheduled follower TX at that same
uptime before publication. Thus the origin is simultaneous, not arrival-aligned;
TX hardware antenna conventions and raw broadcast RX estimation are unchanged.
Skew can contain path changes and physical error remains UNKNOWN.

The default strict type-2/version-1 broadcast preserves header/source/sequence/TX
offsets 0..33 and adds:

| Bytes | Meaning |
|---|---|
| 34 | Version 1 |
| 35 | Flags: bit 0 AVAILABLE, bit 1 physical uncertainty UNKNOWN (required), bit 2 TWR power corrected; other bits rejected |
| 36..43 | uint64 follower acquisition-local epoch |
| 44..47 | uint32 current reference observation sequence |
| 48..51 | uint32 TWR reference/exchange sequence |
| 52..56 | uint40 reference simultaneous origin t0 |
| 57..61 | uint40 follower simultaneous origin f0 |
| 62..65 | signed int32 skew ppb |
| 66..67 | uint16 origin age at TX, ms |
| 68..69 | uint16 TWR delay age at TX, ms |
| 70..71 | uint16 delay lifetime, exactly 1000 ms |
| 72..73 | uint16 remaining conservative lifetime at TX, ms |

All integers are little endian, explicit fields, no C padding. Ordered full clock
identities are fixed unambiguously by version 1 and the full follower source
85:00:00:00:00:00:cf:bc; reference is 84:00:00:00:00:00:cf:bc. Only the follower
may send type 2. Length/type/version/flags/identities/skew/age/span checks are strict;
legacy type-1/34 means unavailable metadata. Unavailable version 1 retains only epoch
and reference sequence, with other model fields zero. All nodes must update their
parser to receive type-2 broadcasts; old parsers reject them rather than silently
reinterpreting. TWR stays versioned 84 bytes; no new transmission is scheduled.

Publication uses the current model BEFORE arming, with outward-rounded hardware
lead time plus a one-ms guard. Both existing corrected-API contracts must hold at
TX; remaining lifetime covers the maximum application span to origin and follower
TX, and affine horizon, with an extra ms guard. It is never extended to the
five-second TWR interval. Typically only the follower slot after a TWR is available
(default delay age about 850 ms, remaining <150 ms); at 50/10 ms the age is
about 400/360 ms and remaining <600/640 ms. Subsequent slots still report
unavailable under this strict compatibility policy: shortening the slot alone
never extends the 1000 ms TWR delay lifetime. Stage 3E adds explicit bounded reuse
as described below.

RLR freezes hardware RX age before re-arm (max 15 ms accepted for a relation),
then binds metadata to the current follower frame and associated reference sequence
AND exact TX marker. It never substitutes cached available metadata. Local age,
40-bit query horizon, original measurement-age policy, full receiver/source clock
identities and direction all apply. An accepted follower epoch change advances its
local observation generation, drops that pair and resets experimental rate state.
Fresh cadence is required. Epochs distinguish acquisitions within the follower boot;
reference boot epochs are absent, and undetected restart/replay ambiguity remains
subject to existing cadence/liveness checks. This is not authenticated time transfer.

## Continuous propagation correction (Stage 3E)

`MUL_PROP_MAX_AGE_MS` defaults to 1000 for strict v1 compatibility. Configure
6000 on all three nodes for continuous operation. Values 1000..6000 are accepted;
6000 bridges the five-second TWR cadence with about one second of margin while
remaining below the DW1000 signed half-wrap (~8.6 s). This is a bounded sample
hold, not prediction, a new clock algorithm or a calibrated accuracy guarantee.
The affine model still reanchors at each 1 Hz reference, with its original 2 s
origin/query horizon. Only the TWR propagation value is reused.

The committed estimate (`tof`, epoch, estimateId, measuredMs, power flag) is
separate from the exchange candidate. RESULT must verify all existing timestamps,
power metadata and DS-TWR arithmetic before commitment. measuredMs is backdated
from RESULT to the original raw POLL RX capture, rounded outward; callbacks,
REPORT, failed attempts and later broadcasts cannot re-date it. Each packet
predicts its original TWR age at scheduled antenna TX, rounded outward with a
guard; RLR adds frozen hardware RX age and subsequent local processing age.

Timeout, missing response, late setup and broadcast preemption retain a committed
estimate only within its original policy. Malformed active exchange/result,
invalid ranging arithmetic, TX/DX verification failure, antenna configuration
fault or incompatible clock epoch invalidates it. Unrelated packets outside an
active exchange do not refresh it. Clock loss/reset, role initialization and
reacquisition cannot carry a previous estimate into a new epoch. There is no
holdover transmission. A failed refresh cannot overwrite the committed age/value.

States: NONE before any estimate; FRESH in its measurement reference cycle or
first following reference; REUSED in later valid cycles; EXPIRED when original
age exceeds policy; INVALIDATED after a trust/context failure. FRESH describes
provenance, not calibrated accuracy. A successful new RESULT replaces the old
measurement and starts a new FRESH cycle. Strict v1 cannot transport distinct
unavailable states or the numeric delay; these remain UNKNOWN at its receiver.

Continuous transport uses explicit **version 2**, still type 2 and 74 bytes.
There is no spare v1 payload space for the propagation value and state. All
unchanged fields retain their v1 offsets; only these v2 fields differ:

| Bytes | Version 2 meaning |
|---|---|
| 34 | Version 2, explicitly incompatible with v1-only parsers |
| 35 | Bits 0..2 unchanged; bits 3..5 state (NONE=0, FRESH=1, REUSED=2, EXPIRED=3, INVALIDATED=4); bits 6..7 rejected |
| 70..71 | uint16 actual one-way propagation estimate, reference DTU (0..63898) |
| 72..73 | uint16 original maximum estimate age, ms (1000..local configured policy) |

Epoch/current reference sequence/TWR acquisition sequence and both original-age
fields retain their meanings. v2 derives conservative remaining lifetime from
original delay age, query span and affine-origin age, minus one ms; a broadcast
cannot refresh the TWR lifetime. Available packets require FRESH/REUSED, matching
sequence provenance, valid ages and the current associated reference origin.
Unavailable v2 clears affine origins/skew/origin age but may retain the last delay,
age and acquisition sequence when expired; NONE/INVALIDATED has empty delay
metadata. Version/length/flags/age/state validation is strict. v1 remains decoded
with its fixed 1000 ms contract even in a continuous build; a strict build rejects
v2 policy beyond 1000. Old v1-only firmware rejects v2 rather than reinterpreting
it. Use identical builds on all nodes. Reference 34-byte and TWR 84-byte packets,
DS-TWR equations, PHY, antenna/power corrections and EEPROM are unchanged.

`MUL_RELATIVE_SPEED_MM_S` is -1 (UNKNOWN) by default; 0 explicitly declares
constant ALR separation; a positive integer is an externally justified upper
bound on relative separation speed. For age dt in ms, the geometric-only bound
is `ceil(v_max_mm_s * dt / 1000)` mm, converted outward to nominal reference DTU
with `ceil(mm * 63897600000 / 299792458000)`. No floating state or fitted offset
is used. A missing motion bound remains UNKNOWN even at age zero. This geometric
term is distinct from unknown DS-TWR measurement, clock-skew, RLR-rate, power,
antenna/path and other synchronization errors. The nominal DTU conversion is
not an absolute oscillator calibration. Total physical uncertainty remains UNKNOWN
with either zero or nonzero motion bound; no statistical SD is invented.

## RLR rate domains and signed downlink TDoA

First compute `tx_ref = F_inverse(follower TX)` and
`Delta_TX_ref = diff40(tx_ref, reference TX)`. Raw and optional corrected
`Delta_RX_rlr` remain in the RLR clock. They cannot be subtracted numerically from
reference intervals until a relative RATE is supplied. The default build explicitly
reports `clock_relation=AVAILABLE tdoa=UNAVAILABLE`, reason RLR_RATE_UNAVAILABLE.

`MUL_EXPERIMENTAL_STATIONARY_RLR_RATE=1` is an explicit bench-only opt-in.
It reuses `mulClockObserve()` and its five-interval median/convergence checks on
reference TX versus raw RLR RX cadence. Only the skew is used; no RLR arrival
intercept is used to erase propagation geometry. It assumes constant propagation
and RX bias over estimation and measurement, requiring a stationary declaration.
There is no independent stationarity detector: constant radial motion is
indistinguishable from skew. This assumption is not a mobile-robot solution.
Observable cadence discontinuities, reference rebasing, follower epoch changes,
incomplete convergence, or a stale rate invalidate availability.

The rate must match the exact current reference sequence/TX/raw RX and be no
older than the configured candidate age (700/250/210 ms). Convert each bounded interval with
`Delta_RX_ref = Delta_RX_rlr - round(Delta_RX_rlr*skewPpb/(1e9+skewPpb))`, then
`TDoA_ref = Delta_RX_ref - Delta_TX_ref`. Positive means follower->RLR propagation
minus reference->RLR propagation; sign is retained. Convert signed DTU to signed
mm with the existing 299792458/63897600000 convention. The existing <=1 us
prototype inter-ALR envelope also rejects grossly inconsistent selected residuals;
it is not a physical error bound. Raw and corrected RX/TDoA are separate results;
missing power preserves raw-only measurement with power=0, not false full correction.
Total physical uncertainty always remains UNKNOWN, including skew, origin, TWR,
antenna, power, motion and path errors. Numerical rounding is not synchronization
accuracy. A plausible result is conditional experimental availability, not calibration.

`mulLocalization_t` is a caller-owned 88-byte value generated once per valid
associated pair, including unavailable cases. It owns both full ALR identities,
independent sequences, local generations, follower acquisition epoch, exact raw
RLR follower RX marker, coarse follower callback uptime, slot, selected signed
TDoA DTU/mm, propagation DTU/age/state, rate status/value, relation age/validity,
power flags, association validity and component UNKNOWN flags. There are no
pointers into radio buffers or new persistent packet/observation queues. Future
localization code can consume it directly before printing; no solver is added.
Unavailable numerical values use explicit sentinels and serialize as NA, never a
plausible zero. Rate availability and computability do not imply physical accuracy.

One `MUL OBS` line replaces redundant paired-follower RX/PAIR/TDOA output. RX
tracking logs remain for reference and unpaired follower observations; ALR
TX/CLOCK/MAP/SYNC/TWR logs remain. OBS has this stable field order:

```text
MUL OBS: ref=84/11 fol=85/905 t_ms=11012 rx=<40-bit hex> gen=1/1 epoch=<64-bit hex> status=AVAILABLE dtu=0 mm=0 slot_ms=10 delay_dtu=426 delay_age_ms=362 delay_state=FRESH rate=STATIC/-10000 clock_age_ms=12 clock=1 power=0 assoc=1 motion_mm=0 motion_dtu=0 quality=2F uncertainty=UNKNOWN
```

`t_ms` is uint32 RLR callback uptime, `rx` its exact 40-bit raw follower RX marker;
these are local time domains, not reference/MCS absolute time. Sequences/generations
and epoch identify acquisition segments; epochs are not persistent boot UUIDs.
Status is AVAILABLE, NO_RELATION, NO_RATE, STALE_RATE or INCONSISTENT. dtu/mm are
NA unless AVAILABLE. delay/clock ages are ms; unknown metadata prints UNKNOWN.
rate is STATIC/<signed ppb> under explicit experimental stationary policy, NONE
or STALE. clock/assoc are Boolean validity. power bits 0/1/2 indicate applied
reference RX/follower RX/TWR corrections; absence is not calibrated zero error.
quality bits 0..5 denote UNKNOWN TWR/clock/rate/power/motion/other components;
2F still has unknown total uncertainty when motion is configured. Startup labels
slot, propagation policy, motion bound and STATIC_EXPERIMENTAL versus UNAVAILABLE
rate policy. The worst-case serial fixture is <512 bytes without a formatting
buffer; emission occurs after radio rearm and only once per associated pair.

For later MCS matching, capture each node's USB stream with host monotonic receive
timestamps and preserve raw OBS lines, source identity and reset segments. Unwrap
local rx/t_ms within each segment; use sequences/epochs/generations to prevent
cross-restart matching. Establish a separate verified time alignment to MCS
(e.g. common event) before assigning ground-truth poses. USB latency/local uptime
alone do not provide calibrated cross-system synchronization. No MCS integration,
bias fitting or online calibration is implemented in this stage.

`mul_role_state` overlays 128 bytes of ALR TWR state with mode-6 relation/rate
scratch because one persisted mode runs until reboot. No heap is added. The
1600-byte UWB task and 2048-byte main/ISR stack reservations remain; static RAM
has approximately 20 bytes outside existing reservations. Compiler stack frames
are reviewed separately; on-device high-water and RF accuracy remain unmeasured.
Further state growth requires an explicit memory review. Stage 3E keeps relation48,
scratch128, data1072 and bss/reservations15292 (20 spare). Record88 is transient.
Compiler frames: RLR event520, record builder128/print88, TX setup248, publisher320.
Reviewed nested paths stay below the reservation before library/context overhead;
this does not establish on-device stack high-water or hard real-time safety.

## Stage 3E build and hardware procedure

Default safe compatibility: `make clean && make` (v1, 1000 ms, no justified RLR
rate). Preferred **stationary experimental** continuous build:

```sh
make clean && make EXTRA_CFLAGS="-DMUL_EXPERIMENTAL_STATIONARY_RLR_RATE=1 -DMUL_FOLLOWER_SLOT_MS=10 -DMUL_PROP_MAX_AGE_MS=6000 -DMUL_RELATIVE_SPEED_MM_S=0"
```

Use slot 50 or 500 to retain other schedules. Omit the speed flag for UNKNOWN
motion guarantee. Mobile-oriented metadata experiment (no mobile rate solution):

```sh
make clean && make EXTRA_CFLAGS="-DMUL_FOLLOWER_SLOT_MS=10 -DMUL_PROP_MAX_AGE_MS=6000 -DMUL_RELATIVE_SPEED_MM_S=500"
```

Here 500 mm/s is an explicit example assumption to justify for the actual robots,
not an established WaveRover limit. Without a justified RLR/reference rate this
build emits NO_RATE rather than pretending mobile TDoA is calibrated. A relative
ALR speed bound does not make the stationary cadence-rate estimator motion-safe.

**User-only flashing:** build first, put each node in DFU mode, then run
`sudo make dfu` to flash that existing image; the dfu target does not rebuild.
Use the identical image on 0x84/0x85 mode 5 and 0x86 mode 6. Codex did not flash.
After warmup and a successful TWR, expect one OBS per pair near 1 Hz, FRESH then
four REUSED cycles with growing original delay age, reset only by a successful
new RESULT near five seconds. Observe >40 s for wrap. Missed refresh can preserve
availability only up to six-second age, then EXPIRED/NO_RELATION. Clock reset/loss
requires a new TWR and rate warmup. In the strict build availability remains
intermittent, and default rate stays NONE. Verify delta=0, no gaps/starvation,
expected lengths and no unexpected numerical values under unavailable status.
Keep existing geometry for functional acceptance; detailed bias calibration is
deferred until efficient MCS acquisition. Stage 4 online calibration and Stage 5
localization remain future work.

Reproduce host/sanitizer matrices with the existing native GCC/Ruby/Rake setup:

```sh
python3 tools/test/mul_slots.py --sanitize --prop-age 6000 --speed 500
python3 tools/test/mul_slots.py --sanitize --prop-age 6000 --speed 0 --slot 10
python3 tools/test/mul_slots.py --sanitize --slot 500 --default-rate
python3 tools/test/mul_slots.py --sanitize --prop-age 6000 --slot 10 --default-rate
python3 tools/test/mul_slots.py
```

The three-node synthetic test runs real handlers and wire transport through two
successful TWR updates and a missed refresh, independent clock rates/sequences,
startup, fresh/reused/expired states and counter wraps. A bounded separation-change
case checks growing geometric aging and UNKNOWN remaining terms. Separate real-
driver SPI tests preserve scheduling/register equality and power-correction tests
preserve existing calculation. Host results do not establish RF accuracy.

## Planned measurement processing

- Form downlink TDoA observations for relative localization after clock relationships
  and transmission associations have been modeled explicitly.
- Use occasional TWR measurements for online measurement-error calibration.
- Model uncertainty in both ALR and RLR geometry, including motion and ALR poses.
- Develop a probabilistic UWB error model distinguishing relevant measurement conditions.
- Initially use known-map information for LOS probabilities.
- Later investigate SLAM-derived LOS information and its uncertainty.

These are architectural directions; no position solver, independent motion-aware
clock estimator, or online measurement-error calibration is implemented in MUL.

## Reuse and evidence

Reuse tested Bitcraze DW1000 operations and protocol mechanisms where appropriate.
Before new timing, clock-correction, or ranging work, inspect `uwb_tdoa_anchor2.c`,
`uwb_tdoa_anchor3.c`, `uwb_twr_anchor.c`, and `uwb_twr_tag.c` in `src/`.
Do not assume stationary-anchor algorithms transfer unchanged to mobile ALRs.
The existing Unity/CMock infrastructure covers packet utilities, MUL event behavior,
and real-driver operations against a simulated SPI device; these are software checks.
Physical acceptance belongs to the milestone and task checkpoints in PLAN.md.
Maintain decisions here and status there; Git history provides the change history.
