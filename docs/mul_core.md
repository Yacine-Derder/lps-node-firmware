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

| Source / API | Responsibility |
|---|---|
| `portable/mul_core/mul_common.h/.c` | Preserved legacy codec, identities, sequence state, 40-bit encode/decode/mask/signed difference |
| `portable/mul_core/mul_core.h/.c` | `mulCoreEncodeDiag`, `mulCoreDecodeDiag`, `mulCoreSourceValid`, reset/observe sequence, caller-owned `mulCoreObserve` observation |
| `inc/mul_diag_dw.h`, `src/mul_diag_dw.c` | DW1000 diagnostic state, hardware capture, immediate TX, RX rearm and USB task logging |
| `src/phy_interop_dw.c` | Unchanged common channel-5 PHY setup; Stage 3F codec/path retained |
| STM32 `Core/Src/mul_diag.c` | Corresponding bounded polling adapter; no radio ISR or queue added |
| STM32 `Core/Src/uwb_diag_profile.c` | One shared Stage 3F/3G configuration using existing Qorvo SPI/driver |
| STM32 `Core/Src/mul_timing.c`, `qorvo_irq.c` | Optional cycle measurement only; interrupt ownership/speed unchanged |

A standalone remote/submodule was considered and rejected as unnecessary
administration for this small extraction. STM32 vendors an immutable snapshot
from a published LPS commit, with `UPSTREAM.json` and every file's SHA-256. Its
configure AND build verify hashes offline and reject modified/extra files.
No sibling checkout is required to build either firmware. Only canonical LPS
files may be edited; regenerate/review/commit the consumer snapshot explicitly.
There is no implicit fetch/update or package manager.

Compatible implementation checkpoints (both published):

- LPS / canonical core: `1b807c5c7507d67d802ee976ae4dfe5b27fd88e9`.
- STM32 consumer: `1bd3ecdba534549b4c9ac89f17fd8bd7270fe056`.
- STM32 `UPSTREAM.json` pins the LPS checkpoint above. Later documentation commits
  retain that exact dependency. Record both final firmware HEADs with each capture.

Fresh clone recovery: clone LPS with `--recurse-submodules` (existing driver/test
submodules), or run `git submodule update --init --recursive` in its checkout.
STM32's committed snapshot is sufficient; no new submodule or local unpublished
path is needed. The consumer's standalone tests work without the LPS checkout.

Host prerequisites remain the existing ARM GCC/Make and CMake/Ninja toolchains,
Ruby/Rake for LPS tests and a native C compiler. Python 3 is required for STM32
offline hash verification; the explicit LPS export script requires Python 3.9+.
No Python packages or network access are needed for that verification.

## Validation and resource evidence

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

| Build | Text / data / bss bytes | Static RAM including linker reservations |
|---|---|---:|
| LPS default |123024 /1072 /15292|16364 (20 bytes spare)|
| LPS Stage 3E continuous |123096 /1072 /15292|16364|
| LPS Stage 3F TX / RX |123992 or124120 /1080 /15292|16372 (12 spare)|
| LPS Stage 3G TX / RX |124136 or124352 /1080 /15292|16372 (12 spare)|
| STM32 workspace default |52600 /712 /27856|28568|
| STM32 workspace Stage 3F TX / RX |78628 or78852 /736 /28256|28992|
| STM32 workspace Stage 3G TX / RX |79136 or79260 /736 /28288|29024|
| STM32 workspace Stage 3G RX + trace |79700 /736 /28312|29048|
| STM32 clean committed default |50000 /712 /27504|28216|
| STM32 clean committed Stage 3F RX |76256 /736 /27904|28640|
| STM32 clean committed Stage 3G TX |76536 /736 /27936|28672|
| STM32 clean committed Stage 3G RX + trace |77100 /736 /27960|28696|

Flash is text+data. LPS default/continuous and Stage 3F sizes are unchanged.
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

Main-loop order remains USB, UWB, ESP, e-puck; no motor, ownership, UART, safety
or sensor source was changed. Diagnostic EXTI0 remains disabled, so no radio ISR
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
Stage 3F UART faults. No SPI speed, masking policy, interrupt priority, DMA,
protocol or safety logic is changed to optimize delivery.

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
bound or all-system interrupt-latency trace. No on-device cycle results exist yet.

## User hardware experiments (not performed by Codex)

Use one Bitcraze DW1000 and one powered e-puck STM32/DWM3000 extension, working
USB links, existing ST-Link SWD, UART wiring, dsPIC bridge and ESP32 firmware.
Start stationary, modest fixed LOS separation (~1 m), unchanged orientation.
Record geometry, board identities, exact Git HEADs, core pin, build flags, raw
logs and any local diff. Do not compare delivery across different capture windows.
Discover actual serial devices with `ls -l /dev/serial/by-id/`; set `LPS_PORT`
and `STM32_PORT` to the corresponding actual paths. Do not run multiple readers
on one port. Receiver capture starts before TX capture and ends after it.

