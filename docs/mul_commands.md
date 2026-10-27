# Unified UWB commands — checkpoint D contract

Status: **DW1000 and STM32 D implemented/software tested; hardware retest pending**.
STM32 reference:7769a30682ce8f0d90b31245b496cc657ea8c61c. DW1000 keeps modes0..6,
its EEPROM TLV layout, TDMA/TWR and native40 timestamps; no shared-core changes.
The ESP bridge vocabulary and UWB wire formats are unchanged. Prior A/C validation
does not establish this new command/RF lifecycle on physical hardware.

## Common command contract

All new UWB commands are lowercase, with case-insensitive parsing. IDs accept
decimal or0x hex. Unsupported capabilities produce an explicit error and no
mutation. Read-only/invalid commands never fall through into robot control.
No command grants motor movement/arming in the STM32 UWB-only build.

|Command|DW1000 LPS|STM32/DWM3000|
|---|---|---|
|help|Concise UWB reference; original character help retained|Existing paged help/topics plus new aliases|
|show|Saved configuration and pending restart|Current/saved configuration through existing transaction|
|status|Boot-selected mode/role/ID and requested/owner RF state|Selected mode/ID, requested RF and actual runtime state|
|on / off|Persist RF enable and request change from radio owner; on rejects pending restart|Persist/apply RF enable through existing safe transaction|
|mode hybrid|Select custom family; persisted protocol role chooses ALR or passive RLR algorithm|Select passive hybrid; ordinary NO_RATE retained|
|mode other|Original anchor/tag/sniffer/tdoa2/tdoa3; static requires opt-in image|Existing twr/beacon/listen/hybrid_static; static remains stationary-only|
|role ref / follower|Persist explicit role, retaining participants when swapping; mode hybrid selects family|Unsupported legacy TDMA transmitter role; reject|
|role rlr|First stop RF, select passive role/algorithm; restart required before on|Select passive hybrid with RF off when leaving a transmitting mode|
|addr id|Persist address with role retained|Existing address transaction|
|reference id / follower id|Set explicit ordered roster; own endpoint follows addr|Set explicit roster, independent of local ID/ADS role|
|peer list|Show the two mandatory roster participants/roles|Existing bounded multi-peer list|
|peer add id / peer del id|Reject ambiguous arbitrary roster edits; use reference/follower|Existing add/remove, with del as remove alias|
|cal show|Report build antenna/power policy and unvalidated calibration|Existing ADS correction and antenna settings|
|cal on / off|Unsupported runtime calibration switch; reject|Existing additive ADS calibration flag; never passive TDoA|
|wifi on / off|Unsupported hardware; reject|Existing optional Wi-Fi telemetry setting|
|tlm on / off|Unsupported framed telemetry switch; legacy text remains|Existing telemetry enable/disable, preserving interval|
|diag|Owner radio-event counters without changing radio/model state|Existing hybrid/peer/link diagnostics as applicable|
|diag reset|Require RF off acknowledged; clear owner event counters only, retain protocol state|Existing idle-only reset; reject active exchange|
|save|Already autosaved; no redundant write|Existing autosave compatibility alias; no redundant write|

DW1000 also accepts role standalone (disabled/unassigned): leaving an explicit
RLR stops RF and clears its mode selection to the existing idle sentinel255;
choose a mode and restart before on. Existing :mul role disabled retains its
legacy behavior. IDs accept0..255 for local addresses; ALR participants must be
distinct0..254. Converting an ALR to an RLR requires a roster that excludes its
local ID first (for example :mul setup disabled ref_id follower_id); commands
never guess replacement ALRs. Invalid syntax/rosters are checked before RF stops.

Both platforms autosave successful configuration mutations and avoid redundant
writes. No role/mode/address operation resets antenna or calibration values.
DW1000 settings retain their existing restart-to-activate behavior; STM32 applies
through its existing stopped-radio/preflight/journal/resume transaction. These
activation differences must be visible, rather than a misleading common success
message. Failed or unreadable persistence is reported as unconfirmed; it does not
prove the requested bytes were never committed. Unreadable or magic-preserving damaged EEPROM is retained; the pre-existing
invalid-magic factory/reset path remains unchanged.

RF on/off means UWB operation, not hardware power or robot control. Off preserves
the selected mode, role, ID and calibration. On uses the selected, validated mode;
no selection means an error, not an inferred transmitter. A pending DW1000 restart
blocks on so it cannot re-enable an old ALR role after selecting rlr. Runtime RF
changes occur in the existing radio owner, with late events ignored while stopped.
The DW1000 console reports requests, not synchronous hardware acknowledgement.
Status reports requested/running separately. Only the UWB task idles/clears the
DW1000 and initializes the boot-selected algorithm on resume. TX/RX/timeout/failure
callbacks are suppressed once off is requested, including late semaphore events.
Explicit passive-role records cannot enable an override transmitter image; status
shows all build override flags, rather than treating the saved mode as that engine.
Volatile clock/association state is reacquired after resume; no stale model becomes
valid merely because RF was enabled.

