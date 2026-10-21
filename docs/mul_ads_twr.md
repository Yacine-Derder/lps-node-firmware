# UWB-only ADS-TWR engineering and hardware handoff

User hardware logs demonstrate mixed-radio ADS exchanges. Calibrated accuracy,
reliability and safety acceptance remain **open**; the hardened software in this
handoff has not been flashed. Stage3G's reverse-direction sustained gate remains
open. No new Stage3H work, TDoA/localization, scheduling or calibration constants
are introduced. Source/wire contract: portable/mul_core/README.md.

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

## First mixed-radio ranging review and hardening (2026-10-20)

This handoff requires500 completed exchanges per distance. It audits the existing
ADS foundation; no new Stage3H protocol,
TDoA/localization, scheduling or online calibration stage is started.

The user-operated approximate1m captures are now available locally. Generated
[measurement report](ads_first_hardware_report.md) and
[all219 raw samples/flags](ads_first_hardware_samples.csv) are authoritative for
accounting, hashes, percentiles and selection denominators. Reproduce from LPS:

```sh
python3 tools/test/analyze_ads_logs.py \
  --initiator /home/derder/Documents/ADS-ALR-initiator.log \
  --responder /home/derder/Documents/ADS-RLR-responder.log \
  --reference-m 1.0 --max-range-m 10 --jump-m 0.5 \
  --csv docs/ads_first_hardware_samples.csv \
  --report docs/ads_first_hardware_report.md
python3 -m unittest discover -s tools/test -p test_analyze_ads_logs.py -v
```

If logs are elsewhere, supply their actual paths. If absent, place complete,
unedited `ADS-ALR-initiator.log` and `ADS-RLR-responder.log` under
`/home/derder/Documents/`, then run this command. Raw inputs are never overwritten.
The tool handles concatenated/truncated records, counter resets/wrap, session
changes, duplicate/out-of-order IDs, missing TIMES, nonfinite fields and signed
modulo40 arithmetic. Unique preceding TIMES must match file/epoch/session/id;
legacy untagged TIMES uses only its current capture epoch, never another session.
Unknown responder sessions remain unknown. TIMES-only failed candidates remain
in CSV without becoming successful ranges. Outputs are deterministic; report
line numbers and hashes identify the inspected input. Counter snapshots are
never summed as independent events or pooled across devices.

### Findings and limits

- 219 RESULTS each have six unique matching timestamps. Independent rational
arithmetic agrees with every firmware ToF, ratio and metre output within its
printing precision: max absolute residuals4.991e-7DTU,4.993e-10ratio and
0.00004951m. No association/order/within-exchange wrap defect is demonstrated.
Raw timestamps show26 across-result local clock decreases, consistent with wraps;
no individual exchange interval crosses wrap in these captures.
- All219 finite raw samples: mean1.337567m, median1.3420m, population SD0.187779m,
RMSE0.386280m against the **approximate**1m reference. One negative sample
(id362, -1.3884m) is retained. Primary218 verified nonnegative samples: mean
1.350071m, SD0.034344m, RMSE0.351752m, extrema1.2764–1.4716m. The explicit
MAD rule additionally flags five high samples, leaving213 with mean1.347908m
and SD0.031584m; this filtered subset does not replace the raw evidence or prove
that those five samples are physically invalid.
- Closed initiator cohort IDs326–550 reconstructs219 computed exchanges/225
attempts (97.3333%) and6 aborts/225 (2.6667%). Counter conservation and paired
records support this *one initiator* cohort, including the negative computation.
The earlier endpoint counters cover a large omitted interval and startup failures.
Last STAT is attempt550/done462/abort87/stage1; RESULT550 follows, so at least463
computed exchanges are observed through capture end. Responder window is separate:
first258/244/14 to last483/468/15, both idle, hence224 completions/225 newly
accepted POLLs (99.5556%) and1/225 abort. The483/468/15 lifetime endpoint has
96.8944% completed/resolved. These different windows/definitions are not a PDR.
- Initiator final errors249/timeouts10334; responder1098/8988. Many errors/timeouts
occur idle. Without old abort causes they cannot be assigned to the six or15
active failures. No malformed packets or protocol rejects are recorded, but this
alone does not prove error-free timestamp association or complete logging.
- The capture has a truncated STAT4 concatenated with STAT326. No console drop
message is printed; missing text remains demonstrated. Responder has no INIT,
STOP ACK or LINK_STATUS endpoints, so no complete boot/safety validation follows.

