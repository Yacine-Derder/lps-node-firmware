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

## Implementation and compatibility

Pure original codecs/clock/association/downlink math live in portable/mul_core;
LPS inc/src forwarding files retain the original APIs and hardware scheduler.
The new passive `mul_hybrid` receiver composes those functions and validates
observed inter-ALR TWR prefixes, timing and final arithmetic; it never replies.
DWM3000's existing radio owner selects this adapter in universal hybrid modes.
No generic replacement MAC or independent synchronization protocol was introduced.

| Capability | Implemented/software verified | Physical status |
|---|---|---|
|Original DW1000 modes/EEPROM/TDMA/inter-ALR TWR|Original algorithms retained;195 Unity tests;178 MUL sanitizer tests at each500/50/10ms|Existing earlier results are user-reported; new channel5 combination pending|
|Channel5 custom-mode compatibility|Explicit MUL_HYBRID_CH5 profile; other modes/defaults unchanged; real mixed-PHY helper reused|Must program both ALRs; channel2 cannot reach DWM3000|
|Passive DWM3000 hybrid receiver|34/74/84-byte parsing, full40-bit RX, exact original association/mapping, loss/restarts, no-TX tests|Pending|
|Inter-ALR TWR observation|Original six-stamp/reference-unit equation, staged/prefix/time checks, no RLR-range substitution|Pending|
|Geometric TDoA|Original conditional stationary-rate opt-in; default NO_RATE; DW1000 power correction not applied to DWM3000|Receiver rate/bias/accuracy pending|
|Wi-Fi capture/replay|UWB3 bounded raw frames; v1/v2 compatible; exact shared C receiver replay, domains/generations retained|Historical ADS forwarding worked; new hybrid records pending|
|2D solver|Generic fixed-anchor ranges/differences, numerical/independence/ambiguity checks, synthetic truth recovered|Current two-ALR network insufficient for unconstrained2D; no measured positions claimed|
|UART robustness|HAL ORE traced; Universal IRQ-preemption/finite SPI timeout hardening, fault/reconnect tests|Neither reported UART symptom declared resolved|

The solver does not change the on-air roster. Additional anchors/RLR ranges,
geometry/trajectory constraints, a mobile receiver-rate solution, physical bias
calibration and statistical anchor uncertainty remain separate work. There is no
claim of mobile heterogeneous localization from two anchors or software tests.

## Build and hardware handoff

STM32 docs/hybrid_uwb.md is the complete operating guide and preserves the user's
historical2026-10-22 deleted-capture summary. It contains exact builds, ELF paths,
configuration, collector/replay/solver commands, software validation and an ordered
hardware campaign. Read its rate/geometry limitations before enabling hybrid_static.
The independently maintained central project document is untouched.

A: reference discovery complete (e0c9955). B: shared receiver and passive adapter
implemented (6aa9c2e, STM32 a514ee4). C: telemetry bridge implemented (ESP dce7f3d)
and scoped SPI/UART hardening tested (STM32 dca615e). D: shared-core offline replay
and guarded generic2D solver implemented/tested. E: integration/profile regression
and hardware handoff. These are software checkpoints; no flashing or physical
experiments were performed. PLAN.md records current remaining work.

## Passive reception investigation (2026-10-22)

User hardware output establishes reception/association and successful ALR TWR,
not passive reconstruction or ranging accuracy. `twr_reject` previously merged
missing reference, wrong sequence/window, missing/out-of-order components, changed
prefix, timestamp/arithmetic failures and late captures. Parser failures were in
another aggregate. A successful ALR exchange does not prove the RLR heard it.
The PHY helper disables hardware frame filtering; directed destinations alone
are not excluded by the configured receiver. The RLR already consumes the
follower's transported relation independently of its passive five-frame TWR audit.
Thus zero audited exchanges does not itself explain `clock=0` on associated pairs.

Shared diagnostics now distinguish these conditions and count each decoded TWR
type and transported available relations. Validation, packet bytes, timing and
ALR scheduling are unchanged. Deterministic tests pass complete exchanges and
identify lost/reordered packets, prefix changes, absent reference, late capture,
wrong address, malformed length and invalid relation. No physical root cause can
be assigned from the supplied aggregate counters. STM32's handoff specifies a
small on-demand USB capture to resolve that evidence gap before configuration
refactoring; no protocol extension or relaxed validation is justified yet.