Role rlr must never start a transmit path. It selects the passive family with RF
stopped before leaving an incompatible transmitting mode. Existing normal-mode
ADS initiator/responder settings remain separately configurable through cfg set
role; canonical ref/follower/rlr names are protocol roles. Mode selection alone
must not silently enable RF. Ordinary hybrid remains NO_RATE; hybrid_static is
an explicit stationary baseline. On DW1000 this is a build-only policy:
MUL_EXPERIMENTAL_STATIONARY_RLR_RATE=1 makes all mode6 operation stationary-only;
mode hybrid_static is accepted there, while an RLR mode hybrid request is rejected
to avoid presenting that image as ordinary moving hybrid. Normal images reject
hybrid_static, and ordinary mode6 remains NO_RATE. Neither this interface nor CFO makes moving
metric TDoA observable or produces a unique two-anchor2D position.

## Framing and backward compatibility

DW1000 retains its immediate single-character menu. A leading colon introduces
one complete line (`:show`, `:role ref`); `:console` enters a line-console session
where ordinary lines such as `help`, `show` and `off` work. `legacy` returns to the
original menu. Connection-generation changes clear partial commands/session state.
The existing :mul commands remain aliases. This explicit entry avoids interpreting
the first letter of role/mode/help as an immediate legacy command. Commands require
newline, with bounded length and complete discard on invalid/oversized input.
USB OUT backpressure now preserves complete64-byte packets without a larger queue.

STM32 already accepts newline-delimited commands; cfg, peer, tlm, hybrid, sync,
help and their case-insensitive aliases remain. Legacy robot safety commands keep
their original case and safety dispatcher. A complete read-only show/status/help
must be consumed by the UWB parser; split lines remain separate commands. No
joining of malformed fragments or weakening of safety gates is permitted.
The ESP bridge forwards existing command payloads; no new transport vocabulary or
wire format is needed. UWB1/2/3 and custom-roster replay remain compatible.

DW1000 command replies/help/diagnostics reuse the existing48-byte input buffer,
with one response line per main-loop service. Configuration commands retain the existing
bounded EEPROM/I2C page waits in the main task; radio service remains preemptive.
There is no wait for USB output or lock held across EEPROM I/O. CDC_TryWrite reserves64 TX bytes
for other output and either enqueues the complete <=48-byte line or nothing;
input pauses under backpressure while LED/button/radio service continues. USB
connection-generation changes cancel replies, partial input and line-console mode
before that pause, preventing a disconnected session from blocking recovery.
Existing legacy printf output retains its historical best-effort behavior. No
queue/buffer/stack growth or long radio lock was added for help.
Critical errors/safety service and radio processing continue. Optional FTDI UART
uses its existing stdout path; USB guarantees must not be represented as measured
UART timing. Hardware command responsiveness and RF stop/resume need an operator
retest; the assistant does not flash or operate devices.

## Persistence and verified software behavior

DW1000 adds optional TLV10 (one boolean byte, RF enable) and role value3 in the
existing three-byte MUL network TLV9 (explicit passive RLR). Missing RF TLV retains
legacy enabled boot behavior; a legacy disabled/passive role2 in mode6 is read as
RLR without writing EEPROM. Stock mode IDs and other field IDs/values remain
unchanged. Invalid stored MUL rosters/roles also block RF in stock and override
images, preventing a malformed passive-role record from falling back to a transmitter.
Invalid required mode/address fields and invalid RF encodings do not
start RF. Factory/default writes must verify successfully before RF is admitted.
Legacy EEPROM remains an additive-checksum page image, **not a power-cut journal**.
A failed off save leaves the live radio off but can leave the old durable on value;
inspect settings after reboot. Failed/readback-uncertain writes block subsequent
on requests until reboot/recovery. Capacity/size failures never overwrite fields.
Configuration edits latch restart-required, including edits through legacy menus
and LPP; RF-only changes do not. All typed writers use the same bounded readback
check. A short critical section rejects concurrent console/LPP writers; no I2C,
EEPROM wait or radio lock is held inside that section. Successful no-ops avoid wear.

STM32 schema4 adds RF enable/protocol role within the128-byte journal; old records
load without migration writes. Legacy cfg set mode retains auto-start semantics;
canonical mode retains RF state. Unsupported/reserved encodings fail closed.

Validation for this DW1000 checkpoint:
- The full native Unity/CMock suite passes217 tests. The20 focused configuration/
  lifecycle/actual USB tests also pass ASan/UBSan. Tests use actual TLV code, parser, USB callbacks and lifecycle;
  hardware/SPI scheduling are doubles, not a physical timing measurement.
