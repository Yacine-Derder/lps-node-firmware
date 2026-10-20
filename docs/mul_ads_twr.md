# UWB-only ADS-TWR engineering and hardware handoff

Software foundation implemented; hardware ranging, accuracy and Stage3H hardware
validation are **unverified**. Stage3G's reverse-direction sustained hardware gate
also remains open. No hardware was flashed by Codex. No TDoA, localization,
multi-robot scheduling, motion control or online calibration is added here.

## Stage3G audit and evidence

The three new raw files exist at `/home/derder/Documents/` and were read fully.
`G-UWB-only-tx.log` prints535 DONE records through seq536/done537 (seq1/2 DONE
text absent). RX prints519 packets: seq2991–2994, then515 distinct low sequences
4–537. Final STAT is valid525/old519/dup0/gaps0/invalid0/err1020/to12566;
CRC0/PHR-RS59. All515 comparable previous-TX markers match TX DONE exactly.
Missing text and unmatched endpoints prevent a synchronized delivery percentage.
Nineteen missing *printed* positions in4–537 are not nineteen proven RF losses.

Root cause of OLD accumulation is reproducible software baseline lifetime:
`mulCoreObserveSequence` increments valid for every decoded expected-peer frame,
then compares against the last accepted sequence. OLD does not update that last
sequence. After2994 (or an unprinted later high value), low restart sequences
remain OLD until catching up. Thus valid and old intentionally overlap; accepted
is valid-old-dup modulo2^32. Gaps count forward holes within an accepted epoch,
not loss across a restart. OLD is not evidence of stale RF packets. Diagnostic
v1 has no session ID, so it cannot distinguish rapid restart from out-of-order
traffic. A source identity requires its own sequence context; adapters accept
only their configured peer. Full32-bit serial wrap remains correct below2^31.

The capture shows a DW1000 startup at seq0 and an earlier high RX prefix. This
supports a sender restart or mixed capture epochs while RX state persisted;
it cannot establish exact reset timing or whether early output was queued.
A358-message console overflow and omitted sections invalidate claims based on
complete printed windows. TIME calls also span a long omitted interval.

Fix: both adapters make `order` explicit (0 first,1 forward,2 gap,3 duplicate,
4 old). After >=5000 host milliseconds with no decoded expected peer, they log
`MULG EPOCH reason=idle previous=... next=... idle_ms=...` and discard only the
ordering baseline, preserving cumulative counters. No gaps are inferred across
that boundary. This is a declared idle epoch policy, not a boot detector.
A rapid restart without sufficient silence still needs a receiver reset.
New ADS packets instead require an explicit session and transaction.

UWB-only capture shows startup STOP and STREAM0 acknowledgements and no printed
e-puck FAULT. No LINK_STATUS endpoints were captured, so this does not prove
zero cumulative UART faults. SPI-off max50052cycles/168MHz=297.929us;
service124704=742.286us, start gap169401=1008.339us. These exceed a115200-8N1
character interval in places but do not demonstrate SPI-caused UART failure.
The previously confirmed G-C disabled baseline remains the earlier evidence.
**`G-UWB-off-baseline.log` is not an STM32 baseline:** it contains DW1000 MULG
TX849–1361. Preserve it; acquire a correctly identified receiver baseline.

SHA-256 provenance:

```text
e053ba625f4923f409f0fef1c52e0046794f5e6c557e6378f6a638550d97fa45 G-UWB-only-tx.log
e4f52ccad6675167d6997ae682e30d0c0cc77ab0564347c77d1110c82609468a G-UWB-only-rx.log
7c7a4e36426e2887d21d8c42ffc079ba91000c1c419cfec2bf9cb965894324ea G-UWB-off-baseline.log
```

## Protocol, state and arithmetic

