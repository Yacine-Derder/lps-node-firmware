# Stage 3G: portable MUL core and diagnostic adapters

Software implementation and host/build validation are complete. Hardware
validation is pending. Do not begin Stage 3H or infer mixed-radio ranging,
clock conversion, antenna calibration or physical timing accuracy from this work.

## Source ownership, compatibility and platform boundary

Canonical source is this repository's [portable/mul_core](../portable/mul_core/README.md).
That README defines the public C APIs, source ABI, exact 44-byte diagnostic v1
wire format, timestamp/sequence contracts, tests and explicit export/update
procedure. Existing `inc/src/mul_common` files forward to the extracted code,
which is byte-identical to the pre-extraction implementation. Existing Stage 3E
34/74/84-byte packet formats, correction ages/states, signed TDoA, rate units,
observation fields, estimators and radio state machines are preserved.

A new type `0x70`, version 1 uses the existing extended-address MUL MAC header;
it is not a Stage 3E broadcast, clock relation or TWR exchange. It carries full
source identity, sequence and explicit 40-bit nominal DTU metadata. Since TX is
immediate, completion logs the actual hardware marker, and the NEXT packet
carries that measured marker and its sequence. First TX has unavailable metadata;
a numeric zero can subsequently be a valid timestamp. No scheduled/current TX
time is guessed. RX logs preserve raw hardware markers before rearming, without
application power correction. Configured hardware antenna adjustment remains
radio specific and physically uncalibrated. Never subtract independent clocks.

| Source / API                                     | Responsibility                                                                                                                            |
| ------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------- |
| `portable/mul_core/mul_common.h/.c`            | Preserved legacy codec, identities, sequence state, 40-bit encode/decode/mask/signed difference                                           |
| `portable/mul_core/mul_core.h/.c`              | `mulCoreEncodeDiag`, `mulCoreDecodeDiag`, `mulCoreSourceValid`, reset/observe sequence, caller-owned `mulCoreObserve` observation |
| `inc/mul_diag_dw.h`, `src/mul_diag_dw.c`     | DW1000 diagnostic state, hardware capture, immediate TX, RX rearm and USB task logging                                                    |
| `src/phy_interop_dw.c`                         | Unchanged common channel-5 PHY setup; Stage 3F codec/path retained                                                                        |
| STM32 `Core/Src/mul_diag.c`                    | Corresponding bounded polling adapter; no radio ISR or queue added                                                                        |
| STM32 `Core/Src/uwb_diag_profile.c`            | One shared Stage 3F/3G configuration using existing Qorvo SPI/driver                                                                      |
| STM32 `Core/Src/mul_timing.c`, `qorvo_irq.c` | Optional cycle measurement only; interrupt ownership/speed unchanged                                                                      |

A standalone remote/submodule was considered and rejected as unnecessary
administration for this small extraction. STM32 vendors an immutable snapshot
from a published LPS commit, with `UPSTREAM.json` and every file's SHA-256. Its
configure AND build verify hashes offline and reject modified/extra files.
No sibling checkout is required to build either firmware. Only canonical LPS
files may be edited; regenerate/review/commit the consumer snapshot explicitly.
There is no implicit fetch/update or package manager. LPS Make explicitly
invalidates firmware objects when canonical core C/header inputs change,
including code behind the compatibility wrappers. The real Make dependency
graph and a source-mtime rebuild were checked; changing build-role flags still
requires the documented clean build.

Compatible implementation checkpoints (both published):

- LPS / canonical core: `1b807c5c7507d67d802ee976ae4dfe5b27fd88e9`.
- STM32 consumer: `1bd3ecdba534549b4c9ac89f17fd8bd7270fe056`.
- STM32 `UPSTREAM.json` pins the LPS checkpoint above. Later commits
  retain that exact dependency. Record both final firmware HEADs with each capture.

Fresh clone recovery: clone LPS with `--recurse-submodules` (existing driver/test
submodules), or run `git submodule update --init --recursive` in its checkout.
STM32's committed snapshot is sufficient; no new submodule or local unpublished
path is needed. The consumer's standalone tests work without the LPS checkout.