- Tests cover legacy roles/modes/aliases, retained power/roster, disabled/re-enabled
  RF across reboot, failed and committed-but-unreadable off writes, restart guards,
  owner-only SPI, fresh algorithm initialization, late-event suppression, concurrent
  LPP-style writes, invalid syntax/roles, session reset, fragmented/bulk packets,
  bounded atomic output and non-mutating errors. Existing real-adapter TDMA/TWR,
  passive RLR, timestamp/wrap and stationary/NO_RATE tests remain in the full suite.
- Clean firmware builds cover default channel2, CH5 at500/50/10ms, stationary CH5,
  supported diagnostic TX/RX profiles and optional FTDI. Initialized data load
  addresses are checked as well as text/RAM; both linker scripts now reject an
  image whose initialized data would exceed flash. Only non-radio main/console/
  configuration/control/wrapper objects use-Os; radio/protocol optimization is unchanged.
  Explicit override builds retain the menu names/IDs without linking unreachable
  normal engines; normal builds retain all seven modes.

Reproduce a normal build/test (no flashing):

```sh
rake unit summary
make clean
make -j4
```

Each additional profile was built from clean objects with `make -j4 EXTRA_CFLAGS='…'`:

|Profile|EXTRA_CFLAGS|
|---|---|
|CH5,500ms|-DMUL_HYBRID_CH5=1|
|CH5,50ms|-DMUL_HYBRID_CH5=1 -DMUL_FOLLOWER_SLOT_MS=50|
|CH5,10ms|-DMUL_HYBRID_CH5=1 -DMUL_FOLLOWER_SLOT_MS=10|
|Stationary CH5|-DMUL_HYBRID_CH5=1 -DMUL_EXPERIMENTAL_STATIONARY_RLR_RATE=1|
|PHY TX/RX|-DPHY_INTEROP_ROLE=1 / =2 (separate builds)|
|Shared diagnostic TX/RX|-DMUL_DIAG_ROLE=1 / =2 (separate builds)|
|ADS TX|-DMUL_TWR_ROLE=1 -DMUL_TWR_SESSION=1 (compile test only)|
|ADS RX|-DMUL_TWR_ROLE=2|
|FTDI|-DUSE_FTDI_UART|
|Longer range|-DLPS_LONGER_RANGE|

The13 profiles use REV=B/BOOTLOAD=0. The final default CH2 ELF/DFU is at
bin/lps-node-firmware.elf and bin/lps-node-firmware.dfu. Named build artifacts
bin/mul-d-<profile>.elf/.dfu are ignored build outputs, not committed firmware.
PLAN.md records final memory limits. No physical calibration, RF silence or
new moving-RLR metric validity is inferred from these builds.

Operator retest (not performed here): back up settings and use the appropriate
normal/CH5 image; enter :console, run show/status/help, off, wait for running=0,
then on and confirm fresh ALR acquisition/RLR observations. Verify RF silence after
off, including a pending delayed TX, and safe resume at each spacing. Exercise
oversized/split commands and disconnect during paged help, then reconnect and
confirm the legacy menu still works. Change an address/role, verify on is blocked
until restart, and verify role rlr never transmits. On a stationary opt-in image,
use hybrid_static only while stationary. EEPROM failure/power-loss behavior needs
physical fault testing; do not infer RF silence, responsiveness, accuracy or UART/
PHY fault resolution from host tests. Diagnostic ADS initiator images require a
fresh nonzero MUL_TWR_SESSION for each physical run, as before.

## Persistent reference-to-follower delay

The line console accepts `:cfg show` and `:cfg set slot_ms 10` (also50/500).
This delay is independent of the hardware-timed1000ms reference cycle and the
650..800ms inter-ALR TWR window. Change all participating nodes explicitly; no
received packet writes EEPROM. Scheduled broadcasts advertise the selected delay
in the packet-type high bits, so mismatches fail closed with `slot_mismatch`.

On each ALR, enter `:off`, then poll `:status` until RF requested=0/running=0.
Only then send `:cfg set slot_ms 10`, inspect `:cfg show`, and use `:on`.
Slot changes do not require restart/reflash: the radio owner applies the saved
value while stopped and reinitializes scheduling, synchronization and TWR on ON.
Other configuration edits retain their existing restart requirements. Unsupported
values and changes while RF is running or stopping are rejected before EEPROM IO.
A failed EEPROM write/readback keeps RF off and requires inspection/reboot.

Fresh factory configurations select10ms. Old EEPROMs without the additive slot
TLV retain500ms without writes; explicitly set10ms when upgrading the experiment.
Previously hand-built50/10ms images had no persistent record of that choice and
must also be explicitly configured after upgrade. No address/roster/calibration
or existing EEPROM field IDs are changed.

Build the mixed-radio ALR firmware with:

