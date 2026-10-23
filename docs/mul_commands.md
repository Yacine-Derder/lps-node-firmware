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