A reproducibility distinction matters: this task preserves ten uncommitted
STM32 streaming/control files. A fresh committed clone retains 1 Hz R polling;
the user's working tree has100 ms automatic STREAM support. Builds of both
were tested, but those user changes are not included in Stage 3G commits.
`STREAM`/`STREAM_STATUS` commands below apply to that preserved streaming tree;
for a clean checkout use its existing periodic R polling and record that lower
load. Do not claim a fresh checkout reproduces the10Hz coexistence experiment
without separately preserving/reviewing that user patch. Record it with:

```sh
cd /home/derder/Documents/epuck-stm32
git rev-parse HEAD
cat ThirdParty/mul_core/UPSTREAM.json
git diff --binary > /tmp/stm32-experiment-local.patch
sha256sum /tmp/stm32-experiment-local.patch
```

### A — DW1000 TX, DWM3000 RX

Build/flash the receiver first. Flash commands below are operator instructions;
Codex did not execute them. Use the existing documented ST-Link setup:

```sh
cd /home/derder/Documents/epuck-stm32
cmake -S . -B build/MulRX -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release -DPHY_INTEROP_ROLE=0 -DMUL_DIAG_ROLE=2 -DMUL_TIMING_TRACE=ON
cmake --build build/MulRX
source /home/derder/.espressif/tools/activate_idf_v6.1.sh
openocd -s "$OPENOCD_SCRIPTS" -f interface/stlink.cfg -f target/stm32f4x.cfg -c 'program build/MulRX/EPuck_STM32.elf verify reset exit'
```

Then DW1000 TX; enter its existing Bitcraze DFU mode before the flash command:

```sh
cd /home/derder/Documents/lps-node-firmware
make clean && make EXTRA_CFLAGS="-DPHY_INTEROP_ROLE=0 -DMUL_DIAG_ROLE=1"
sudo make dfu
```

In separate terminals, receiver monitor first, TX monitor second:

```sh
picocom -b 115200 --logfile G-A-rx.log "$STM32_PORT"
picocom -b 115200 --logfile G-A-tx.log --exit-after 120000 "$LPS_PORT"
```

Collect >=100 matching completed transmissions over >=120 s; allow extra time
for enumeration/startup. Do not reset mid-run, splice files or repeat startup
prefixes. If a reset is needed, start a separate labeled run. Exit RX afterward
with Ctrl+A, Ctrl+X. Keep whole raw files, not terminal excerpts.

### B — DWM3000 TX, DW1000 RX

Close monitors, reverse roles, rebuild and flash using the same SWD/DFU setup:

```sh
cd /home/derder/Documents/lps-node-firmware
make clean && make EXTRA_CFLAGS="-DPHY_INTEROP_ROLE=0 -DMUL_DIAG_ROLE=2"
sudo make dfu
cd /home/derder/Documents/epuck-stm32
cmake -S . -B build/MulTX -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release -DPHY_INTEROP_ROLE=0 -DMUL_DIAG_ROLE=1 -DMUL_TIMING_TRACE=OFF
cmake --build build/MulTX
source /home/derder/.espressif/tools/activate_idf_v6.1.sh
openocd -s "$OPENOCD_SCRIPTS" -f interface/stlink.cfg -f target/stm32f4x.cfg -c 'program build/MulTX/EPuck_STM32.elf verify reset exit'
```

Separate terminals, same minimum duration/count:

```sh
picocom -b 115200 --logfile G-B-rx.log "$LPS_PORT"
picocom -b 115200 --logfile G-B-tx.log --exit-after 120000 "$STM32_PORT"
```

### Expected output and A/B acceptance

```text
MULG INIT radio=DW1000 role=TX src=84 core=1 type=70 version=1 bytes=44 bits=40 unit=DTU ch=5
MULG TX src=84 seq=N attempt=N
MULG DONE src=84 seq=N done=N tx=0123456789 bits=40 unit=DTU
MULG RX src=84 seq=N valid=1 rx=9876543210 gaps=N bits=40 unit=DTU
MULG META seq=N prev_valid=1 prev_seq=P prev_tx=0123456789
MULG STAT tx=N done=N valid=N gaps=N dup=N old=N invalid=N err=N to=N
```

Values are illustrative. B swaps source84/86; LPS RX includes previous metadata
on its RX line, STM32 emits a separate META line with the same current sequence.
INIT validates build settings; CRC-good and exact strict core decode precede RX.
Match `prev_seq=P` and `prev_tx` to transmitter DONE for P, not current packet N.
First unavailable marker is expected. Verify full ten-hex-digit precision and
wrap across a run; do not subtract `rx` from `prev_tx`. Never infer calibrated
antenna timing from matching serialized hardware markers.