```sh
make clean
make -j4 REV=B BOOTLOAD=0 EXTRA_CFLAGS='-DMUL_HYBRID_CH5=1 -DMUL_PROP_MAX_AGE_MS=6000'
```

Artifacts: `bin/lps-node-firmware.elf` and `.dfu`. Update all ALRs and the STM32
receiver before enabling the advertised schedule; older firmware rejects these
new broadcast types. The ESP32 UWB3 envelope remains fixed length. Software tests
cover real delayed-TX register programming at10/50/500ms, mismatch skips,
owner-only safe transitions, EEPROM persistence/malformed fields, and unchanged
one-second reference cadence. Physical timing/accuracy still need operator checks.

### Three-robot10ms deployment and hardware acceptance

Build once per radio platform; slot changes afterwards need only configuration.
For a named mixed-radio ALR artifact, run the CH5 build above, then:

```sh
cp bin/lps-node-firmware.elf bin/runtime-slot-ch5.elf
cp bin/lps-node-firmware.dfu bin/runtime-slot-ch5.dfu
```

On STM32:

```sh
cd ~/Documents/epuck-stm32
cmake -S . -B build/Universal -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake \
  -DCMAKE_BUILD_TYPE=Release -DMUL_UNIVERSAL=ON -DMUL_UWB_ONLY=ON \
  -DMUL_TWR_ROLE=0 -DMUL_DIAG_ROLE=0 -DPHY_INTEROP_ROLE=0 \
  -DMUL_TIMING_TRACE=ON -DMUL_ADS_TRACE_TIMES=ON
cmake --build build/Universal
python3 tests/runtime_config/check_elf.py build/Universal/EPuck_STM32.elf
```

Operator-only flashing, with one intended ST-LINK target connected at a time:

```sh
# Repeat for reference132 and follower133. External EEPROM is retained.
cd ~/Documents/lps-node-firmware
openocd -f interface/stlink.cfg -f target/stm32f0x.cfg \
  -c 'program bin/runtime-slot-ch5.elf verify reset exit'
# Then connect the STM32 RLR target. Retain its configuration sectors; no mass erase.
~/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI \
  -c port=SWD -d ~/Documents/epuck-stm32/build/Universal/EPuck_STM32.elf -v -rst
```

The ESP32 radio-independent bridge already supporting UWB3 TDoA needs no new
flash for runtime scheduling. Its shared-core snapshot/host tools were updated
and its firmware rebuilt to verify compatibility. If the older telemetry-delivery
fix is not installed, use the ESP32 flashing instructions in
`../epuck-esp32/docs/uwb_logging.md` relative to the workspace repositories.

1. Keep all three radios OFF while configuring. On each ALR enter `:off`, wait
   for `:status` to show requested=0/running=0, then `:cfg set slot_ms 10`.
   Inspect `:cfg show`; retain addresses0x84/0x85, reference/follower roles and
   configured roster. On STM32 enter `off`, `cfg set slot_ms 10`, then `show`.
   Retain0x8d, RLR, hybrid_static, enabled peers0x84/0x85 and Wi-Fi/TLM ON.
2. Power-cycle each node and verify slot_ms10 remains. Repeat50/500 and restore10
   to verify runtime persistence without rebuilding. Use `:on` on ALRs and `on`
   on STM32 only after the receiver is stationary. Wait for fresh clock/TWR
   acquisition. Ordinary hybrid remains NO_RATE by design.
3. Save ALR USB output. Pair each follower `REF_RX rx=` with its completed
   `TX role=FOLLOWER actual=`. Both timestamps belong to the follower: signed
   modular40-bit `actual - rx` should be638976000..638976511DTU at10ms
   (alignment rounding only; aborted/missing TX is not a timing sample).
   Separately check consecutive reference completed-TX timestamps differ by
   63897600000DTU per sequence step, about1second. Never subtract timestamps
   from different radios directly. Look for clock acquisition and successful
   inter-ALR TWR, with no new late-slot failures.
4. Capture Wi-Fi for ten minutes, then repeat raw:

   ```sh
   cd ~/Documents/epuck-esp32
   timeout --signal=INT 600s python3 tools/uwb_collect.py --robot 192.168.1.141:3333 --no-live
   timeout --signal=INT 600s python3 tools/uwb_collect.py --robot 192.168.1.141:3333 --raw --no-live
   ```

   Logs remain under `~/Documents/uwb_logs`. Compact roster rows must report
   slot_ms10; warm-up/unavailable TDoA must retain status/null distances, and
   available observations must have signed numeric dd_mm with ref/fol progressing
   about1Hz. Raw records retain the advertised frame types and native RX stamps.
   Replay a raw capture using `python3 tools/uwb_hybrid.py RAW.jsonl --slot 10
   --output REPLAY.jsonl` (replace file paths; output must be new).
