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
- Stage 3A.1's reference/follower arrangement is defined in PLAN.md.
  Its receive-only follower must not autonomously transmit.

## Current firmware boundaries

| Component | Responsibility |
|---|---|
| `src/main.c` | USB configuration menus and mode/address persistence requests |
| `inc/uwb.h`, `src/uwb.c` | Algorithm interface, registry, PHY setup, IRQ/task dispatch |
| `src/cfg.c`, `src/eeprom.c` | Existing persistent configuration |
| `inc/mul_common.h`, `src/mul_common.c` | Wire encoding/validation and modular timestamp utilities |
| `inc/mul_alr.h`, `src/mul_alr.c` | DW1000 delayed broadcasts and TX diagnostics |
| `inc/mul_rlr.h`, `src/mul_rlr.c` | Passive RX, raw hardware timestamps, receiver re-arming |
| `vendor/libdw1000`, `src/dwOps.c` | Existing radio driver and board SPI operations |

Both MUL roles implement `uwbAlgorithm_t.init` and `onEvent` callbacks.
The UWB task waits on its IRQ semaphore with the timeout returned by the role.
USB configuration remains in the existing main task; no MUL timer subsystem exists.

Mode IDs remain: 0 TWR Anchor, 1 TWR Tag, 2 Sniffer, 3 TDoA Anchor V2,
4 TDoA Anchor V3, 5 MUL ALR, and 6 MUL RLR.
The serial `m` menu lists/selects modes; stored changes take effect after reset.
Node addresses and modes use the existing EEPROM layout with no MUL-specific fields.
Current mode 5 broadcasts for every configured address; no follower role selector exists.

## Current packet protocol

The test broadcast is 34 bytes excluding the two-byte hardware-generated FCS.
Multibyte fields are explicitly encoded little endian; C struct padding is not on wire.
Source definitions: `inc/mul_common.h` and `src/mul_common.c`.

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

Current ALR operation uses hardware-delayed TX at approximately 1 Hz.
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

Delayed TX and antenna-referenced timestamps are hardware validated according to
the user's current project status. Software tests alone do not establish this.
The earlier reported constant packet/TX discrepancy has no established root cause
in the repository; successful validation must not be described as a proven diagnosis.
Configured antenna-delay compensation is not proof of physical antenna calibration.

A transmitted ALR timestamp cannot be directly subtracted from another robot's
RX timestamp to obtain propagation time or distance. Relative offset, skew, and
propagation delay are distinct effects; one-way observations alone cannot identify
exact clock offset independently of propagation delay.
Initial TDMA synchronization will use a reference ALR and follower ALRs.
Clock correction, uncertainty, loss/reacquisition robustness, and decentralized
operation are future work, not capabilities implied by the current timestamp pair.

## Planned measurement processing

- Form downlink TDoA observations for relative localization after clock relationships
  and transmission associations have been modeled explicitly.
- Use occasional TWR measurements for online measurement-error calibration.
- Model uncertainty in both ALR and RLR geometry, including motion and ALR poses.
- Develop a probabilistic UWB error model distinguishing relevant measurement conditions.
- Initially use known-map information for LOS probabilities.
- Later investigate SLAM-derived LOS information and its uncertainty.

These are architectural directions; no localization, clock estimator, or calibration
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