Host prerequisites remain the existing ARM GCC/Make and CMake/Ninja toolchains,
Ruby/Rake for LPS tests and a native C compiler. Python 3 is required for STM32
offline hash verification; the explicit LPS export script requires Python 3.9+.
No Python packages or network access are needed for that verification.

## Initial extraction validation and resource evidence

The resource figures below describe the original extraction checkpoint. Current
follow-up policy builds and hardware evidence are recorded later in this guide.

Commands run successfully:

```sh
# LPS checkout
sh portable/mul_core/tests/run.sh
python3 tools/test/mul_slots.py --slot 500 --default-rate
python3 tools/test/mul_slots.py --slot 10 --prop-age 6000 --speed 0 --sanitize
python3 tools/test/mul_core.py
python3 tools/test/phy_interop.py
# STM32 checkout
sh ThirdParty/mul_core/tests/run.sh
sh tests/mul_diag/run.sh
python3 tests/mul_diag/run_coexist.py
sh tests/phy_interop/run.sh
sh tests/epuck_console/run.sh
sh tests/esp_link/run.sh
```

195 LPS tests pass for default and continuous stationary 10 ms policy; 178 MUL
ASan/UBSan cases pass. Both new diagnostic DW1000 roles pass real-driver SPI
register tests under sanitizers, including profile/FCS, previous completed TX,
RX length/rearm, watchdog, serial and host-tick wrap. Stage 3F golden/profile
checks still pass. Canonical/vendor bytes and golden vectors match exactly.
Each core snapshot passes three new diagnostic vectors plus legacy equivalence,
null/bounds/length/magic/type/version/source/metadata rejection, observation,
state reset/sequence/40-bit wrap and 20,000 malformed input checks. Vendor-drift
checks reject both modified and extra files; invalid/mutually enabled build
roles are rejected.

STM32 TX/RX mocks and all six APP_UWB x diagnostic-role combinations pass;
failed initialization leaves radio IRQ disabled. Error/CRC-good priority,
RX-enable retries, passive RX, timestamp capture and one-pass-per-ms service
are checked. Nine workspace ESP/safety/sensor scenarios also pass with the real
MUL RX task interleaved, recoverable PHY errors and deliberate bounded USB queue
overflow. Fifteen console and nine ESP regressions pass unchanged. A clean
committed-tree export excluding all user edits independently passes core/adapter,
Stage 3F, six console/eight ESP and eight interleaved coexistence scenarios.
Mocks establish cooperative state/queue behavior, not interrupt latency.

Clean builds: LPS default, Stage 3E continuous, Stage 3F TX/RX, Stage 3G TX/RX;
STM32 Release default, Stage 3F TX/RX, Stage 3G TX/RX with tracing off and RX with
tracing on. Clean committed STM32 export also builds default, Stage 3F RX,
Stage 3G TX/RX and retained normal APP_UWB_ENABLED=1/channel-9 code.

| Build                                     | Text / data / bss bytes      | Static RAM including linker reservations |
| ----------------------------------------- | ---------------------------- | ---------------------------------------: |
| LPS default                               | 123024 /1072 /15292          |                   16364 (20 bytes spare) |
| LPS Stage 3E continuous                   | 123096 /1072 /15292          |                                    16364 |
| LPS Stage 3F TX / RX                      | 123992 or124120 /1080 /15292 |                         16372 (12 spare) |
| LPS Stage 3G TX / RX                      | 124136 or124352 /1080 /15292 |                         16372 (12 spare) |
| STM32 workspace default                   | 52600 /712 /27856            |                                    28568 |
| STM32 workspace Stage 3F TX / RX          | 78628 or78852 /736 /28256    |                                    28992 |
| STM32 workspace Stage 3G TX / RX          | 79136 or79260 /736 /28288    |                                    29024 |
| STM32 workspace Stage 3G RX + trace       | 79700 /736 /28312            |                                    29048 |
| STM32 clean committed default             | 50000 /712 /27504            |                                    28216 |
| STM32 clean committed Stage 3F RX         | 76256 /736 /27904            |                                    28640 |
| STM32 clean committed Stage 3G TX         | 76536 /736 /27936            |                                    28672 |
| STM32 clean committed Stage 3G RX + trace | 77100 /736 /27960            |                                    28696 |