5. Check `hybrid status` slot_mismatch=0 and separate `tlm status` USB/Wi-Fi/UART
   counters, plus bridge counters. Unconsumed USB drops can grow independently.
   Wi-Fi should drain at nominal traffic; reconnect the collector and verify
   delivery resumes. Repeat USB disconnected. Physical variation/accuracy still
   requires measured geometry; AVAILABLE alone is not an accuracy certificate.
6. With RF safely stopped, change only the follower to50ms while reference/RLR
   stay10ms, then enable it: expect slot_mismatch and no follower scheduled TX.
   Restore10 with RF stopped. Separately set only RLR to50 while ALRs stay10:
   expect slot_mismatch, no accepted TDoA, and no reused rate. Restore10, reacquire,
   and confirm normal delivery. Unsupported20ms must reject without changing
   saved configuration. Do not change slots mid-flight or while moving the RLR.

Native driver tests validate programming arithmetic, not actual RF timing. No
hardware was flashed, moved or measured during implementation. LPS remains very
close to its128KB flash/16KB RAM limits; bounded passive codec/association code
uses size optimization without reducing stacks/queues or changing radio math.


## Persistent multi-ALR schedules

Mode5 supports an ordered roster of2..8 canonical8-bit ALR IDs. First ID is
reference; local identity derives the follower slot. With RF OFF acknowledged:

```
:off
:status
:cfg set slot_ms 10
:cfg roster set 0x84 0x85 0x86
:cfg set cycle_ms 1000
:cfg set schedule_revision 1
:cfg roster show
:on
```

Only enable after every participant has the identical roster/timing/revision.
Roster show includes profile, count, period, revision, fingerprint and every
slot offset. Three through eight IDs select multi profile2; two IDs select the
legacy profile1. `:cfg set profile multi` explicitly permits the new protocol with
two IDs. `:cfg set profile legacy` requires exactly two IDs. No reboot is needed
for stopped-radio schedule changes, but address/mode edits retain their existing
restart requirement. `:cfg set slot_ms` accepts10/50/500, subject to the data-window
guard;500ms cannot accommodate more than two ALRs. Period is currently1000ms.
`cfg` commands above also work inside the existing console session without `:`.

An explicit roster owns role/endpoints. Old `reference`, `follower`, ALR role
aliases and `:mul setup` are rejected until using roster commands instead. Address
changes must select an ID already in the explicit roster. Standalone/passive role
selection retains its existing stop/restart semantics. LPS mode6 cannot receive
profile2; the STM32 RLR supplies that implementation. Diagnostic override builds
cannot activate a multi schedule.

The multi deployment image must use `MUL_PROP_MAX_AGE_MS=6000` on ALR and receiver,
so the round-robin synchronization schedule can refresh all seven followers.
See [the architecture](mul_multi_alr.md) and [manual deployment/handoff](mul_multi_handoff.md).

## Experimental numerical timing (software-tested, hardware-unverified)

Compatibility profiles1/2 retain the timing restrictions above. Opt-in profile3
uses schedule codec2 and multi RF envelope3: all participating ALRs and STM32 must
run matching new revisions. Version/fingerprint mismatches are rejected.
The following are preparation commands for a later authorized experiment; no radio
operation is required for software verification. With RF OFF, apply the same ordered
roster, timing budgets and revision to each participant:

```text
off
cfg roster set 0x84 0x85 0x86 0x87
cfg set profile experimental
cfg set tx_lead_us 1000
cfg set rx_budget_us 500
cfg set airtime_us 1000
cfg set guard_us 100
cfg set slot_ms 3
cfg set cycle_ms 1000
cfg set schedule_revision 1
cfg roster show
```

LPS console accepts the same commands prefixed by `:`. Node identities/roles must
already be correct; STM32 selects `mode hybrid_static` only for a stationary-rate
experiment and retains an RLR ID outside the roster. The3ms example explicitly
lowers conservative lead/processing budgets; it is not a hardware capability claim.
Actual delayed-TX lead guards remain before and after SPI.2ms requires still lower
explicit budgets (for example lead500/rx500/air400/guard100us) and is experimental.
LPS experimental RF activation requires the known fast PHY, normally the CH5 build;
long-preamble/low-rate PHY settings are rejected. Stock modes remain available.

`slot_ms` accepts numerical1..65535, subject to lead+processing+airtime+guard and
cycle feasibility. `cycle_ms` accepts100..2000 in10ms steps. ALR–ALR TWR currently
retains its compatibility650/800ms windows; `cfg set alr_alr_twr off` allocates none
and permits shorter cycles. Without TWR or a future external propagation provider,
fresh propagation relations are unavailable: disabling AA is not yet a complete
geometry-backed localization mode. Automatic scheduling and active ALR–RLR TWR
remain pending. Cycle changes update the STM32 estimator's period atomically.

