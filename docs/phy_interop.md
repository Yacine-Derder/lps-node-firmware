# Stage 3F: opt-in PHY packet experiment

Status: Stage 3F packet interoperability accepted from user hardware logs reviewed
2026-10-20. Stage 3G may begin; reliability and timing limitations are recorded
in [the experiment results](#hardware-results-reviewed-2026-10-20).
The central project document remains unchanged. Existing e-puck streaming/control
work is preserved; this experiment does not port MUL or perform mixed-radio ranging.

## Evidence and candidate profile

Qorvo's [DWM3000 product specification](https://www.qorvo.com/products/p/DWM3000)
identifies IEEE 802.15.4a operation and channel-5 compatibility with DWM1000.
[Qorvo technical support](https://forum.qorvo.com/t/not-receiving-preamble-tx-is-dw1000-rx-is-dw3000/20974/2)
recommends channel 5, 64/128-symbol preamble and no STS for cross-generation tests.
These establish a plausible supported profile, not reception on these boards.
[Qorvo API guide](https://forum.qorvo.com/uploads/short-url/xD3TlXKvkujjdUaJWXv2b4E7GQN.pdf),
configuration tables, distinguishes IEEE-4a ternary SFD from IEEE-4z binary SFD,
code 9 at 64 MHz PRF, standard PHR rate, and PAC8/SFD timeout129 for preamble128.
The local driver definitions are authoritative for the installed API version.

| Parameter | Existing LPS DW1000 | Existing e-puck DWM3000 | Candidate / assessment |
|---|---|---|---|
| Channel / center | 2 /3993.6 MHz | 9 /7987.2 MHz | 5 /6489.6 MHz; supported by both, other defaults incompatible |
| PRF | 64 MHz | code9 selects BPRF64 | 64 MHz/code9 on both; enum values are generation-specific |
| Preamble |128, EEPROM can select1024 | 128 |128 symbols, PAC8 |
| TX/RX code |9 |9 |9/9, not SCP code25+ |
| SFD | IEEE4a at6.8M; proprietary at lower rates in libdw1000 | IEEE4a | IEEE4a 8-symbol ternary, DWM DWT_SFD_IEEE_4A=0 |
| Data |6.8M, EEPROM optional850K |6.8M |6.8 Mb/s;110K unsupported by DW3000 |
| PHR |standard |standard |standard length,850 kb/s PHR; not extended or data-rate PHR |
| STS / PDOA |not supported |STS off /M0 |off /M0; STS/4z/SCP excluded |
| MAC/FCS |hardware CRC16 |hardware CRC16 |32-byte MAC+payload,34-byte PSDU incl FCS; no ACK/filter |
| RX |IRQ-to-task, manual rearm |normal IRQ callbacks |bounded capture, rearm on frame/error/timeout; no radio wait loops |
| Timeout |algorithm dependent |TWR dependent |RX 50000 UUS (~51.28ms); TX watchdog100ms, low-rate1Hz TX |
| Timestamp |40-bit, nominal15.65ps DTU |40-bit, nominal same DTU; coarse APIs also exist |log raw marker only; origins/calibration/correction semantics NOT interchangeable |

Local evidence: LPS `src/uwb.c`, `dwSetDataRate`, `dwTune` and DW register
constants; e-puck `qorvo_port.c`, `deca_device_api.h`, DW3000 configuration code.
Confirmed: both installed drivers expose the candidate settings, and user logs
demonstrate bidirectional CRC/payload-valid reception on channel5. Unsupported:
current ch2/ch9 pairing, DW1000 STS/4z or DW3000 110K operation. Unresolved:
reproducible delivery performance, board RF/clock/OTP tuning, TX power,
antenna placement and combined control-loop timing. Timestamps are diagnostic
markers only, not mixed-radio range/TDoA validation. No MCS needed.

## Canonical diagnostic packet v1

This is the canonical contract; the e-puck documentation references it. Independent
codec copies and identical JSON golden vectors live in both repositories; no shared
core/submodule is introduced. All multibyte integers are little endian.

| Offset | Bytes | Meaning |
|---|---|---|
|0..1|2|FCF0x8841: data, no security/ACK, PAN compression, short destination/source, frame version0|
|2|1|low byte of sequence|
|3..4|2|PAN0xbccf|
|5..6|2|broadcast0xffff|
|7..8|2|short source0x0084 (DW1000) or0x0086 (DWM3000)|
|9..12|4|ASCII M3FI|
|13|1|protocol version1|
|14..17|4|uint32 sequence, wraps modulo2^32, starts0 per role init|
|18..31|14|byte i=0..13: low8(0xa5 XOR i XOR source XOR (sequence >> (8*(i%4))))|

Exactly32 bytes exclude hardware-generated2-byte CRC16 FCS; PSDU34 bytes. Receiver
CRC-good status must precede codec validation. Validate every byte, exact length,
magic/version/header/source/MAC-sequence/pattern. Reject own-source packets in the
platform wrapper. No retransmission, ACK, application CRC or ranging flag. Failed
decode leaves output unchanged. Gaps count forward sequence advances minus1;
duplicates/out-of-order do not rebase. Half-range or greater advance is old.
First/last losses require correlating TX attempts/completions with receiver logs;
gaps alone are not packet-loss probability. New boots require new log segments.

## Build and activation

Role flags are identical:0 off (default),1 TX,2 RX. Normal mode/address EEPROM
remains untouched. On LPS,1/2 overrides the running algorithm regardless of saved
mode, with fixed diagnostic source84. e-puck uses source86 and the explicit CMake
flag takes precedence over APP_UWB_ENABLED/BOARD_ROLE for this experiment only.
No new NVM/menu fields. TX uses immediate frames near1Hz; this is a packet test,
not the Stage3E hardware-delayed MUL scheduler. MUL scheduling code is unchanged.
DW1000 frame-wait50000 UUS is ~51.28ms, not an error at1Hz: it rearms silently.
DWM3000 uses the same frame-wait and explicit SFD timeout129; libdw1000 retains
its reset SFD detection timeout. This local receive deadline is not an on-air
format difference. Both use PAC8, IEEE4a SFD and explicit manual rearm. DW1000
channel5 tuning uses libdw1000 non-smart defaults (ignores saved force-power only
in diagnostic builds); DWM3000 reuses driver/device defaults, as its old platform
does. Absolute RF power/OTP/antenna tuning remains a board-specific unknown.

## User procedure: connections, flashing and capture

No hardware was flashed during implementation. Use one Bitcraze node and one
existing STM32F405/DWM3000 extension. Connect LPS USB and the STM32's native USB
CDC (not ESP32 USB). Connect the existing ST-Link SWD setup for STM32 flashing;
leave ESP32/dsPIC firmware installed and the robot disarmed. No motors or MCS are
needed. The ST-Link commands below come from e-puck `docs/disconnect_diagnostics.md`;
actual probe connectivity/target power remain user checks, not host validation.

Identify the two CDC ports after each flash/reset (enumeration may change):

```sh
ls -l /dev/serial/by-id/ /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
read -r -p 'Bitcraze CDC device path: ' LPS_PORT
read -r -p 'STM32 native CDC device path: ' STM32_PORT
test -c "$LPS_PORT" && test -c "$STM32_PORT"
```

Do not copy an assumed ttyACM number. If by-id does not distinguish them,
disconnect/reconnect one board at a time and repeat discovery. Close any monitor
before flashing that board. `sudo make dfu` flashes an existing file; build first.

### Experiment A: DW1000 TX -> DWM3000 RX

Build in separate terminals/checkouts:

```sh
cd /home/derder/Documents/lps-node-firmware
make clean && make EXTRA_CFLAGS="-DPHY_INTEROP_ROLE=1"
# User: enter the existing Bitcraze DFU mode, then:
sudo make dfu
```

```sh
cd /home/derder/Documents/epuck-stm32
cmake -S . -B build/PhyRX -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release -DPHY_INTEROP_ROLE=2
cmake --build build/PhyRX
# User: connect the documented ST-Link SWD setup, then:
source /home/derder/.espressif/tools/activate_idf_v6.1.sh
openocd -s "$OPENOCD_SCRIPTS" -f interface/stlink.cfg -f target/stm32f4x.cfg -c 'program build/PhyRX/EPuck_STM32.elf verify reset exit'
```

Re-identify ports. Start the receiver monitor first; in a second terminal run TX
monitor (set the corresponding port variable there too):

```sh
picocom -b 115200 --logfile A-rx.log "$STM32_PORT"
picocom -b 115200 --logfile A-tx.log --exit-after 120000 "$LPS_PORT"
```

These are two concurrent commands, not sequential commands in one terminal.
Receiver should run beyond TX capture; exit it later with Ctrl+A, Ctrl+X.
picocom3.1 and its logfile/exit-after options are present on this development host.

### Experiment B: DWM3000 TX -> DW1000 RX

Close monitors, reverse flags, rebuild/flash both boards with the same procedure:

```sh
cd /home/derder/Documents/lps-node-firmware
make clean && make EXTRA_CFLAGS="-DPHY_INTEROP_ROLE=2"
sudo make dfu
```

```sh
cd /home/derder/Documents/epuck-stm32
cmake -S . -B build/PhyTX -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release -DPHY_INTEROP_ROLE=1
cmake --build build/PhyTX
source /home/derder/.espressif/tools/activate_idf_v6.1.sh
openocd -s "$OPENOCD_SCRIPTS" -f interface/stlink.cfg -f target/stm32f4x.cfg -c 'program build/PhyTX/EPuck_STM32.elf verify reset exit'
```

Discover ports again; receiver first in one terminal, transmitter in another:

```sh
picocom -b 115200 --logfile B-rx.log "$LPS_PORT"
picocom -b 115200 --logfile B-tx.log --exit-after 120000 "$STM32_PORT"
```

### Expected output and Experiment C reliability gate

```text
PHY INIT radio=DW1000 role=TX src=84 ch=5 prf=64 pre=128 code=9 sfd=4A rate=6800 phr=STD sts=OFF bytes=34
PHY TX src=84 seq=0 attempt=1
PHY DONE src=84 seq=0 done=1
PHY RX src=84 seq=0 valid=1 rx=<10 hex digits> gaps=0
PHY STAT tx=0 done=0 valid=1 gaps=0 dup=0 old=0 invalid=0 err=0 to=<count>
```

B swaps source to86; INIT confirms DWM3000 TX and DW1000 RX. Receive markers are
in each receiver's independent clock and must not be subtracted across nodes.
PHY RX is printed only after hardware CRC-good and all32 bytes validate. Invalid
payloads increment invalid; PHY STAT err is aggregate RX/TX failures, to counts
normal idle timeouts. DWM3000 additionally emits `PHY ERR status=<hex> crc=N
phr_rs=N`; DW1000's driver supplies only aggregate failure callback here. Missing
INIT/failed init, done remaining0, invalid/CRC/PHR errors or no valid opposite
source are failure indicators. UART/USB queue loss is another possible cause of
missing log records; do not interpret missing text alone as RF loss.

If both directions show valid packets, keep fixed modest LOS separation (start
around1m, neither touching nor blocking antennas) and collect >=100 completed
transmissions per direction at1Hz. The120s TX capture allows margin. Keep RX
running until after TX capture ends; do not reset mid-capture. Summarize:

```sh
cd /home/derder/Documents/lps-node-firmware
python3 tools/test/analyze_phy_logs.py /path/to/A-tx.log /path/to/A-rx.log --source 84 --minimum 100
python3 tools/test/analyze_phy_logs.py /path/to/B-tx.log /path/to/B-rx.log --source 86 --minimum 100
```

Replace log paths with files actually captured. The helper checks continuous TX
logs, handles uint32 rollover, correlates completion/receive sequences, reports
valid/unique frames, duplicate records, missing sequence IDs, gaps, final error/
timeout counters and extra RX outside the capture window. It exits unsuccessfully
if fewer than100 completions or no reception is demonstrated. This is a report,
not an automatic physical pass: USB logs are best effort. If counter totals and
record counts disagree, repeat with intact serial capture before attributing loss.
Report direction asymmetry, separation, antenna orientation, board IDs, firmware
hashes, exact build flags, both raw logs and both JSON summaries. No additional
robot or MCS is needed. Investigate PHY/SPI/IRQ/init/status errors first; do not
fit propagation or antenna-delay corrections to obtain packet reception.

**Stage3G gate:** user evidence of valid CRC/payload reception in BOTH directions,
>=100 transmissions/direction with packet counts/loss/errors/timeouts reported,
and any reproducible asymmetry explained or explicitly recorded. Compilation,
local SPI mocks, same-family TWR, or a common channel do not meet this gate.
Mixed-radio ranging/timestamps, portable MUL core and localization remain excluded.

## Hardware results reviewed 2026-10-20

The user performed the A/B experiments; Codex inspected all 2535 supplied lines,
not physical hardware. Raw files remain in the sibling `epuck-stm32/` checkout
(ignored local captures, not newly committed). Provenance:

| File | Lines | SHA-256 |
|---|---:|---|
| A-tx.log |1158|`17e8230cbcdcc0d81b1544e3937f62e916847298c5c8d026a125e2f9a03508d7`|
| A-rx.log |881|`efc48f4b996ec7ac1dea4938cc9a40ef79253e3f96fe09d101b7c649bc2c3b0c`|
| B-tx.log |309|`c796de28e3cfda2f944d7027b33bed6437413533d9f0389c144ed752adcfef59`|
| B-rx.log |187|`30e055e0f9d41caafeef806a3b4cc85d1ec2203be644db6f6564f981639e7cd0`|

### Capture accounting

Use one-based file lines and separate capture blocks at INIT/counter resets.
Distinguish printed records from firmware cumulative counters. Forward sequence
gaps count absent valid receptions between accepted sequences; they do not count
leading/trailing capture omissions. Do not subtract independent radio timestamps
to synchronize files. The v1 packet has no boot identifier or transmit timestamp.

A-tx contains four INIT blocks, each with a startup seq0/1 prefix followed by a
discontinuous continuation. B-rx repeats the same seq0 RX timestamp
`2D61F1A48A` in both startup prefixes. These are evidence of duplicated/spliced
capture sections, not sufficient evidence of distinct physical reboots. Never
sum their cumulative counters or treat the discontinuities as RF packet loss.
A-rx has a separate pre-experiment sensor session (lines1–530), then INIT and
reset e-puck counters at531–532; its PHY observations belong to the latter session.

| A TX block (INIT line) | Printed attempts / DONE records | Consecutive continuation | Last DONE counter |
|---|---:|---|---:|
|21|201 /201|369–567 (199 packets)|568|
|645|81 /81|579–657 (79 packets)|658|
|909|19 /19|13–29 (17 packets)|30|
|987|57 /57|36–90 (55 packets)|91|

All 358 printed attempts have matching completion records in their own block;
there are352 distinct sequence values, with repeated startup0/1 prefixes.
Neither358 nor352 is a denominator for A's RX capture. Every printed TX STAT
has err=0,to=0. The first199-packet continuous continuation alone establishes
the >=100 completed-transmission requirement; much of it is outside RX's window.
The highest per-session DONE counter is658, not an additive experiment total.

B-tx starts mid-session atseq66 and ends at134:69 consecutive attempts and69
matching DONE records. DONE counters67–135 establish135 completions since that
transmitter's initialization; the first66 completion lines are outside capture.
STAT is printed before completion, so its final done=134 is correctly superseded
by `PHY DONE ... seq=134 done=135` atline309. All TX STAT err=0,to=0.

### Directional reception and errors

| Evidence | A: DW1000 → DWM3000 | B: DWM3000 → DW1000 |
|---|---|---|
| Main receiver sequence span |2–90:89 sequence positions|0–136:137 sequence positions|
| Firmware-valid receptions |85 (last STAT)|113 (STAT112 through135, then valid RX136)|
| Firmware sequence gaps |4|24 (19 at118 was intermediate)|
| Receiver-span valid fraction |85/89 =95.51%|113/137 =82.48%|
| Printed RX records |82|65 in whole file;58 in main INIT block|
| Duplicate / old / invalid counters |0 /0 /0|0 /0 /0 in both blocks|
| Aggregate RX errors |103 through final STAT|56 through STAT at135; later count unknown|
| RX timeouts |1634 through final STAT|2611 through STAT at135; later count unknown|
| CRC / PHR-or-RS errors |0 /8 (cumulative detail counters)|not classified by this diagnostic driver|

For A, `85 + 4 = 90 - 2 + 1`. Missing printed seq9,10,11 were received:
seq8 and12 both report gaps=1, and valid rises5→10 between seq6 and12.
The four actual receiver gaps are7,35,37,47. Thus seven absent text records
are **not seven RF losses**. Two console messages explicitly report10 and2
dropped output messages (lines535,626); these are not UART byte-drop counters.
Nothing establishes delivery ofseq0/1 before A's first reception or afterseq90.

B's first INIT block (lines21–59) has7 printed RX records, but last STAT
valid24,gaps3,err8,to551 throughseq26. The main block (60–187) has58 RX records,
including a repeated startup0; its counter totals cannot be added to the first
block as independent receptions. In its main block, `113 + 24 = 136 + 1`.
Counter increases account for unprinted earlier receptions; the0→68 text jump
does not mean67 packets were lost. The final seq136 arrives after the last STAT;
56 errors/2611 timeouts are observed lower bounds at that endpoint.

Direct TX/RX record matching provides smaller, incomplete windows:

- A: final TX continuations13–29 and36–90 give72 completed sequences,70
  printed matches (97.22%); absent37,47. Startup0/1 repeats are excluded.
  This excludes the other two receiver gaps and unprinted TX intervals.
- B: bracket68–134 lies within the continuous TX66–134 capture and main RX
  continuation:55/67 printed matches (82.09%); absent71,75,80,93,100,102,
  104,120,124,125,126,128. RX135/136 follow the TX capture;66/67 precede the
  visible RX continuation and must not be declared lost from missing text.

These are descriptive fractions, not a synchronized end-to-end PDR measurement
or a reproducible directional asymmetry. Geometry, antenna orientation, exact
flashed image hashes/build flags and host-aligned capture times were not supplied.
The logs confirm the advertised channel5 profile and diagnostic packet path,
but cannot certify fixed geometry or exact firmware provenance. The strict
`analyze_phy_logs.py` helper intentionally rejects A's discontinuous TX file;
its reset-free assumptions do not apply to these full files. Do not force its
output by merging repeated sequence numbers into one artificial experiment.

### PHY diagnosis

Installed Qorvo `deca_device_api.h` and `dw3000_deca_regs.h`, together with
`Core/Src/phy_interop.c`, define the error interpretation. Of A's63 printed
last-error snapshots,58 are `05800102` (SFD timeout bit`04000000`),4 are
`01801302` (PHR error bit`00001000`), and1 is`01810F02` (RS/sync-loss
bit`00010000`). Snapshot frequencies are **not event counts**: the service logs
only the last status once per reporting interval; err103 and phr_rs8 are the
cumulative counts. CRC remains0; these snapshots show no RX-overrun, CIA,
STS-quality, filtering or SPI-CRC error bit. RCINIT/SPIRDY/PLL-lock and receive
progress bits in the same words are not additional PHY failures.

SFD timeout is classified in `err`, while ordinary frame-wait timeout increments
`to`. At1Hz TX with ~51.28ms frame wait, roughly20 idle timeouts per second are
expected. Neither timeout count is a missing-packet count. Errors may result
from false preamble detection, interference, signal level/orientation or missed
SFD acquisition; none of those causes is isolated here. B has only aggregate
errors, so attributing its56 errors specifically to CRC would be unsupported.
Successful valid reception after errors demonstrates recovery, not error-free RF.

### e-puck UART and sensor diagnosis

A's pre-experiment session has124 consecutive EPUCK status samples with rx=0,
pong=0,data=0 despite advancing tx/started. Later it receives PONG, acknowledges
STREAM and delivers data without recorded UART errors. This demonstrates an
initially absent receive stream, not its electrical/power/firmware cause.
`tx_fail=0` proves local transmission did not report failure; it does not prove
the dsPIC was powered, ready or receiving it. No host time base supports an
exact outage duration. The malformed console-command response atline413 is
separate from USART1 receive/PHY errors.

In A's actual PHY session, EPUCK counters end at rx73945,pong77,data702,
drops27,errors6,bad_lines6,data_bad6,tx_fail0,stream_tx5. Drops/errors first
appear atline623, then increase at715,784,794,861. These are demonstrated
UART receive faults and rejected records, not just USB capture omissions.
`epuck_link.c` counts error callbacks, damaged/full-ring bytes and deliberately
flushed bytes on recovery;27 is not27 lost sensor samples, nor does errors6
identify the hardware error subtype. STREAM re-acknowledgements and continuing
valid sensor records demonstrate recovery. Valid sample ages in this session
range54–175ms at printed snapshots; no motor/watchdog deadline guarantee follows.
Acknowledged/confirmed100 with pending=1 alone is not a failed stream: pending
also represents transaction/fence work in the current streaming state machine.

B's34 EPUCK snapshots show continuous reception: data511→1171 (+660),
pong53→119 (+66), rx53291→122084; all drops/errors/bad_lines/data_bad/tx_fail
remain0, stream_tx1, acknowledged100,confirmed1,pending0. Valid sensor ages
83–92ms are consistent with the100ms stream. DATA time65428→1892 advances
2000ms modulo65536, matching the dsPIC bridge's16-bit unsigned tick in
`epuck_programming/uart_bridge/main.c`; it is not evidence of a restart.
Sampled status output is not the full10Hz DATA stream.

One concrete latency risk exists in the unchanged Qorvo platform: both SPI
rates use84MHz/64 =1.3125MHz, and `qorvo_spi.c` globally masks interrupts for
blocking HAL transfers (`qorvo_irq.c`). A32-byte body alone takes about195us,
versus86.8us per115200-8N1 UART character; RX reads and TX writes can delay
USART1 servicing. This is a plausible overrun mechanism, not a demonstrated
cause of A's six errors: no UART subtype, IRQ-off trace or timing correlation
was captured, and B's TX session has no recorded UART errors. HAL_MAX_DELAY
and the SPI-ready spin also prevent a hard latency bound. Do not change SPI
speed, IRQ ownership, PHY or protocol solely on this evidence.

Before claiming combined control-loop timing safety, capture USART error
subtypes and IRQ-off/service latency alongside DATA ages and console-drop
counters, with a matched UWB-off baseline and RX/TX trials at fixed geometry.
Keep motors disarmed for that diagnosis. A later measured, separately tested
platform fix may be justified; no production firmware change is made here.

### Acceptance decision

**Close Stage 3F's bidirectional packet-interoperability gate; Stage 3G may begin.**
Both hardware CRC-good and strict v1 payload checks pass in both directions;
each transmitter demonstrates >=100 completions, and gaps/errors/timeouts are
assessed with the window limitations above. The gate specifies no zero-loss
or minimum delivery percentage. This is user-performed hardware evidence,
not an agent hardware test or proof of production reliability.

The controlled >=100-packet **overlapping** fixed-geometry reliability run still
needs an intact capture for each direction before making comparable PDR claims.
Keep that follow-up and UART latency diagnosis visible in Stage3G planning.
Stage3G authorization here means its gate is open; no shared-core extraction,
mixed-radio ranging, clock conversion, calibration or localization is implemented
by this review. The central project document still needs its owner's status
update from Stage3F not started to packet gate accepted with these limitations.

### Restore the original default firmwares

Close monitors. Restore LPS default channel2 without changing saved addresses/mode:

```sh
cd /home/derder/Documents/lps-node-firmware
make clean && make
sudo make dfu
```

Or restore the previously working Stage3E stationary continuous configuration:

```sh
make clean && make EXTRA_CFLAGS="-DMUL_EXPERIMENTAL_STATIONARY_RLR_RATE=1 -DMUL_FOLLOWER_SLOT_MS=10 -DMUL_PROP_MAX_AGE_MS=6000 -DMUL_RELATIVE_SPEED_MM_S=0"
sudo make dfu
```

Restore STM32 normal app switches/channel9-if-enabled; explicitly clear the cached
CMake role rather than assuming its previous value was0:

```sh
cd /home/derder/Documents/epuck-stm32
cmake --preset Debug -DPHY_INTEROP_ROLE=0
cmake --build --preset Debug
source /home/derder/.espressif/tools/activate_idf_v6.1.sh
openocd -s "$OPENOCD_SCRIPTS" -f interface/stlink.cfg -f target/stm32f4x.cfg -c 'program build/Debug/EPuck_STM32.elf verify reset exit'
```

The unchanged current app_config has APP_UWB_ENABLED=0; normal boot therefore
leaves DWM3000 disabled. Normal APP_UWB_ENABLED=1 still selects the existing
channel9 Qorvo link and original BOARD_ROLE. No ESP32/dsPIC reflash is required.


## Final software validation and repository handoff

LPS implementation checkpoints25bbc97/3f72be3 and validated operator guide92a8759
are pushed; the final validation-record commit contains this section. E-puck
implementation f7f3e9f and final75a90e630de6bfb24bdcd23f2e25d40b07a52fd1 are pushed
to origin/main. LPS changes are isolated; the e-puck tree deliberately retains
its ten pre-existing streaming/control edits/untracked document, checked by hashes
and excluded from the diagnostic commits. Central/ESP32/dsPIC repositories were
not modified. Proposed central-document update: Stage3F packet gate accepted,
with reliability and combined UART timing limitations as reported above.

Checks completed:
- LPS193 host tests in default and Stage3E continuous10ms configurations;176 MUL
  ASan/UBSan cases plus two diagnostic real-driver cases for each TX/RX role.
- Both independent codecs:3 identical golden vectors,768 single-bit mutations,
  wrong lengths and sequence semantics. Crosscheck: after native runners exist,
  `python3 tools/test/phy_interop.py` from LPS.
- Log analyzer:3 wrap/loss/reset/direction/minimum tests via
  `python3 -m unittest discover -s tools/test -p test_analyze_phy_logs.py`.
- STM32 codec/TX/RX and six APP_UWB/role activation/init-failure/IRQ combinations
  under ASan/UBSan via `sh tests/phy_interop/run.sh`;15 existing console and9 ESP
  safety/telemetry scenarios pass using their original run.sh scripts.
- Clean LPS default, Stage3E continuous and diagnostic TX/RX builds. Default
  text123024/data1072/bss15292 is unchanged:20 bytes beyond reservations.
  Experimental data1080/bss15292 leaves12; existing scratch128 is reused.
  Diagnostic compiler frames80 TX/120 RX, not measured stack high-water.
- STM32 default/experimental Release builds; exported committed-code builds
  excluding unrelated edits also pass all roles and normal UWB-enabled/channel9.
  Workspace RAM28568 default/28992 RX versus clean export28216/28640; difference
  is preserved streaming work. Diagnostic service frames264 TX/280 RX. Disabled
  feature links no diagnostic state. HAL/vendor/library/interrupt timing remains
  outside these compiler-only figures; no hard real-time guarantee is claimed.

Hardware-result review validation: all four full captures were parsed and their
record/counter/window arithmetic checked with assertions. The three host analyzer
regressions, STM32 codec/TX/RX/six activation sanitizer cases, and15 console /
9 ESP regression scenarios were rerun and passed. This review changes
documentation only; firmware/build files and the ten
pre-existing user files were verified unchanged. Firmware was neither rebuilt
for these documentation edits nor flashed. Prior build results above remain
implementation-time validation; they do not identify the flashed hardware image.