Text+data gives flash section contents; the linker FLASH span also includes
section-alignment gaps. STM32 workspace linker spans are53,372 bytes default,
79,420/79,648 Stage3F TX/RX,79,928/80,052 Stage3G TX/RX and80,492 RX+trace.
Clean committed spans are50,768 default,77,048 Stage3F RX,77,332 Stage3G TX
and77,896 RX+trace. For example, the STM32 vector-to-text alignment adds56
bytes not counted by the size utility's text+data sum. LPS default/continuous
and Stage 3F section sizes are unchanged.
The new DW runtime fits the existing 128-byte mode-exclusive scratch; frame
storage is 46 bytes including FCS reserve. No heap or new persistent RX queue.
STM32 keeps a <=128-byte diagnostic state plus small error/poll counters;
tracing adds 24 bytes of static RAM in the measured RX image. Disabled STM32
builds link no diagnostic/timing state. The workspace/committed 352-byte RAM
and code-size differences belong to pre-existing user streaming/control edits.

Compiler `-fstack-usage` estimates: LPS diagnostic event frame 88 bytes;
core encode24/decode64, plus their callees and existing printf/driver/task stack.
STM32 process frame272 TX,280 RX,288 RX+trace; timing report216, plus libc/HAL/
SPI/interrupt callers. These are individual frames, not total stack high-water
or safety proofs. STM32 linker minimum stack reservation remains1024 bytes;
no physical high-water measurement was performed. New core/adapters compile
without new warnings; retained LPS HAL/FreeRTOS/newlib warnings and STM32 vendor
unused functions/maybe-uninitialized `chan_ctrl` and existing motor-format
truncation warnings remain outside this extraction.

## UART coexistence assessment and instrumentation

At the initial extraction checkpoint, main-loop order remained USB, UWB, ESP,
e-puck without changes to motor or sensor policy. The follow-up below adds an
explicit UWB-only safety policy and quieter normal telemetry. Diagnostic EXTI0 remains disabled, so no radio ISR
can run lengthy processing. One status pass per host millisecond reduces polling
load versus an unrestricted loop; it does not guarantee one-millisecond latency
under load. RX is rearmed before serialization/logging. Normal frame-wait
50000 UUS (~51.28 ms) timeouts are expected at 1 Hz TX; error counters are not
packet-loss counts. Bad/error status wins over simultaneous CRC-good.

Existing USART1/USART3 service priorities are both6; e-puck ring1024 bytes with
64-byte task budget, USB ring1024 and output16 x192-byte bounded messages.
USB overflow increments/reports dropped messages; it does not block radio/UART
or count lost RF/sensor frames. Other existing bounded ESP queues are unchanged.
The new diagnostic adds no queue with hidden overflow semantics.

The actual open risk remains Qorvo's global PRIMASK sections around blocking
SPI at1.3125 MHz (both nominal slow/fast settings), including HAL_MAX_DELAY and
SPI-ready spinning. A44-byte body alone is ~268 us, versus86.8 us per115200-8N1
UART character. This is a plausible overrun mechanism, not proof of the six
Stage 3F UART faults. SPI speed, masking policy, interrupt priority and DMA remain unchanged.
The follow-up reduces optional application traffic without rewriting the driver.

Enable STM32 `MUL_TIMING_TRACE=ON` only with `MUL_DIAG_ROLE=1/2`:

```text
MULG TIME hz=168000000 spi_off_cycles=N service_cycles=N gap_cycles=N calls=N
```

Actual `hz` is logged from SystemCoreClock. Convert cycles with `cycles/hz`;
at168 MHz an86.8 us character is about14583 cycles. Maxima are cumulative after
radio initialization (startup/calibration excluded). Nested outer Qorvo mutex
windows are measured; nesting/reset/uint32 cycle wrap are host-tested. Service
includes its bounded logging; start gaps expose cooperative scheduling delays.
A TIME line reflects previously completed service passes; the current pass is
included on the next report. DWT32 wraps ~25.6 s at168 MHz; interpret differences
only for intervals shorter than a wrap. Entry/exit instrumentation overhead and
unmeasured IRQ-off sites mean this is sampled instrumentation, not a hard WCET
bound or all-system interrupt-latency trace. User on-device maxima are below.

