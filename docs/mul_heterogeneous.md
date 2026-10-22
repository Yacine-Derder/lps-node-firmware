# Heterogeneous custom MUL integration

## Reference architecture (checkpoint A)

The reference is the current custom MUL implementation, confirmed by the user:
`src/mul_alr.c`, `mul_twr.c`, `mul_relation.c`, `mul_rlr.c`, `mul_observation.c`,
`mul_downlink.c`, `mul_localization.c` and `mul_clock.c`. Git history traces it from
f106c8b (modes), eed7062 (hardware TDMA), 2ba6626 (inter-ALR TWR), through
0257ba0 (localization observations). Stock Bitcraze TDoA2/3 are not this protocol.

ALR0x84 is the reference, broadcasting every1000ms at hardware-delayed TX times.
ALR0x85 follows each fresh reference at a build-selected500/50/10ms local RX-based
slot. Loss after3000ms stops follower free-running; there is no catch-up burst.
Broadcast type1 is34 bytes; follower relation type2 is74 bytes, v1/v2. Both carry
native40-bit antenna-referenced TX. The relation transports a propagation-corrected
reference→follower simultaneous clock mapping, ages and a follower acquisition-
local epoch (not a persistent boot UUID). The RLR pairs cadence-checked receptions,
binds the current frame's relation to the exact reference sequence/TX, and maps
follower TX into reference units before subtracting emission separation from
receiver-clock-corrected arrival separation. Receiver clock rate is unknown by
default; the original explicit stationary experimental rate opt-in is conditional
on stable propagation. Inter-ALR TWR alone does not identify receiver clock rate.

Every fifth reference sequence, inter-ALR TWR occupies650..800ms (preparation630ms),
using20ms delayed turnarounds and bounded deadlines. Directed types0x10..0x14
POLL/RESPONSE/FINAL/REPORT/RESULT are84 bytes with six timestamps and receiver-owned
DW1000 power-correction metadata. The reference-unit DS-TWR estimate supplies
propagation correction; it is not a range from the RLR to an anchor. RLR mode6
is entirely passive; multiple RLRs may listen without adding airtime.

Two anchor positions plus one range difference define a hyperbola, not a unique
unconstrained2D position. The localization solver must reject that geometry unless
additional independent measurements/constraints are supplied. Generic solver
interfaces will accept arbitrary anchor IDs; the legacy on-air roster remains
exactly that supported by the reference implementation. Extending the scheduler
or adding RLR-initiated TWR is outside this compatibility milestone.

## Implementation plan

B: extract existing radio-independent codecs/association/math as canonical shared
code without changing DW1000 behavior; add opt-in channel5 custom-mode PHY;
implement a passive DWM3000 universal mode using the existing radio owner.
C: preserve local epochs/config/calibration/safety; transport versioned raw legacy
frames plus exact RX and timing/association provenance through the bounded bridge.
D: replay shared receiver math offline; keep a radio-independent fixed-anchor2D
solver separate, test identifiable synthetic geometries and reject underdetermined
or unsynchronized data. Time-box UART fault investigation and retain fail-safe loss.
E: regressions/builds, compatibility matrix and exact user hardware instructions.

The only initially identified ALR compatibility change is PHY: custom modes inherit
channel2 by default, unsupported by DWM3000 (channels5/9). An explicit channel5
mixed-profile build will preserve frame bytes, slot timing, EEPROM and default
stock/custom behavior. It must be deployed on both participating ALRs. Antenna
delays remain per-board; DW1000 empirical power tables must not be applied to DWM3000.

## Checkpoint status

A complete: source/history mapped; reference confirmed; no hardware operated.
B–E pending. Hardware interoperability/localization accuracy remain unvalidated.
Progress and remaining work belong to PLAN.md; this guide will describe the final
adapter, executable commands, validation and compatibility matrix as they land.
