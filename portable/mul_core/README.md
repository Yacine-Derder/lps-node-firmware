# Portable MUL core API 1

Canonical source: `Yacine-Derder/lps-node-firmware:portable/mul_core`.
LGPL-3.0-or-later, as the extracted MUL implementation. Only edit this canonical
folder. STM32 carries a deterministic committed vendor snapshot. No remote or
package manager is added. Firmware builds require no sibling checkout/network.

`mul_common.h/.c` preserve the legacy34-byte type1 codec, complete ALR identities,
sequence state and signed modular40-bit helpers byte-for-byte. Their existing
void encoder/arithmetic pointer preconditions remain unchanged. LPS `inc/src`
forwarding files preserve build/test discovery. Clock/relation/TWR/correction/
localization records stay in their established LPS modules; no lifecycle or
observation fields are redefined here.

`mul_core.h/.c` add status-returning bounds-checked diagnostic encode/decode,
full84/85/86 source validation, resettable sequence accounting and caller-owned
observation construction. Null/bounds/format failures do not modify decoder
output. Counters wrap uint32; forward ordering is unambiguous below2^31.
Duplicates/old frames count as valid but do not advance the baseline or undo gaps.
Reset explicitly after a known boot/session change; packets have no boot epoch.
No allocation, logging, drivers or RTOS. Fixed scan lengths, no unbounded loops.
Source ABI only: padding/struct sizes are not a wire or binary ABI.

## Diagnostic v1 wire format

44 bytes excluding2-byte hardware FCS. This is a NEW type0x70, not legacy
broadcast type1, relation type2 or TWR. Stage3E decoders reject it and remain
unchanged. No mixed-radio clock/range/TDoA/correction availability is implied.
All multibyte fields little endian; no struct overlays.

| Offset | Size | Meaning |
|---|---:|---|
|0–20|21|Legacy extended MAC:41 dc, low sequence byte, PANcf bc, broadcast8xff, source8 bytes|
|21–23|3|ASCII MUL|
|24|1|Diagnostic type0x70|
|25|1|Version1|
|26–29|4|Current sequence32, resets0, wraps modulo2^32|
|30|1|Previous completed TX marker valid:0/1; other bits rejected|
|31|1|Timestamp width40|
|32|1|Unit1: nominal1/(499.2MHz*128) DTU, not calibrated physical SI time|
|33|1|Sender radio family:1 DW1000,2 DW3000; full source identifies the clock domain|
|34–37|4|Sequence of the reported previous completed TX|
|38–42|5|Previous actual hardware TX marker, source clock, full40 bits|
|43|1|Deterministic pattern0xa5|

Absent marker requires both marker fields zero. Valid marker must precede the
current sequence by1..2^31-1; zero timestamp can be valid. It need not be the
immediate predecessor if a TX completion failed. Immediate transmission cannot
embed its own future actual timestamp: first frame has no marker, completion
is logged locally and next frame carries that measured marker and its sequence.
Hardware TX values include configured antenna adjustment; physical calibration
is unverified. RX observations preserve raw hardware40-bit receiver markers,
receiver identity/family, message validity and unit/width contract. Application
power correction is not applied. Never subtract clocks of different sources.
Exactly-half-period signed differences map to-2^39; use only within half-period.
Encode requires capacity>=44; decode requires length EXACTLY44. Strict checks
cover MAC/destination/PAN/magic/type/version/flags/width/units/family/source and
marker ordering. Adapters additionally require the expected peer identity/family
and hardware CRC-good, and own sequence-baseline lifetime.

## Platform boundary and tests

Platform owns PHY, CRC/FCS, SPI/IRQ, timestamp capture, scheduling/rearm, storage,
monotonic host-ms timers and transport. The core never schedules the radio.
Use `sh portable/mul_core/tests/run.sh` in LPS or
`sh ThirdParty/mul_core/tests/run.sh` in STM32 for sanitizer/golden/20000-input
malformed tests. Same sources compile on host/Cortex-M0/Cortex-M4.

## Pinned export/update

Publish canonical changes to LPS origin/master FIRST, then from LPS run:
`python3 tools/mul_core_vendor.py <full-published-LPS-SHA> <empty-destination>`.
Review/replace STM32's `ThirdParty/mul_core` with that export and commit it there.
UPSTREAM.json records commit, canonical path and every file hash. STM32 build
and native core tests verify all files offline and reject drift (also added files).
Run both core tests, cross-repo byte/hash comparison and firmware regressions
before publishing the consumer. Record compatible firmware SHAs in PLAN/handoff.
Fresh clone: committed snapshot is sufficient; standard LPS driver submodule
checkout is still required, no new submodule. Recovery: checkout the matching
firmware commits; do not update a core implicitly during configure/build.