## Hardware results reviewed 2026-10-20

All seven complete local files were inspected at `/home/derder/Documents/`.
These are user-operated experiments; Codex did not flash or operate hardware.
Captures contain startup fragments, omitted text and unequal logging windows.
An INIT prefix alone does not prove a physical restart. Do not sum counters
across epochs or divide unmatched TX/RX totals to obtain a delivery percentage.

| Run | TX evidence | RX evidence at final visible endpoint | Previous-TX markers compared |
| --- | --- | --- | --- |
| A: DW1000 → DWM3000 | 55 printed DONE records; final boot completion counter89; main contiguous seq36–88 | 43 printed unique seq37–86; final STAT valid80, gaps1, old77, invalid1, dup0, err373, to2281 | 43/43 exact matches |
| B: DWM3000 → DW1000 | 66 consecutive DONE seq27–92; final boot completion counter93 | 64 printed seq26–91; final STAT valid90, gaps2, old0, dup0, invalid0, err59, to2557 | 62/62 exact matches; two references outside TX capture |
| C: concurrent DW1000 TX → DWM3000 RX | 87 printed DONE: seq0/1 then488–572; final boot completion counter573 | Final STAT through572: valid541, gaps6, dup0, old0, invalid0, err608, to10007; RX573 follows, hence **at least542 valid** | 84/84 exact matches; five references outside TX capture |

A's received frames are classified OLD: first retained STAT is valid38/old35,
last80/77, both gaps1. Its sequence baseline is inconsistent with the visible
run; the gap counter is not a reliable loss estimate for that run. Missing
printed sequences are not demonstrated radio losses. B's two in-span missing
printed sequences46/52 agree with its cumulative gaps2, but unequal endpoints
still preclude a synchronized delivery ratio. C's absent TX text2–487 and RX
text30–487 are capture holes, not hundreds of demonstrated losses. Its console
reports2131 dropped messages. Packet metadata comparisons use `prev_seq` to
match the *previous* completed transmission, never independent TX/RX clocks.
No comparable marker differs; full ten-hex-digit/40-bit precision is retained.

A's last CRC/PHR-RS subtype counters are1/12; C's are0/24. Aggregate err includes
other radio conditions and cannot be interpreted as CRC loss. Frame-wait `to`
is expected during approximately1Hz transmissions and is not packet loss.
A/B establish shared-core exchange in both directions, but89/93 completion
endpoints fall short of the guide's >=100-completion gate. Repeat intact runs.

### UART evidence and disabled baseline

C shows an ESP session invalidation near RX529: `uart_fault`, `bits=0x8`,
UART error count4, `rx_ovf=0`, `tx_full=0`, and fresh heartbeat age9ms. Installed
STM32F4 HAL defines0x8 as **ORE: hardware UART overrun**. This is not a software
RX queue overflow or heartbeat timeout. RX528/529/530 continue and gaps stays6
through573 (45 consecutive529–573); this particular visible fault did not
produce a recorded sequence gap. Earlier missing history prevents broader
causal conclusions. E-puck snapshots change errors26→27, drops121→125,
data_bad24→25 and data2958→3040; most cumulative faults predate that window.
Streaming was deliberately1000ms. Sample ages742–1744ms are observations, not
a latency bound; commanded step counters do not establish physical geometry.

DWT maxima at168MHz: A SPI IRQ-off49965cycles (297.41us), service104693
(623.17us), start gap211664 (1259.90us); C49980 (297.50us),107650
(640.77us),214368 (1276.00us), respectively. IRQ masking exceeds the86.8us
115200-8N1 character interval and is a plausible overrun mechanism. Other
PRIMASK sites and USB/UART scheduling also exist. Aggregate maxima do not
correlate the exact fault with a transfer; SPI causation is **not established**.
No masking/speed/priority/driver change is justified solely by these captures.

