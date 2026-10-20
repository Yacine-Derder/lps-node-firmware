# Stage 3F: opt-in PHY packet experiment

Status: CP0–CP6 software experiment implemented and validated; hardware
interoperability UNVERIFIED. Stage 3G is gated on bidirectional valid reception.
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
| Timeout |algorithm dependent |TWR dependent |RX ~0.5s; TX watchdog100ms, low-rate1Hz TX |
| Timestamp |40-bit, nominal15.65ps DTU |40-bit, nominal same DTU; coarse APIs also exist |log raw marker only; origins/calibration/correction semantics NOT interchangeable |

Local evidence: LPS `src/uwb.c`, `dwSetDataRate`, `dwTune` and DW register
constants; e-puck `qorvo_port.c`, `deca_device_api.h`, DW3000 configuration code.
Confirmed: both installed drivers expose the candidate settings. Plausible:
bidirectional frame interoperability, pending the user gate below. Unsupported:
current ch2/ch9 pairing, DW1000 STS/4z or DW3000 110K operation. Unresolved:
actual RF reception, board RF/clock/OTP tuning, TX power,
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
not modified. Proposed central-document update: Stage3F software ready, OTA pending.

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

No physical result has been obtained. The next required evidence is A/B/C logs
and counters above; do not begin Stage3G or claim mixed-radio compatibility yet.