For id362 the within-domain intervals are Ra95581905, Rb47017297, Da47016239,
Db95585851 DTU. Exact recomputation gives -295.932823488DTU and the printed
-1.3884m. Thus the negative value is explained **mathematically by its inputs**,
not by a formatting, wrap or metre-conversion bug. Its ratio1.000035092 is about
35.09ppm, versus neighboring/median~10.02ppm. Ratio is derived from these same
timestamps, not an independent oscillator measurement. A hypothetical single
~3575DTU (~56ns) early t2 marker would explain both deviations, but other
combinations of timestamp errors or time-varying clocks can also fit. This is a
counterfactual diagnostic, **not an identified cause or a correction to apply**.
RX estimation/channel effects, transient timing errors and capture association
remain hypotheses. Constant-rate skew cancellation cannot explain or repair an
arbitrary transient marker error. No equation, antenna constant or observed
measurement was adjusted toward1m.

### UART evidence

One visible `[EPUCK] FAULT uart=5 bits=0x8 drops=14 bad_lines=3 data_bad=0`
occurs between responder attempt360 and361. This is **USART1 e-puck link ORE**
(HAL hardware overrun), not the earlier USART3/ESP fault. Five callbacks and14
dropped/flushed bytes are cumulative endpoints, not five visible new faults;
no initial snapshot is available. STREAM0 is subsequently acknowledged, supporting
configuration recovery, without proving all safety deadlines. Old responder STAT
has no session/transaction tag or synchronized time, so it cannot be aligned to
initiator id362 or used to claim the UART fault caused a range outlier.

Qorvo SPI still masks global PRIMASK around blocking HAL transfers; the SPI
clock remains1.3125MHz. A56-byte body alone occupies341.3us, versus86.8us per
UART character at115200-8N1. UART reception can therefore be delayed; other IRQ-off
sites, USB interrupts and command formatting also exist. No timing trace was
captured in these ADS files. This is a concrete mechanism to measure, not proof
of causation. No driver masking/rate/DMA/priority policy was changed. New optional
ADS DWT maxima expose outer SPI-off/service/start gaps; they are sampled maxima,
not a fault-time trace or WCET. Use a matched radio-disabled baseline and, if
needed, a logic-analyzer GPIO/UART/SPI trace to establish temporal overlap.

### Minimal hardening and parseable diagnostics

Wire protocol v1, PHY, six timestamp math and metre conversion stay unchanged.
`done` retains completed-exchange semantics. Negative raw ToF now has
`range_valid=0`, is counted separately in cumulative `bad_range`, and is excluded
from physical-range acceptance without clamping or suppressing the raw RESULT.
Zero/small positive ToF remains supported; range_valid1 is not a calibration or
uncertainty certificate. The analyzer also flags sub-print-resolution negatives
from exact recomputation. Its configurable10m bound is a declared laboratory
filter, not a general hardware limit.

TIMES and STAT now include session. Active aborts emit one compact record:

```text
MULH ABORT session=0000000078C3DCF1 id=362 cause=3 stage=6 by_stage=0,0,0,0,0,1,0
MULH RESULT session=... id=... tof_dtu=... ratio=... nominal_m=... uncalibrated=1 range_valid=0
MULH STAT attempt=... done=... abort=... reject=... invalid=... err=... to=... stage=... session=... bad_range=...
```

The ABORT example is illustrative, not an explanation of old id362. Cause0
cancel,1 deadline/missing TX completion,2 radio error,3 RX timeout,4 TX start/write
failure,5 encoding failure,6 invalid timestamp ordering/interval/clock/math.
Stage1 POLL_TX,2 RESPONSE_RX,3 RESPONSE_TX,4 FINAL_RX,5 FINAL_TX,6 REPORT_RX,
7 REPORT_TX. by_stage is cumulative, saturating at65535; do not sum snapshots.
Idle RF errors/timeouts do not create active-abort records. Associated math-invalid
REPORTs retain TIMES even without RESULT. `MUL_ADS_TRACE_TIMES=0` / CMake
`-DMUL_ADS_TRACE_TIMES=OFF` reduces normal timestamp output; keep it ON for all
calibration runs. No per-poll logging or general telemetry is added.