The user confirms `G-C-baseline.log` was captured with `MUL_DIAG_ROLE=0`,
`PHY_INTEROP_ROLE=0`, `APP_UWB_ENABLED=0`. It has two separate counter epochs:

| Disabled session | Visible EPUCK snapshots | Counter endpoints | UART/drop/parser errors |
| --- | --- | --- | --- |
| 1000ms, lines1–46 | 14 | tx/pong736→762; rx440406→443968; data4185→4211 | All zero |
| 100ms after reset, lines48–309 | 88 | tx/pong9→219; rx7930→229440; data74→2152 | All zero |

Two ESP losses are `control_hb_timeout` at500ms with UART0/bits0, distinct from
C's ORE. Client intent/timing is not available. The short1Hz baseline and longer
C run are not matched experiments; zero baseline errors strengthen the
coexistence concern but do not isolate its cause. Do not add the two sessions.

Capture SHA-256 provenance (raw logs are kept outside the repositories):

```text
6859aa9d58455a2f15ca123847438afbbc72c5844482e9f2ae713e1939415757 G-A-tx.log
e0bc5b72a87a0e32759d7b194eb90f008c2645e49f20497441b42d25e227c048 G-A-rx.log
ca2ac3e47b1ed45777cd8b3c49711bc2c3452ed9c9941880aaf0bb4b6e124a5b G-B-tx.log
57a68344c3e66511a076745b74edc9eb0c97385889986c50fa1b7d4c31450adf G-B-rx.log
c77d62abca96c38b323b99e61c109e4ef7d722129ec34a2d6998c89d911ab0b3 G-C-tx.log
81047eb9465b3257da23c21c318a6631c2035c53bbf7bf86f396037a57b56c4d G-C-rx.log
164a5bb4fa53dba7793984a2bbe73e8dd02c92e74968abc0c2cd74d76a48e619 G-C-baseline.log
```

## Follow-up software policy

Reproducible software checkpoints: STM32
`53485dba7f03a31e4d0bfaa855b7f121c5197b91`, ESP32/client
`43375732a6f2d26567a14e897e6be9c34373e416`. The canonical portable core remains
pinned to `1b807c5c7507d67d802ee976ae4dfe5b27fd88e9`.


`MUL_UWB_ONLY=ON` explicitly disables ESP/Wi-Fi and remote motor commands,
keeps the e-puck safety link enabled, forces sensor streaming0 and debug
telemetry0. Startup queues/transmits STOP before radio initialization. STOP
acknowledgement/retry, watchdogs and error handling remain active. ARM, movement
and stream-changing commands are rejected from every command source; STOP,
HELP, MOTOR_STATUS, STREAM_STATUS and LINK_STATUS remain available. Fresh PONG
requests STREAM0; an unacknowledged idempotent stop-stream request can retry
after5s with a new fence. Startup STOP must be acknowledged for acceptance;
software cannot guarantee a powered/disconnected peer physically stopped.

Normal mode remains available (`MUL_UWB_ONLY=OFF`). Previously local streaming
work and the ESP client/UI/tools are now included in the published changes.
Default acquisition is1000ms; Wi-Fi sends the latest accepted sample at most
once per1000ms even if acquisition is faster. Compact `DATA2` has16 numeric
fields: sequence, age_ms, epuck_time_ms, prox[8], acc[3], steps[2]; it omits
lights. Raw dsPIC DATA parsing remains unchanged. Updated ESP bridge/client
accept DATA2 and legacy DATA; **normal Wi-Fi use needs the updated ESP bridge**.
The dsPIC firmware does not change. Safety heartbeats/deadlines are not throttled.
Routine USB sensor arrays and motor HB ACKs are quiet by default; errors remain
and LINK_STATUS gives concise counters. Optional APP_DEBUG_TELEMETRY=1 restores
verbose normal-mode diagnostics. MUL RX metadata is now on the RX line on both
platforms, reducing console messages without changing packet bytes/timestamps.
The portable-core pin, PHY, frame formats and timestamp arithmetic are unchanged.

Current STM32 Release resource measurements (text/data/bss; RAM includes linker
reservations, not physical high-water):