Accept A/B when both directions have valid frames, >=100 completed TX assessed
in intact overlapping windows, full source/version/type/length validation and
matching previous-TX metadata wherever both logs contain the referenced DONE.
Report attempts/completions/valid/unique/gaps/dup/old/invalid/errors/timeouts and
logging drops with separate window endpoints. No arbitrary PDR threshold is
introduced. Explain or retain any measured asymmetry. The Stage 3F analyzer is
for PHY-prefixed reset-free logs and must not be used unchanged for MULG logs.
Failure: init failure, no valid reception, invalid payload/source/version,
marker mismatch, truncated timestamps, stuck rearm, repeated watchdog failures
or unavailable counts due to incomplete capture. Repeat an intact run rather
than treating missing console text as RF loss.

### C — Concurrent sensors, ESP communication and safety

First collect >=120 s of the same sensor/ESP traffic with UWB disabled:

```sh
cd /home/derder/Documents/epuck-stm32
cmake -S . -B build/MulBaseline -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release -DPHY_INTEROP_ROLE=0 -DMUL_DIAG_ROLE=0 -DMUL_TIMING_TRACE=OFF
cmake --build build/MulBaseline
source /home/derder/.espressif/tools/activate_idf_v6.1.sh
openocd -s "$OPENOCD_SCRIPTS" -f interface/stlink.cfg -f target/stm32f4x.cfg -c 'program build/MulBaseline/EPuck_STM32.elf verify reset exit'
picocom -b 115200 --logfile G-C-baseline.log "$STM32_PORT"
```

Confirm APP_UWB_ENABLED remains0 for this baseline. Keep existing EPUCK/ESP
switches enabled and motors disarmed. On the preserved streaming firmware,
enter `STREAM 100`, `STREAM_STATUS`, `MOTOR_STATUS`, `HELP` in the USB console;
wait for actual OK STREAM and fresh DATA. On a clean clone record1Hz polling.
Run the existing ESP client in another terminal using the real discovered IP:

```sh
cd /home/derder/Documents/epuck-esp32
python3 tools/wifi_client.py "$ESP32_IP" --plain --logs --verbose
```

Wait for SESSION READY; issue HELP/MOTOR_STATUS and confirm continued sensor
telemetry/ESP responses. This uses the existing client/protocol unchanged.
Then close monitors, flash MulRX again with the exact A receiver command above,
run A's transmitter, and collect >=120 s to `G-C-rx.log` plus matching TX and
client output. Use the same stream period/load/geometry and record baseline
counter endpoints separately; do not subtract counters across resets.

Initially test STOP and status while disarmed. To test ARM/heartbeat without
motion, secure/lift the chassis with wheels clear, keep an operator ready to
remove power, and use only one control owner. Through the existing Wi-Fi client,
explicitly ARM, wait for actual OK ARM, command `M 0 0`, wait for OK M, observe
verbose OK HB/status for >=1 s, then STOP and require actual OK STOP/disarmed.
Never send a nonzero speed. Verify a competing USB ARM is rejected while Wi-Fi
owns control; USB STOP must still work. For heartbeat-loss behavior on a secured
zero-speed robot, pause the identified client process long enough to stop CONTROL_HB, while
leaving the TCP connection open. With the secured zero-speed robot only, use
`kill -STOP "$CLIENT_PID"`, wait >500 ms, then `kill -CONT "$CLIENT_PID"` with
the actual identified client PID. Verify heartbeat-expiry/ownership loss and
STOP acknowledgement; require a fresh explicit ARM after recovery. A normal
Ctrl+C client exit sends best-effort STOP and does not isolate heartbeat expiry.
Restore to STOP/disarmed immediately. These are user-only hardware tests.

C acceptance: valid MUL RX continues; sensors remain fresh at the configured
period; UART errors/dropped/flushed bytes/parser rejects, USB/ESP queue loss,
missed HB/ACKs and ownership/STOP failures are measured against the disabled
baseline, not hidden. No unexplained added UART faults, stale stream, lost STOP
or missed safety deadline is acceptable for combined-control acceptance. Record
TIME maxima and UART error subtypes/IRQ-off traces if available. Existing
aggregate errors alone cannot identify overrun versus framing/noise. If faults
recur, stop/disarm and retain logs; Stage 3G remains hardware-pending until the
cause/impact is resolved or explicitly reviewed with the user. A/B reception
alone does not close the concurrent-operation gate.

### Recovery

Close serial readers. Restore DW1000 with `make clean && make`, then user DFU;
this returns to EEPROM-selected normal modes/PHY. Restore STM32 using the
MulBaseline commands above, or the repository's normal Debug preset with both
diagnostic roles0 and tracing OFF; flash that exact ELF via the existing SWD
command. To repeat Stage 3F, use its existing guide with MUL_DIAG_ROLE=0.
No EEPROM reset, ESP32/dsPIC reflash or central project-document edit is needed.

Send all A/B/C/baseline logs, build flags/HEADs/core pin/local patch fingerprint,
geometry, metadata comparisons and timing/UART counter endpoints. Software is
ready; full Stage 3G acceptance and Stage 3H remain gated on those user results.
