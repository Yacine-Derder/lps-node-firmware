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
- The present configuration cannot interoperate directly with DWM3000 over UWB.
  DW3000 hardware supports channels 5/9, with limited DW1000 compatibility on
  channel 5; the families are not register/API interchangeable.
  See [Qorvo's DW3110 specification](https://www.qorvo.com/products/p/DW3110)
  and [migration guidance](https://forum.qorvo.com/t/dwm1000-to-dwm3000-migration/10120).
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
| `inc/mul_clock.h`, `src/mul_clock.c` | Clock rate, arrival mapping, and dormant external-delay correction |
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

The test broadcast is 34 bytes excluding the two-byte hardware-generated FCS.
Multibyte fields are explicitly encoded little endian; C struct padding is not on wire.

| Byte offsets | Width | Current meaning |
|---|---|---|
| 0–1 | 2 | FCF `0xdc41`: data frame, compressed PAN, extended addresses, no ACK |
| 2 | 1 | MAC sequence: low byte of MUL sequence |
| 3–4 | 2 | PAN `0xbccf` |
| 5–12 | 8 | Broadcast destination, all `0xff` |
| 13–20 | 8 | Configured source address |
| 21–23 | 3 | Protocol identifier ASCII `MUL` |
| 24 | 1 | Test-broadcast type `1` |
| 25–28 | 4 | Unsigned MUL sequence number |
| 29–33 | 5 | Expected antenna-referenced TX timestamp, radio time units |

The eight-byte source comes from `uwbConfig_t.address`; byte 0 is the EEPROM node ID.
The existing address base has bytes 6/7 equal to `0xcf`/`0xbc`, with middle bytes zero.
Diagnostics show the node ID; future source tracking should retain full identity.
The sequence restarts at zero on role initialization and wraps as an unsigned 32-bit value.
Decoding checks exact length, FCF, PAN, destination, identifier, type, and MAC-sequence
consistency before extracting fields. Stage 1's shorter frame is not accepted now.
There are no role, reference, slot, clock-model, or localization fields yet.

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
Propagation-corrected clock conversion and decentralized operation are future work.

## Reference-following slots and bounded source observations

The follower starts in RX and accepts only the full reference identity. A first
valid packet acquires; three seconds without forward sequence progress loses acquisition.
RTOS ticks provide only this wrap-safe coarse watchdog.
A stale stream may restart from a different sequence; replaying the last sequence
alone cannot reacquire. Duplicate, old, unknown, or malformed packets never arm TX.

Each fresh reference sequence permits one attempt in the follower's local clock:
`antenna target = local reference RX + 500 ms`; `raw DX_TIME = align_up(target -
live TX_ANTD, 512 ticks) modulo 2^40`. The encoded time is verified DX_TIME plus
TX_ANTD, so intended antenna-referenced separation is 500 ms to within 511 ticks.
The reference's embedded TX time is stored/logged, never used as a local TX target.
Follower packet sequences are independent of reference sequences and survive reacquisition.

RX turns directly into delayed TX through `startDelayedTx()`, shared with the reference.
No RX is enabled while the delayed TX waits in idle. Completion captures TX_STAMP,
re-arms RX, and logs role, scheduled, packet, actual, and signed packet delta.
RX errors/unexpected radio events cancel a pending slot. An absolute local-hardware
watchdog aborts if completion is missing 100 ms after the target; early wakeups
leave TX armed. Late setup (under 5 ms lead), register mismatch, or delayed-start
errors skip the attempt and return to RX. No slot is moved forward or retried.
Declared reference loss aborts pending TX; no missing superframe is extrapolated.

This is coarse TDMA, not clock synchronization: 500 ms uses the follower's nominal
clock rate, includes propagation/clock-rate effects, and leaves large slot guards.
The follower cannot receive during its roughly 500 ms delayed-TX wait.
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
There is no offset estimation, TWR, or TDoA observation formation.
See [the focused reuse audit](mul_stage3_reuse_audit.md) for implementation decisions.

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

The follower updates/logs CLOCK after arming TX or rearming RX, at most once per
fresh reference plus loss transitions, in the UWB task. Only skew is printed,
with sign and three ppm decimals. TX/RX timestamps, TDMA targets, and RLR behavior
are unchanged; no estimator or mapping output is used for scheduling.

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
in follower units cannot be passed unchanged. No distance or live TWR is injected.

Initial application policy permits nonnegative delay <=63898 reference DTU
(approximately 1 us/300 m) and a caller-selected age limit of 1..2000 ms,
recommended 1000 ms. These are prototype applicability limits, not hardware range
or motion guarantees. Validity checks wrap-safe age, full ordered identities,
units, and epoch. A sequence guard of at most three forward reference advances
at the established 1 Hz cadence also rejects uptime full-wrap aliases.
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
measurements. Token exhaustion fails closed. There is no new wire epoch, so an
undetected remote restart remains subject to the existing sequence/loss policy.

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

No production call supplies a delay, and CLOCK/MAP logs and radio paths are
unchanged: normal correction is UNAVAILABLE and `sync=UNCALIBRATED` stays truthful.
Future TWR must establish one-way interpretation, clock units, path applicability
and uncertainty before feeding this contract. Acquisition does not implement
Stage 4's later online ranging-error calibration or independently identify exact
offset from broadcasts alone.

## Planned measurement processing

- Form downlink TDoA observations for relative localization after clock relationships
  and transmission associations have been modeled explicitly.
- Use occasional TWR measurements for online measurement-error calibration.
- Model uncertainty in both ALR and RLR geometry, including motion and ALR poses.
- Develop a probabilistic UWB error model distinguishing relevant measurement conditions.
- Initially use known-map information for LOS probabilities.
- Later investigate SLAM-derived LOS information and its uncertainty.

These are architectural directions; no localization, clock-offset estimator, or calibration
algorithm is currently implemented in MUL.

## Reuse and evidence

Reuse tested Bitcraze DW1000 operations and protocol mechanisms where appropriate.
Before new timing, clock-correction, or ranging work, inspect `uwb_tdoa_anchor2.c`,
`uwb_tdoa_anchor3.c`, `uwb_twr_anchor.c`, and `uwb_twr_tag.c` in `src/`.
Do not assume stationary-anchor algorithms transfer unchanged to mobile ALRs.
The existing Unity/CMock infrastructure covers packet utilities, MUL event behavior,
and real-driver operations against a simulated SPI device; these are software checks.
Physical acceptance belongs to the milestone and task checkpoints in PLAN.md.
Maintain decisions here and status there; Git history provides the change history.