| Build | text | data | bss | static RAM |
| --- | ---: | ---: | ---: | ---: |
| UwbOnlyRX, trace | 76124 | 732 | 16288 | 17020 |
| UwbOnlyTX | 75604 | 732 | 16264 | 16996 |
| UwbOffSafe | 49088 | 708 | 15828 | 16536 |
| CoexistRX, trace | 79292 | 736 | 27056 | 27792 |
| NormalBaseline | 52156 | 712 | 26600 | 27312 |

A clean committed STM32 export independently passes policy/console/ESP tests
and builds UwbOnlyRX with the same17020-byte RAM footprint (text76120;
workspace76124 includes a longer absolute vendor assertion filename). A clean
ESP export also passes transport/client/UI tests.

Software checks pass:15 console and9 ESP scenarios, the UWB-only boot STOP /
ACK-loss retry / ARM/movement rejection / fault diagnostics test, compact
telemetry1Hz coalescing and tick wrap, core/adapter/activation/timing suites,
nine interleaved coexistence cases and retained Stage3F tests. ESP C transport
and21 Python client/UI tests pass, including PTY input, legacy/compact parsing
and light omission. All five STM32 profiles and both retained Stage3F roles build; ESP-IDF6.1 fresh build
succeeds (app0xc6b10 bytes). Mocks do not establish physical UART deadlines.
No new physical validation is claimed.

## Next user experiments — UWB first

### Exact builds and ELF selection

Run in `/home/derder/Documents/epuck-stm32` (no flash occurs):

```sh
cmake -S . -B build/UwbOnlyRX -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release -DPHY_INTEROP_ROLE=0 -DMUL_DIAG_ROLE=2 -DMUL_UWB_ONLY=ON -DMUL_TIMING_TRACE=ON
cmake --build build/UwbOnlyRX
cmake -S . -B build/UwbOnlyTX -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release -DPHY_INTEROP_ROLE=0 -DMUL_DIAG_ROLE=1 -DMUL_UWB_ONLY=ON -DMUL_TIMING_TRACE=OFF
cmake --build build/UwbOnlyTX
cmake -S . -B build/UwbOffSafe -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release -DPHY_INTEROP_ROLE=0 -DMUL_DIAG_ROLE=0 -DMUL_UWB_ONLY=ON -DMUL_TIMING_TRACE=OFF
cmake --build build/UwbOffSafe
cmake -S . -B build/CoexistRX -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release -DPHY_INTEROP_ROLE=0 -DMUL_DIAG_ROLE=2 -DMUL_UWB_ONLY=OFF -DMUL_TIMING_TRACE=ON
cmake --build build/CoexistRX
cmake -S . -B build/NormalBaseline -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release -DPHY_INTEROP_ROLE=0 -DMUL_DIAG_ROLE=0 -DMUL_UWB_ONLY=OFF -DMUL_TIMING_TRACE=OFF
cmake --build build/NormalBaseline
```

APP_UWB_ENABLED must remain0 in app_config.h for both role0 baselines.
STM32CubeProgrammer ELF paths (select only the intended profile):

```text
/home/derder/Documents/epuck-stm32/build/UwbOnlyRX/EPuck_STM32.elf
/home/derder/Documents/epuck-stm32/build/UwbOnlyTX/EPuck_STM32.elf
/home/derder/Documents/epuck-stm32/build/UwbOffSafe/EPuck_STM32.elf
/home/derder/Documents/epuck-stm32/build/CoexistRX/EPuck_STM32.elf
/home/derder/Documents/epuck-stm32/build/NormalBaseline/EPuck_STM32.elf
```

**User-only flashing:** connect the existing ST-LINK/SWD in CubeProgrammer,
open the selected ELF, download with verification, then reset. Equivalent CLI
for the first test (not executed by Codex):

```sh
/home/derder/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI -c port=SWD -w /home/derder/Documents/epuck-stm32/build/UwbOnlyRX/EPuck_STM32.elf -v -rst
```

For DW1000 TX, run in `/home/derder/Documents/lps-node-firmware`:

```sh
make clean && make EXTRA_CFLAGS="-DPHY_INTEROP_ROLE=0 -DMUL_DIAG_ROLE=1"
# User-only existing DFU procedure:
sudo make dfu
```