### Exact hardened builds and flashing artifacts

Build the DW1000 initiator with a fresh session at each boot/run:

```sh
cd /home/derder/Documents/lps-node-firmware
twr_session=$(date -u +%Y%m%d%H%M%S)
make clean
make -j4 EXTRA_CFLAGS="-DMUL_TWR_ROLE=1 -DMUL_TWR_SESSION=${twr_session}ULL -DMUL_ADS_TRACE_TIMES=1"
cd /home/derder/Documents/epuck-stm32
cmake -S . -B build/AdsResponderTrace -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release -DMUL_UWB_ONLY=ON -DMUL_TWR_ROLE=2 -DMUL_DIAG_ROLE=0 -DPHY_INTEROP_ROLE=0 -DMUL_TIMING_TRACE=ON -DMUL_ADS_TRACE_TIMES=ON
cmake --build build/AdsResponderTrace
```

Exact artifacts:

```text
/home/derder/Documents/lps-node-firmware/bin/lps-node-firmware.elf
/home/derder/Documents/lps-node-firmware/bin/lps-node-firmware.dfu
/home/derder/Documents/epuck-stm32/build/AdsResponderTrace/EPuck_STM32.elf
```

User-only flashing (not performed by Codex): LPS `sudo make dfu` from its checkout
**after** the selected build. STM32CubeProgrammer SWD download/verify/reset the
selected ELF; equivalent command:

```sh
/home/derder/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI -c port=SWD -w /home/derder/Documents/epuck-stm32/build/AdsResponderTrace/EPuck_STM32.elf -v -rst
```

Use `build/AdsResponder` with identical options except MUL_TIMING_TRACE=OFF for a
controlled instrumentation A/B comparison; its ELF is
`/home/derder/Documents/epuck-stm32/build/AdsResponder/EPuck_STM32.elf`.
For reversed ranging roles use DW MUL_TWR_ROLE2 and STM32 MUL_TWR_ROLE1
with a fresh session; exact commands are below. Explicitly keep MUL_TWR_ROLE0
for diagnostic builds. For the radio-disabled
baseline: build UwbOffSafe with MUL_TWR_ROLE0/MUL_DIAG_ROLE0/PHY_INTEROP_ROLE0,
MUL_UWB_ONLY=ON, MUL_TIMING_TRACE=OFF and existing APP_UWB_ENABLED0. Retain the
safety UART. Do not flash ESP/dsPIC or issue motion commands.

### Controlled multi-distance data collection and calibration

1. Secure/disarm the robot, keep wheels clear. Require actual STOP ACK and
STREAM_STATUS confirmed0. Save LINK_STATUS/MOTOR_STATUS before/after each run.
No ESP, Wi-Fi, ARM or motion commands. Stop if STOP acknowledgement or safe
state is uncertain. Capture both consoles before initiating ranging; record
firmware HEADs, portable pin, fresh session, PHY/power, antenna registers,
orientation, antenna height, temperature and any resets.
2. Precisely measure antenna-to-antenna0.5,1,2,3,5m (adjust to available space),
with fixed height/orientation, stationary boards, clear LOS and controlled nearby
reflectors. Record the measured distance and its uncertainty, not just a nominal
label. Keep the same firmware/session/PHY/power for a full series where possible;
record boundaries and reset only with a fresh initiator session.
3. Use distinct complete raw logs per distance. Example separate terminals:

```sh
picocom -b 115200 --logfile ADS-0p5m-ALR.log "$LPS_PORT"
picocom -b 115200 --logfile ADS-0p5m-RLR.log "$STM32_PORT"
```