Returning to compatibility requires restoring `slot_ms 10` and `cycle_ms 1000`
before `profile multi` (or `profile legacy` with exactly two ALRs). Roster edits
preserve experimental profile3. All timing edits require RF OFF. STM32 schema8
journal migration preserves implicit/explicit clock settings and retains rollback
under interrupted writes; LPS retains the original100-byte checksum image and its
existing fail-closed recovery semantics.

Optional research diagnostics: `cfg set clock_estimator regression`,
`cfg set clock_window 24`, `cfg set samples on`, `cfg set telemetry on` on STM32;
`clock show` reports current state. The default estimator remains median. Clock
telemetry type10 v2 includes raw40-bit timestamps, actual period and capture age;
v1 remains readable with unknown capture age. Routine logs preserve both.

## MUL-only production image

Current `make` builds only the MUL production image (mode5 ALR, mode6 passive RLR).
The original combined firmware remains at Git revision `fd44002`, with its original
mode implementations and configuration history. The current target intentionally
rejects `MUL_ONLY=0`; native stock-mode regression tests still build their original
API without the production define. No upstream mode sources were deleted.

The production image excludes original tag/anchor TWR, sniffer, TDoA2/TDoA3,
LPP handlers, pressure-sensor startup and factory-test commands. It retains the
DW1000 driver, calibrated antenna timing, MUL five-message TWR, reference/follower
roles, passive RLR, diagnostics, EEPROM recovery, USB console, PHY and power controls.
Old EEPROM bytes and mode IDs are not rewritten at boot. Stored0..4 or invalid
modes remain RF OFF; select an appropriate MUL role/mode explicitly. `:mode
anchor/tag/sniffer/tdoa2/tdoa3` and the old single-character aliases cannot activate
removed engines. The `m` menu lists only5/6. New uninitialized storage defaults idle.

```sh
make clean
make -j4 REV=B BOOTLOAD=0 EXTRA_CFLAGS='-DMUL_HYBRID_CH5=1 -DMUL_PROP_MAX_AGE_MS=6000'
python3 tools/test/check_mul_image.py bin/lps-node-firmware.elf
python3 tools/test/mul_slots.py --mul-only --sanitize --prop-age 6000
```

These commands build/test only. The checker verifies that removed engines and the
TDoA3 overlay are absent, the explicit344-byte MUL clock workspace exists, required
MUL engines remain, stack/USB queue allocations meet their documented sizes, and flash/RAM fit.
It does not prove physical timing or dynamic stack safety. No image is flashed by
these commands. `--mul-only` runs only MUL fixtures under the production define;
without it the test helper also runs useful stock-source regressions. Test outputs
are cleaned between profiles to prevent stale summary results.

The final CH5 cleanup image uses117392 flash bytes and12828 RAM bytes including
the2048-byte linker stack, leaving13680 flash and3556 RAM. Compared with the prior
combined CH5 image, savings are12904 flash and3376 RAM bytes. Both builds use the
same compiler/optimization flags; LTO was not needed. The core UWB/main/idle task
stacks and USB queue sizes were not cut. No mode-specific task existed to remove;
all retained tasks serve MUL, console or the kernel. Timers were already disabled.