Reverse role to2 for DW1000 RX when STM32 uses UwbOnlyTX. Flag changes require
clean LPS builds. Normal recovery: `make clean && make`, then user DFU; existing
EEPROM-selected modes are preserved. No EEPROM reset or dsPIC flash is needed.

### 1. Sustained UWB-only

Keep chassis secured, wheels clear, motors disarmed. Start receiver capture
before TX reset; capture complete unedited logs on both nodes for **at least
10 minutes and >=500 completed TX** per direction. Example separate terminals:

```sh
picocom -b 115200 --logfile G-UWB-only-rx.log "$STM32_PORT"
picocom -b 115200 --logfile G-UWB-only-tx.log "$LPS_PORT"
```

Use UwbOnlyRX first, then reverse roles with distinct files. Require INIT
roles/profile and `MULG POLICY uwb_only=1 esp=0 stream_ms=0`. After peer startup,
verify STOP acknowledged/disarmed, STREAM_STATUS acknowledged0 and no Wi-Fi
session. Query LINK_STATUS at start/end and after any error. Do not ARM or send
nonzero speeds. Match all comparable prev_seq/prev_tx against DONE, retaining
full40-bit wrap. Report counter endpoints, logging drops, UART subtypes,
TIME maxima and all gaps/errors/timeouts; missing console lines are not RF loss.
No repeat init failures, TX watchdog failures, stuck RX/rearm, marker mismatches,
unsafe motor state or unexplained UART errors are acceptable. Capture faults
without hiding them; measured gaps must be reported, not tuned away blindly.

### 2. Matched UWB-disabled baselines

Next use UwbOffSafe for the same duration/geometry/power and LINK_STATUS queries;
there should be no MULG traffic, ESP session or nonessential DATA streaming.
Keep this baseline separate from the earlier streaming-enabled baseline.
If coexistence is investigated, also collect NormalBaseline with ESP on and
STREAM1000, followed by CoexistRX with identical conditions and duration.
Run the updated client with the actual ESP address:

```sh
cd /home/derder/Documents/epuck-esp32
python3 -m venv .venv
.venv/bin/python -m pip install -r tools/requirements.txt
.venv/bin/python tools/wifi_client.py "$ESP32_IP" --plain --logs --verbose
```

Normal DATA2 operation first requires the compatible ESP build:

```sh
source /home/derder/.espressif/tools/activate_idf_v6.1.sh
idf.py -B build/UwbReview build
# User-only, actual ESP USB port:
idf.py -B build/UwbReview -p "$ESP32_PORT" flash
```

Do not flash ESP for the UWB-only test; it is disabled there. Normal-mode
zero-speed ARM/heartbeat/STOP checks require secured wheels and an operator;
require actual ACKs and disarmed state after STOP. Client CTRL+C sends best-effort
STOP. For an intentional heartbeat-loss test, pause only the identified client
with SIGSTOP, resume after >500ms and require STOP/disarm plus fresh explicit
ARM for recovery. Never infer a safety pass from continued UWB reception alone.

### Gate decision

Stage3G remains **hardware pending**; Stage3H must not begin. Existing captures
prove bidirectional serialization/previous-TX exchange, not robust timing under
load or mixed-radio ranging accuracy. First establish sustained UWB-only exchange
and safety, then compare disabled baselines and assess remaining UART impact.
Normal Wi-Fi coexistence may remain a documented secondary limitation if the
user accepts that scope; it cannot be declared fixed from software tests.
Return complete logs, exact repository HEADs/build flags/core pin, endpoints,
geometry/power/stream period and any intentional resets/disconnections.

## Subsequent UWB-only audit and ADS foundation

The newer UWB-only logs reproduce the retained-baseline OLD behavior; this does
not imply stale RF packets or zero loss. Explicit ordering/idle-epoch diagnostics
and the separate ADS protocol are documented in
[the UWB-only ranging guide](mul_ads_twr.md). Stage3G reverse-direction sustained
hardware validation remains pending. Use that guide for the new experiments;
the earlier A/B/C evidence and procedures above remain historical context.