Collect a fixed >=10minute/>=600-attempt window per distance, requiring **at least
500 completed exchanges**; retain all failures/negative values. If fewer complete,
mark that run failed and investigate; do not silently select only a convenient
successful interval. Repeat placements/series to assess reproducibility and
thermal drift; keep the original raw data. Also collect a matched10minute
UwbOffSafe baseline, and trace-on/off comparison if UART faults recur.
4. Run the analyzer separately for each pair using `--reference-m` equal to the
precisely measured value; choose/predeclare a lab `--max-range-m` and `--jump-m`.
Report raw, primary and robust subsets together, their exact counts/flags, RMSE,
quantiles, negatives, jumps, ratio changes, all abort stages, radio timeouts,
UART faults and console loss. Separate epochs and unmatched windows. Physical
reference uncertainty is not reduced by collecting more radio samples.
5. For distance d_j, estimate b_j=mean(range_j)-d_j, alongside median and interval
estimates. Fit range=beta0+beta1*distance to per-distance summaries, with repeat-run
and ground-truth uncertainty. A stable beta0 and beta1 compatible with1 supports
a combined constant range-offset model. A trend in b_j or slope different from1
suggests distance/power/channel/scale effects that a single offset must not hide.
Use independent repeats or block-resampling for correlated data, and validate on
held-out placements/distances before choosing parameters.
6. Under constant rates/reciprocal LOS, combined residual delay approximately
satisfies sum(e_TX_A,e_RX_A,e_TX_B,e_RX_B)=2*b/c in seconds. One pair cannot identify
the four individual delays or a TX/RX split; reversed roles add a consistency test,
not enough independent constraints. Calibrated-reference and additional device/
RF-reference measurements are needed. See the manufacturer's calibration
[overview/datasheet](https://store.qorvo.com/datasheets/qorvo/dwm1000datasheet.pdf)
and [APS014 listing](https://www.qorvo.com/products/p/DW1000). This single approximate
1m run supplies no calibrated antenna constants. No range correction parameter
is introduced/enabled by this software change.

### Hardware acceptance for the next checkpoint

Predeclare these provisional engineering criteria; they are not a claimed pass:
complete matched captures and >=500 completions at every measured distance;
zero independent timestamp/math/session-association discrepancies; no lost STOP,
unsafe motor state or unexplained safety-UART fault; demonstrated timeout/error
recovery; all negative/implausible samples retained, flagged and investigated.
Target >=95% computed-exchange completion in closed initiator cohorts, while
reporting physical-range acceptance separately. A numerical target is not a
substitute for explaining invalid samples or UART faults. No calibration/accuracy
gate closes while unexplained negative ranges or safety faults remain.

A provisional constant-bias model may advance only if multi-distance/repeat data
support an intercept and slope compatible with1 within stated uncertainty, and
held-out residuals/outliers meet a predeclared application accuracy target.
Proposed initial lab targets are |held-out mean bias|<=0.10m, population SD<=0.05m,
and95th-percentile absolute error<=0.20m at each measured distance, with<=1%
explicitly flagged invalid ranges and no unresolved negative/safety failure.
Freeze or revise these targets before data collection; they are proposed
engineering thresholds, not IPIN/ICRA requirements or a claimed uncertainty bound.
They have not been demonstrated by this approximate-distance run. Sustained reverse
Stage3G communication and a genuine receiver-disabled baseline remain separate
open hardware gates. Hardware operation remains with the user.


## Restore and reverse-role builds

To restore Stage3G reverse communication (DWM3000 TX→DW1000 RX):

```sh
cd /home/derder/Documents/lps-node-firmware
make clean && make -j4 EXTRA_CFLAGS="-DMUL_DIAG_ROLE=2"
cd /home/derder/Documents/epuck-stm32
cmake -S . -B build/UwbOnlyTX -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release -DMUL_TWR_ROLE=0 -DMUL_DIAG_ROLE=1 -DPHY_INTEROP_ROLE=0 -DMUL_UWB_ONLY=ON -DMUL_TIMING_TRACE=OFF
cmake --build build/UwbOnlyTX
```

STM32 ELF `/home/derder/Documents/epuck-stm32/build/UwbOnlyTX/EPuck_STM32.elf`;
LPS ELF/DFU paths are unchanged. User-only SWD/DFU commands above apply after the
chosen builds. For forward diagnostics swap DW role1 and STM32 role2, using
build/UwbOnlyRX. Run >=10minutes/>=500 TX with intact matching metadata logs,
then collect a genuine UwbOffSafe baseline and explicit safety counter endpoints.

Optional reversed ADS adapter test builds (no new protocol stage):

```sh
cd /home/derder/Documents/lps-node-firmware
make clean && make -j4 EXTRA_CFLAGS="-DMUL_TWR_ROLE=2 -DMUL_ADS_TRACE_TIMES=1"
cd /home/derder/Documents/epuck-stm32
twr_session=$(date -u +%Y%m%d%H%M%S)
cmake -S . -B build/AdsInitiator -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release -DMUL_TWR_ROLE=1 -DMUL_TWR_SESSION="$twr_session" -DMUL_DIAG_ROLE=0 -DPHY_INTEROP_ROLE=0 -DMUL_UWB_ONLY=ON -DMUL_TIMING_TRACE=OFF -DMUL_ADS_TRACE_TIMES=ON
cmake --build build/AdsInitiator
```

ELF `/home/derder/Documents/epuck-stm32/build/AdsInitiator/EPuck_STM32.elf`.
Normal restore: LPS `make clean && make`, then user DFU. Configure STM32
NormalBaseline with all radio roles0, UWB-only OFF and tracing OFF, then build
and user-flash its ELF. Existing EEPROM modes, ESP/dsPIC functionality and raw
experiment logs are preserved. No tracked files were removed or archived.

## Software validation checkpoint

Canonical portable pin:0e3b5802e04f87e2c6e5e41ae07c165ae07ea0d1. The equation and
56-byte wire remain unchanged.16 analyzer tests cover malformed/concatenated logs,
resets/wrap/session boundaries, ambiguities, independent arithmetic, negative/
near-zero samples, cohort conservation, statistics and deterministic output.
Portable tests cover the actual id362 vector, known propagation, both roles,
clock offset/skew, wrap, extreme turnaround asymmetry, missed/late messages,
negative/zero ToF, classified timeout and saturated failed-stage counters.
Both radio adapters replay the real negative sample and exercise math rejection;
DWM tests also cover TX write/start failure and maximum-length log formatting,
with TIMES both enabled/disabled. Timing tests check both MULG/MULH labels.

195 default and195 Stage3E continuous tests,178 MUL ASan/UBSan tests, cross-codec/
real-driver DW and retained PHY tests pass. STM32 ADS, Stage3G/PHY,15 console,
9 ESP,9 interleaved coexistence and UWB-only safety suites pass. Eight LPS and
11 STM32 Release profiles build (ADS both roles, traced/quiet, previous diagnostic
and normal profiles). No new driver warning is accepted; retained vendor/newlib
warnings remain. Clean committed-export verification is recorded at delivery.

Release text/data/bss: DW ADS initiator128080/1080/15292, responder127360/1080/15292;
static RAM16372 (12bytes spare), shared scratch128 unchanged. Default123024/1072/
15292 and continuous123096/1072/15292 are unchanged. DWM ADS initiator78636/732/
16248, responder77996/732/16248, responder+trace78444/732/16272; section RAM16980/
17004 versus linker spans16984/17008. RAM/stack estimates do not prove physical
high-water or deadlines. No hardware was flashed.

Host checks from the relevant checkout:

```sh
# LPS (Rake's existing submodule check needs python→Python3 on PATH)
sh portable/mul_core/tests/run.sh
rake unit summary
python3 tools/test/ads.py
python3 tools/test/mul_core.py
python3 tools/test/phy_interop.py
python3 tools/test/mul_slots.py --slot 10 --prop-age 6000 --speed 0 --sanitize
python3 -m unittest discover -s tools/test -p test_analyze_ads_logs.py -v
# STM32
sh tests/ads_radio/run.sh
sh tests/mul_diag/run.sh
sh tests/phy_interop/run.sh
sh tests/epuck_console/run.sh
sh tests/esp_link/run.sh
sh tests/uwb_policy/run.sh
python3 tests/mul_diag/run_coexist.py
```


## Universal STM32 configuration

The legacy build commands above remain available. For a single STM32 image with
runtime ADS roles, IDs, persistent settings, opt-in calibration and bounded local
telemetry, use epuck-stm32 `docs/runtime_uwb.md`. Its motor policy remains UWB-only;
the shared core and DW1000 wire/driver behavior are unchanged. Calibration analysis
of the five nominal-distance runs is in [ads_additive_calibration.md](ads_additive_calibration.md).
The proposed additive offset is not a universal antenna calibration or a gate pass.