The EEPROM driver now checks device bounds and stops at the first failed page;
a later successful page cannot turn an incomplete write into reported success.
Its8KiB/32-byte-page bound follows the part identified in Bitcraze's
[Rev-D schematic](https://www.bitcraze.io/documentation/hardware/loco_node/loco_node_revd.pdf)
and Microchip's [24AA64 datasheet](https://ww1.microchip.com/downloads/aemDocuments/documents/MPD/ProductDocuments/DataSheets/24AA64-24FC64-24LC64-64-Kbit-I2C-Serial-EEPROM-DS20001189.pdf).
This is board-design evidence, not inspection of each robot's fitted device.
The legacy configuration allocation remains100 bytes. Generated plans use the
separate two-slot journal described below; this assumes that same fitted EEPROM.

## Experimental five-message TWR reply timing

Profile3 adds RF-OFF-only `cfg set twr_reply_ms 20|10|5`. Default remains20.
The reply must cover configured TX lead, processing, airtime and guard while AA
ranging is enabled.5ms therefore requires explicitly lower budgets; for example,
lead1000/rx500/air1000/guard100us passes the software check. These remain unmeasured
hardware budgets. Both before- and after-SPI delayed-TX lead checks are retained.
The actual five-message adapter uses the selected interval, derives its step
failure deadline and final-packet guard from the explicit experimental budgets,
and validates received timing using the same interval. Legacy APIs/profiles retain
20ms replies,5ms lead,50ms step timeout and their original timing acceptance.

The32-byte schedule stays codec2, with flags bit0=AA and bits1..2 reply selector
(0=20ms,1=10ms,2=5ms,3 invalid). Other bits remain rejected. Flags participate in
the fingerprint; older decoders reject the newly assigned bits, and differently
configured participants reject each other's frames. Update and configure all ALRs
and the passive receiver consistently. Roster preview reports `reply` in ms.

Unselected profiles still use the two original650/800ms maintenance windows.
Selected generated plans replace those windows as described below. Software tests
cover complete20/10/5ms exchanges with±150ppm clocks, precise markers, after-SPI
failure, timeout and recovery; they do not establish hardware reliability/accuracy.


## Generated ranging plans (LPS AA integration)

Plans are opt-in and remain separate from legacy settings. With RF OFF
acknowledged, `:plan init` creates an unselected plan using the current ALR roster
and timing, converted to experimental timing if necessary. Ranging starts disabled.
`plan show` displays the stored generation, selection, complete-plan fingerprint,
rosters, timing, rates, budget and cycle1 reservations. Its pages are bounded USB
messages; this is a configuration preview, not observed radio performance.

Commands use the same leading colon as the existing console:

- `:plan use on|off` persists selection; `off` restores the retained legacy profile.
- `:plan alrs ID ID ...` sets2..8 ordered ALRs; first is reference.
- `:plan rlrs [ID ...]` sets0..8 distinct RLRs outside the ALR roster.
- `:plan set aa|ar on|off` independently enables reservation classes.
- `:plan set aa_millihz|ar_millihz N` sets per-pair targets0..10000 milliHz.
- `:plan set slot_ms|cycle_ms|lead_us|process_us|air_us|guard_us N` sets explicit
  timing. Guard is100..10000us in100us increments; existing profile3 limits apply.
- `:plan set reply_ms 5|10|20`, `:plan set util_percent N` (10..80), and
  `:plan set revision N` configure the remaining plan fields.

All edits require RF OFF acknowledged and a valid complete candidate. Set smaller
budgets before selecting a short reply. An infeasible target is rejected before
writing; targets do not claim achieved rates. While selected, legacy roster/timing
aliases and address-menu changes are rejected; clock estimator settings remain
editable. Deselect before editing the retained legacy configuration.

The LPS reference services generated AA operations once each per cycle and only
when the assigned follower was heard. Followers accept only assigned reservations.
Lost/late operations are skipped; later reservations can recover without bursts.
Mandatory broadcasts retain priority. Fixed650/800ms windows do not run alongside
this owner. Selected AR plans enable assigned ALR initiation and STM32 RLR
responses. STM32 RF ON requires the local node in the plan RLR roster. LPS plans require mode5,
roster membership and the known fast PHY. Diagnostics cannot override them.

**Compatibility:** generated-plan RF envelope4 is deliberately distinct from
profile3's RF3 and legacy frames. Matching STM32 schema9 now persists/selects
independent plans and receives RF4, with assigned AR RESPONSE/REPORT transmissions.
ESP/collector supports planned vector/raw11/12 and AR range/quality13/14,
quality4, full-plan fingerprint and explicit profile4.
All participants must have the same full plan, including RLR roster/rates. Old
decoders reject the new types. This path is software-tested; physical timing and
interoperability remain unverified. Unselected profiles retain their protocols.
No hardware has been operated here.

The64-byte plan codec2 embeds the32-byte broadcast codec2, RLR roster, AA/AR
rates, enabled classes, reply and utilization. The broadcast's legacy ranging flags
must be zero. Planner2 reserves an additional1000us before TX setup for the
1kHz FreeRTOS service quantum; the old host-only planner1 is rejected. Reservation
length includes setup, four replies, final airtime, processing and guard. Next
reference preparation retains20ms. Extra callback delays can still skip a slot;
software budgeting is not a measured deadline guarantee.

The journal has two96-byte records at EEPROM offsets128/256, leaving0..99 intact.
Record version1 stores selection, generation32, plan, CRC and commit-last marker.
Inactive-slot invalidation, body write, final commit and exact readback preserve
the prior record across interrupted writes. A lost final ACK may have committed;
failed writes block activation until reboot/recovery. Boot reads/cache publication
occur while RF is stopped; UWB callbacks never perform journal I2C. Reset requires
RF OFF, invalidates legacy magic first, then both journal commits. Interrupted
reset after magic invalidation boots idle defaults rather than an old selected
owner; a failed first invalidation leaves the prior RF-OFF configuration.

The main task now has1600 bytes of stack, increased from800 after reviewing the
journal/console call chain. UWB1600, idle800, linker2048 and USB queue512 remain.
Global float-printf linking was removed; production half-dB power display uses
integer formatting, and the optional ADS diagnostic retains float formatting.
Current image measurements and remaining stack-watermark checks are recorded in
[the engineering handoff](mul_upgrade_handoff.md).


AR diagnostics use STM32 `hybrid ar`; `hybrid status` reports passive=0 only when
a selected AR plan is enabled. `samples on` opts into native RX/TX tracing;
range results remain essential even with samples off. Type13 retains six mixed
clock timestamps, raw/corrected ToF in initiator DTU, power status, cycle/pair/op,
plan identity, acquisition uptime and separate report uptime. Physical uncertainty
is UNKNOWN and these ranges are not calibrated. Type14 carries responder
completion/fault/drop/reset counters; reference_seen is not a freshness guarantee.
Idle diagnostic reset preserves protocol identities and retired operations; busy
reset is refused. Ordinary hybrid remains NO_RATE; AA-only/default RLR is passive.


While RF is OFF, both LPS (`:` prefix) and STM32 support:

```text
plan set policy freshness
plan set aa_priority 200
plan set ar_priority 1
```

Larger class priority breaks equal planned deadlines first. Targets still set
requested per-pair rates; impossible coincident deadline capacity is rejected.
`plan set policy fair` restores the original codec2 schedule and zeros priorities.
Freshness uses codec3; all participants must share that policy, priorities and
plan fingerprint. Saved journal sizes and existing fair defaults are unchanged.
Plan preview reports actual codec, policy and priorities; Python preview accepts
`scheduling_policy`, `aa_priority`, `ar_priority`. Freshness describes planned
reservations, not a guarantee that an exchange succeeds or its data stays fresh.


### Abstract pose observations (software-only interface)

The persisted source defaults to TWR. With RF OFF acknowledged, select one of:

```text
:geometry source external
:geometry source none
:geometry source twr
```

External input uses `:pose begin CLOCK EPOCH FRAME MAX_SKEW_MS` and
`:pose put HEX`, where HEX is exactly160 digits encoding the portable80-byte
version1 record including CRC. `:pose show` reports source/count/availability,
distance/age and covariance availability with physical bound UNKNOWN. Begin
clears old observations; re-begin after source epoch/frame/roster changes. There
are8 bounded identities. Disconnect clears volatile poses and partial commands.
No pose record writes EEPROM or changes RF/motor ownership. Source selection
changes propagation correction, not the plan's AA enable/rate.

Pose fields and strict bounds are in portable/mul_core/README.md. Source clock,
epoch and known frame must agree. Local reception uptime is separate from source
time. `ageUpperMs` must conservatively include observation age, transit and
alignment uncertainty at local acceptance;65535 means UNKNOWN and disables
correction. TTL1..6000 is additionally capped by the build propagation policy.
Nominal source timestamp skew is checked against0..1000ms policy. Supplied
covariance is statistical; it never establishes a hard physical clock bound.

Host files can be generated and replayed without devices:

```sh
python3 tools/mul_pose_inputs.py generate --input truth_input.jsonl \
  --truth truth_eval.jsonl --noisy noisy.jsonl --commands pose_commands.txt \
  --seed 123 --sigma-mm 50 50 50 --age-upper-ms 10
python3 tools/mul_pose_inputs.py replay --input noisy.jsonl \
  --output geometry.jsonl --reference 132 --follower 133
```

Input truth rows contain exactly node/source_us/x_mm/y_mm/z_mm. All output paths
must differ from input and each other, including aliases. Noise uses configured
Gaussian covariance before1mm quantization (component rounding bound0.5mm).
`--rho` optionally adds per-sample AR(1) correlation; different robots retain
independent streams. The default age is UNKNOWN; the example10ms is an explicit
SIMULATION acceptance-age assumption, not a measured serial-link bound. Command
files are reviewable artifacts and never sent to hardware by these tools. A live
source would have to recompute/bound actual acceptance age and respect pacing.
No ROS subscriptions, MCS connection or Raspberry Pi software is provided.

Cross-platform host verification, using loopback sockets only:

```sh
# LPS checkout:
MUL_POSE_RADIO_TRACE=/tmp/pose.frames python3 tools/test/mul_slots.py \
  --mul-only --prop-age 6000 --slot 10 --sanitize
# STM32 checkout, after the LPS test finishes:
MUL_POSE_FRAMES=/tmp/pose.frames MUL_POSE_TLM_TRACE=/tmp/pose.uart \
  bash tests/runtime_config/run.sh
# ESP32 checkout:
python3 tools/verify_multi_replay.py --geometry /tmp/pose.uart
```

This passes native LPS delayed-TX frames through the real STM32 runtime/radio
adapter and ESP bridge/collector. Synthetic independent clocks, external geometry,
source none and stale inputs are checked; it is not a physical accuracy test.

The optional measured-input filter executes on ESP/host and remains disabled by
default; it adds no ROS, MCS, noise generator or EKF math to LPS. See the sibling
[EKF interface and replay](../../epuck-esp32/docs/uwb_ekf.md) and
[consolidated experiment preparation](mul_experiment_campaign.md). The existing
DATA2 commanded/phase counters are not a measured-wheel backend.