Canonical [portable API/wire specification](../portable/mul_core/README.md#ads-twr-foundation-separate-protocol-v1)
is authoritative. Separate56-byte protocol v1 types0x71–0x74 preserve all Stage3E
and Stage3G packet bytes. Extended unicast source/destination, nonzero session64,
transaction32, width40 and nominal DTU unit are checked before state transitions.

Three ranging messages POLL→RESPONSE→FINAL are followed by REPORT returning
responder t2/t3/t6. Initiator captures t1/t4/t5 locally. Calculation occurs there
only after actual FINAL TX completion and a matching REPORT. This fourth packet
is a deliberate measured-timestamp follow-up, not a predicted FINAL timestamp.
Responder REPORT completion does not prove receipt or initiator calculation.

Initiator: idle→POLL TX→RESPONSE RX→FINAL TX→REPORT RX→idle. Responder:
idle→POLL RX→RESPONSE TX→FINAL RX→REPORT TX→idle. Every TX stage requires an
actual completion event. One exchange at a time, fixed buffers, no allocation,
no blocking exchange waits. Stage deadline100ms; radio frame-wait timeout~51ms
can abort earlier. Initiator retries whole exchanges at1Hz with a new transaction.
Duplicate, stale, wrong identity/session/type/transaction/state packets neither
advance state nor refresh deadlines. Radio errors, failed starts or missing
TX completion abort; no measurement is committed on failure.

Session must be fresh for every initiator boot/run: choose a new nonzero uint64
MUL_TWR_SESSION. Reusing the same ID with reset transaction counter can be
rejected by a surviving responder. There is no EEPROM write or automatic nonce.
Responder remembers ordering only for its current session; this is not security
or persistent replay protection. Reset both peers or choose a new session after
interruption. Initiator refuses transaction wrap until reinitialized.

Differences are modulo40 **within each local clock**:
Ra=t4-t1, Rb=t6-t3, Da=t5-t4, Db=t3-t2. Each must be positive and <=100ms,
well below the half-wrap ambiguity. ToF=(Ra*Rb-Da*Db)/(Ra+Rb+Da+Db), using binary64
before multiplication; bounded cancellation error is below0.00001DTU, with no
uint64 product overflow. See [Qorvo/Decawave APS013, figure2](https://forum.qorvo.com/uploads/short-url/5yIaZ3A99NNf2uPHsUPjoBLr2Ua.pdf).

Estimated responder/initiator rate ratio=(Rb+Db)/(Ra+Da) must be within +/-1000ppm.
With constant rates fa/fb and reciprocal propagation p seconds, standard ADS
returns 2*fa*fb/(fa+fb)*p in mixed nominal DTU, not exact initiator DTU. Constant
clock offsets cancel. Frequency changes, motion, channel asymmetry and multipath
remain errors. Fractional/negative results are retained to investigate bias.
`nominal_m` uses299792458/63897600000 and is explicitly UNCALIBRATED, not a
physical accuracy claim. No empirical offset or power-bias correction is added.

## Timestamp and radio adapter audit

DW1000: `dwGetTransmitTimestamp` reads all5 TX_TIME bytes after TXFRS;
`dwGetRawReceiveTimestamp` reads all5 RX_TIME bytes on CRC-good. "Raw" skips
libdw1000's software received-power correction; hardware RX antenna adjustment
still applies. TX delay is added by hardware and RX delay subtracted. DWM3000:
`dwt_readtxtimestamp` and `dwt_readrxtimestamp(...,DWT_COMPAT_NONE)` read the full
5-byte adjusted markers; no software received-power correction is applied.
Both expose logically adjusted RMARKER timestamps, but their physical antenna
reference equivalence has **not** been calibrated. Independent clocks must
never be directly subtracted. Serialization preserves all40 bits and wrap.

Existing diagnostic DW1000 config uses zero antenna delay. The DWM diagnostic
probe/profile does not program a calibrated antenna delay either. These packet
experiments did not establish physical timestamp equivalence. New ADS profile
explicitly programs/readbacks nominal defaults: DW1000 TX/RX16384 DTU
(libdw1000's nominal startup value), DWM3000 TX/RX16380 DTU (existing platform
nominal). They are initial estimates, not fitted corrections. Build overrides
MUL_ADS_DW_ANT_DTU/MUL_ADS_DWM_ANT_DTU must be recorded. Antenna readback failure
prevents start. Existing ALR-specific calibration/Stage3E registers stay unchanged.

Both adapters use immediate TX plus automatic RX-after-TX for the next packet;
measured stamps make turnaround latency irrelevant to the constant-rate formula.
No delayed TX is requested, so scheduling quantization/late-start substitution
cannot affect a reported timestamp. DW1000 dwSetDelay/DX_TIME uses512-DTU
alignment and adds antenna delay to its returned estimate; DWM delayed-TX API
similarly uses high timestamp bits with alignment constraints. Future delayed
replies must verify intended versus measured TX and abort on late start.
Here a start failure/missing TXFRS simply aborts. IRQ/SPI speeds/PHY remain the
proven channel5/PRF64/code9/preamble128/IEEE4a-SFD/6.8M/stdPHR profile. DWM polling
is at most once/ms, task level; DW callback service wakes at most5ms when idle.
No fine deterministic reply interval or physical deadline is promised.

## Exact builds, artifacts and user-only flashing

First test: DW1000 initiator84, DWM3000 responder86. Keep robots stationary,
secured with wheels clear. ESP and sensor streaming remain off. Require actual
startup STOP ACK/disarmed and STREAM_STATUS confirmed0 on STM32. ARM/movement
are rejected; watchdog, emergency STOP and UART safety servicing stay active.
The new STM32 profile refuses configuration unless MUL_UWB_ONLY=ON and both
other radio roles are0. Default normal mode and diagnostics remain available.
No ESP or dsPIC update is required for these UWB-only experiments.

```sh
cd /home/derder/Documents/lps-node-firmware
twr_session=$(date -u +%Y%m%d%H%M%S)
make clean
make -j4 EXTRA_CFLAGS="-DMUL_TWR_ROLE=1 -DMUL_TWR_SESSION=${twr_session}ULL"
cd /home/derder/Documents/epuck-stm32
cmake -S . -B build/AdsResponder -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release -DPHY_INTEROP_ROLE=0 -DMUL_DIAG_ROLE=0 -DMUL_TWR_ROLE=2 -DMUL_TIMING_TRACE=OFF -DMUL_UWB_ONLY=ON
cmake --build build/AdsResponder
```

Artifacts: `/home/derder/Documents/lps-node-firmware/bin/lps-node-firmware.elf`
and `/home/derder/Documents/lps-node-firmware/bin/lps-node-firmware.dfu`; STM32 `/home/derder/Documents/epuck-stm32/build/AdsResponder/EPuck_STM32.elf`.
Only the user flashes: LPS existing DFU procedure `sudo make dfu` from the LPS
checkout **after the chosen build**; this target does not build. STM32CubeProgrammer
open the selected ELF over the existing ST-LINK/SWD, download/verify/reset, or:

```sh
/home/derder/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI -c port=SWD -w /home/derder/Documents/epuck-stm32/build/AdsResponder/EPuck_STM32.elf -v -rst
```

For reversed ranging roles (software-tested, physical test later):

```sh
cd /home/derder/Documents/lps-node-firmware
make clean && make -j4 EXTRA_CFLAGS="-DMUL_TWR_ROLE=2"
cd /home/derder/Documents/epuck-stm32
twr_session=$(date -u +%Y%m%d%H%M%S)
cmake -S . -B build/AdsInitiator -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release -DPHY_INTEROP_ROLE=0 -DMUL_DIAG_ROLE=0 -DMUL_TWR_ROLE=1 -DMUL_TWR_SESSION="$twr_session" -DMUL_TIMING_TRACE=OFF -DMUL_UWB_ONLY=ON
cmake --build build/AdsInitiator
```

STM32 ELF: `/home/derder/Documents/epuck-stm32/build/AdsInitiator/EPuck_STM32.elf`.
Do not reuse the initiator session after a reboot. LPS flags require clean builds.

## First stationary experiment and acceptance

Start complete captures in separate terminals BEFORE restarting the initiator:

```sh
picocom -b 115200 --logfile H-DW-init.log "$LPS_PORT"
picocom -b 115200 --logfile H-DWM-resp.log "$STM32_PORT"
```

Expect INIT role1/2, channel5, version1, bytes56, nominal antenna values;
STM32 startup STOP/STREAM0 ACK. Once per second expect MULH STAT; initiator also:

```text
MULH TIMES id=1 t1=... t2=... t3=... t4=... t5=... t6=...
MULH RESULT session=000000000000007B id=1 tof_dtu=... ratio=... nominal_m=... uncalibrated=1
MULH STAT attempt=... done=... abort=... reject=... invalid=... err=... to=... stage=...
```

TIMES preserves ten hex digits for each of the six measured inputs; t1/t4/t5
are initiator clock, t2/t3/t6 responder clock. Values above are illustrative. Responder done counts REPORT TX, initiator done
counts successful calculations. Abort is active exchange cancellation; invalid
is decoding failure, reject is decoded but state/identity mismatch. Errors and
idle RX timeouts are not packet loss. No per-poll output. Keep console drop
messages, counter endpoints and exact build HEADs/pin/session; never sum reset
epochs. Query LINK_STATUS, STREAM_STATUS and MOTOR_STATUS at start/end and after
faults. No motor operation is needed or authorized by this experiment.

Measure antenna-to-antenna distances0.5,1,2,3,5m in clear LOS, fixed height,
orientation/power/channel, away from reflective surfaces. At each distance,
record at least200 attempts (target200 successful) in each of three repeated
placements; retain failures, do not stop early to discard bad samples. Include
runs >40s for timestamp wrap. Log ground-truth distance uncertainty, temperature,
antenna register values and any intentional interruption. Fit/compare mean bias
(range minus measured distance), median, SD, MAD, percentiles and outlier count
using an explicitly stated rule (e.g. >3MAD); report all samples as well as any
filtered subset. Report successful initiator results/attempts in matched windows,
abort reasons/counters and recovery time after covering/separating/resetting a
peer. Use a fresh session after initiator reboot. Check >40-bit wrap stability.

Fail/stop on unsafe/unknown motor state, lost STOP acknowledgement, radio init
failure, no recovery after timeout, impossible ordering/session acceptance,
nonfinite result, repeated console loss or unexplained UART errors. Large bias
or negative nominal distances must be retained and investigated, not corrected
with an arbitrary constant. Determine physical TX+RX antenna bias from several
known separations and orientations; one pair alone cannot uniquely identify
four per-device TX/RX delays. Add calibrated-peer/reference measurements and
reverse roles to separate device contributions; characterize power/multipath
and temperature dependence before claiming a calibration or uncertainty bound.

### Restore and close remaining Stage3G gates

Restore diagnostics with explicit MUL_TWR_ROLE=0. LPS: `make clean && make
EXTRA_CFLAGS="-DMUL_DIAG_ROLE=2"` for RX, role1 for TX; user DFU after build.
STM32: configure UwbOnlyTX/UwbOnlyRX as in [the Stage3G guide](mul_core.md#next-user-experiments--uwb-first),
adding `-DMUL_TWR_ROLE=0`; select the corresponding `build/<profile>/EPuck_STM32.elf`.
Collect sustained reverse DWM3000 TX→DW1000 RX >=10min/>=500 completed TX with
matching metadata, then repeat forward with clean epoch diagnostics and intact
windows. Capture a genuine STM32 UwbOffSafe baseline plus explicit LINK_STATUS
endpoints. This is still needed to close the original communication/safety gate.
Normal recovery: LPS `make clean && make`; STM32 NormalBaseline with all radio
roles0/UWB-only OFF. Existing EEPROM modes are untouched. Optional Wi-Fi repair
is outside this iteration; previous normal functionality remains available.

## Cleanup inventory and verification boundary

Inspected tracked source/docs and artifact/reference lists in all four repos.
No tracked logs/binaries/object files were cleanup candidates. Candidate directories:
LPS bin (~3MB), STM32 build (~85MB before new profiles), ESP32 build (~328MB).
They include tested rollback images/configuration or unclear historical value;
**none removed**. Raw A/B, G-A/B/C, disabled-baseline and UWB-only captures are
experimental evidence and remain untouched, including the misnamed baseline.
No untracked user file, dsPIC/ESP functionality, TDoA implementation, calibration
record or historical design document was deleted/archived. No demonstrably dead
source/helper/script justified removal. Existing diagnostic guides remain useful;
this guide is authoritative for ADS and links to those retained procedures.
LPS ignore rules now cover local *.log and Python caches; ignore is not deletion.
Potential old build directories may be removed only after their owner identifies
what rollback/configuration evidence to retain. No broad cleanup command used.

Portable core/math/state/golden/fuzz tests and both adapter-role suites pass host
sanitizers. Safety, diagnostic/PHY and Stage3E regressions are checked separately.
Firmware compilation is evidence of integration, not radio exchange, physical
accuracy, measured UART latency or motor safety. See PLAN.md for current status.


Verified resource/build checkpoint (Release, text/data/bss bytes): LPS default
123024/1072/15292, Stage3E continuous123096/1072/15292 (both unchanged), ADS
initiator127168/1080/15292 and responder126616/1080/15292. The shared128-byte
scratch is unchanged; experimental static RAM16372 leaves12 bytes spare.
DWM3000 ADS initiator78052/732/16224, responder77556/732/16224; section RAM16956,
linker RAM span16960 and FLASH78844/78344 including alignment. Compiler frames:
DW callback224 bytes; STM32 ADS process376, init224; portable receive80/calculate64
on Cortex-M4, plus caller/HAL/libc/IRQ frames. LPS UWB task reservation1600 bytes,
STM32 linker stack minimum1024; these are not physical high-water measurements.
Existing vendor/newlib warnings are retained; no new adapter warning is accepted.

195 default and195 continuous LPS tests pass;178 MUL ASan/UBSan tests pass,
plus independent portable ADS/core suites, real-driver DW roles and STM32 ADS
roles/activation/failure tests. STM32 diagnostic/PHY,15 console/9 ESP/9 interleaved
coexistence and UWB-only safety tests pass. Eight LPS profiles (default,
continuous, both Stage3F, both Stage3G, both ADS) and nine STM32 profiles (both
ADS plus UwbOnlyRX/TX, UwbOffSafe, CoexistRX, NormalBaseline, both Stage3F)
build. Delayed-TX failure is intentionally inapplicable: tests assert immediate
TX mode, start failure and missing completion; no delayed scheduling is called.
The canonical portable core pin is ccd78acfb56ac9ea42c31b88d5db458380c67564.

Reproduce host checks from the relevant checkout:

```sh
# LPS; Rake's existing submodule check expects python to resolve to Python3.
sh portable/mul_core/tests/run.sh
rake unit summary
python3 tools/test/ads.py
python3 tools/test/mul_core.py
python3 tools/test/phy_interop.py
python3 tools/test/mul_slots.py --slot 10 --prop-age 6000 --speed 0 --sanitize
# STM32
sh tests/ads_radio/run.sh
sh tests/mul_diag/run.sh
sh tests/phy_interop/run.sh
sh tests/epuck_console/run.sh
sh tests/esp_link/run.sh
sh tests/uwb_policy/run.sh
python3 tests/mul_diag/run_coexist.py
```

Canonical/vendor verification runs offline. Local test tooling used a temporary
python→python3 PATH shim outside the repositories; no dependency was changed.
