# MUL Stage 3 reuse audit

## Scope and boundary

Use existing node firmware mechanisms for reference acquisition, coarse follower
slots, and bounded multi-source reception. Keep the 34-byte MUL wire format unchanged.
This audit records reuse decisions, not milestone status; see [PLAN.md](../PLAN.md).
Coarse TDMA and diagnostic relative clock rate are implemented; no TWR or localization.

## TDoA2: acquisition and RX/TX state transitions

Source: `src/uwb_tdoa_anchor2.c`.
- `tdoa2UwbEvent()` separates reference acquisition from the synchronized state.
- `handleFailedRx()` returns to acquisition when the reference slot is missed.
- `setupRx()` uses `dwNewReceive()`, `dwSetDefaults()`, and `dwStartReceive()`.
- `setupTx()` uses `dwSetTxRxTime()` and completion-driven radio progression.
- `adjustTxRxTime()` illustrates the DW1000's low-nine-bit scheduling alignment.

Reuse the acquisition/loss distinction and event-driven receive re-arming.
Reuse its direct RX-to-delayed-TX transition, not its full frame/slot schedule.
Its low-byte anchor identity and truncated timestamp arrays do not replace MUL's
full-address matching or five-byte timestamps. There is no synchronized clock yet.

## TDoA3: bounded source state and liveness

Source: `src/uwb_tdoa_anchor3.c`.
- `getContext()`, `createAnchorContext()`, and `purgeData()` organize bounded
  per-anchor state and age stale information.
- `handleRxPacket()` captures raw radio RX time before restarting radio operation.
- `setupRx()` provides the same established RX re-arm sequence.
- `startNextEvent()` uses wrap-safe RTOS timing for coarse event/liveness decisions.
- `calculateClockCorrection()` forms local/remote interval ratios; `updateClockCorrection()`
  uses a plausibility bound (±20 ppm), 0.03 ppm noise gate, 0.1 low-pass weight,
  and a bounded confidence bucket for reseeding after repeated rejections.

Use two explicit supported identities instead of dynamic discovery/context eviction.
Keep source TX and local RX timestamps in one observation without subtracting them.
RTOS ticks measure receiver liveness only; they are not precise UWB scheduling time.
Do not inherit ranging formulas, fixed geometry, or a shared-clock assumption.
MUL reuses the interval-ratio principle and bounded validity/recovery approach.
TDoA3 uses double arithmetic and 32-bit timestamps, explicitly requiring short
intervals to avoid repeated wraps. Its slope does not require known distance,
but varying propagation affects it; associated ranging exchanges are not reused.
MUL instead uses existing 40-bit arithmetic, coarse gap/sequence bounds, ppb
integers, a five-sample median/stability check, and a wider ±200 ppm initial gate.
TDoA2's frame timestamp arrays support slot/range exchanges, not a reusable
standalone robust skew estimator; neither truncated history is copied into MUL.

## TWR: later exchange reuse

Sources: `src/uwb_twr_anchor.c` and `src/uwb_twr_tag.c`.
- Their `txcallback()`/`rxcallback()` paths demonstrate hardware timestamp capture.
- `initiateRanging()` and `dwWaitForResponse(true)` illustrate alternating TX/RX.
- `twrAnchorOnEvent()`/`twrTagOnEvent()` illustrate completion/error dispatch.

Retain these as references for occasional future calibration exchanges.
Do not use request/reply or automatic response behavior in a receive-only role.
MUL retains its explicit serialization and bounds checks instead of packet overlays.
No Crazyflie tag-side sources were needed: the local node paths cover this milestone.

## Existing MUL and driver paths retained

Sources: `src/mul_alr.c`, `src/mul_rlr.c`, `src/uwb.c`, `vendor/libdw1000/src/`.
- Preserve Stage 2 delayed TX, DX_TIME readback, live TX_ANTD compensation, and
  scheduled/packet/actual timestamp diagnostics for the reference identity.
- Reuse the existing passive RX configuration and extract it into `mul_rx.c`:
  disable auto re-enable, enable RX-error interrupts, capture raw RX time, re-arm.
- `dwHandleInterrupt()` clears RX status and soft-resets failed/timed-out RX before
  role callbacks. Do not duplicate this driver recovery or change vendor behavior.
- The EXTI callback signals the semaphore; algorithm callbacks and diagnostics
  execute in the UWB task, not in the hardware ISR. Program RX/TX before serial output.
- Keep PHY setup, EEPROM, USB menus, original modes, and the driver untouched.

## Current MUL decisions

- Central full-address allowlist: `84:00:00:00:00:00:cf:bc` is reference;
  `85:00:00:00:00:00:cf:bc` is follower. Unsupported mode-5 identities stay idle.
- A single valid reference broadcast acquires the follower; three seconds without
  forward sequence progress loses it. Malformed/other-source traffic cannot refresh it.
- Sequence ordering is unsigned modulo 2^32, with advances restricted to < 2^31.
  Repeats of the latest are duplicates; older packets (including older repeats)
  are out-of-order. Forward jumps count skipped positions, not proven physical loss.
- After a stale interval, a different sequence can begin a new baseline, allowing
  reference reboot recovery. Repeating the last sequence alone cannot reacquire.
- Counters persist across reacquisition and wrap as uint32 values. No epoch/authentication
  field exists, so a delayed different packet after loss cannot be distinguished from reboot.
- RLR storage is fixed to two source slots. Only accepted observations update each
  slot's timestamp pair; duplicate/old traffic is quiet and counted for the next fresh log.
- Multi-source tests include packets generated by follower TX, interleaved at the RLR.
- Existing Unity tests include a fake event radio and real-driver/simulated-SPI
  checks. Use both to distinguish algorithm policy from driver/register behavior.

## Follower delayed-TX reuse

- `startDelayedTx()` in `mul_alr.c` shares Stage 2 DX_TIME readback, antenna-delay
  compensation, bounds/late checks, and delayed-start error handling between roles.
- Like TDoA2 `setupTx()`, it first enters TX_MODE through `dwNewTransmit()` before
  `dwSetTxRxTime()`. Keep permanent RX/wait-for-response off while the TX waits.
- Follower completion/error callbacks return to the existing `mulRxRearm()` path;
  early watchdog events must not call it, since that would cancel delayed TX.
- Each accepted reference RX supplies one local 500 ms slot; never use TDoA2's
  reference timestamps as a shared clock or copy its holdover/free-running behavior.
