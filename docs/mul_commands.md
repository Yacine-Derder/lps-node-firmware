# Unified UWB commands — checkpoint D contract

Status: **planned interface; implementation and verification pending**. This matrix
is written before the D refactor. Checkpoint C is software tested: core0cb81e3,
LPSd63a29d/962be5c/4010ddc, STM32550b069 and ESPd1e6b8f. Current commands are
in docs/mul_heterogeneous.md and STM docs/runtime_uwb.md. No hardware validation
of custom rosters or moving metric TDoA is implied.

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
|mode other|Original anchor/tag/sniffer/tdoa2/tdoa3; unsupported values rejected|Existing twr/beacon/listen/hybrid_static; static remains stationary-only|
|role ref / follower|Persist explicit role, retaining participants when swapping|Unsupported legacy TDMA transmitter role; reject|
|role rlr|First stop RF, select passive role/algorithm; restart required before on|Select passive hybrid with RF off when leaving a transmitting mode|
|addr id|Persist address with role retained|Existing address transaction|
|reference id / follower id|Set explicit ordered roster; own endpoint follows addr|Set explicit roster, independent of local ID/ADS role|
|peer list|Show the two mandatory roster participants/roles|Existing bounded multi-peer list|
|peer add id / peer del id|Reject ambiguous arbitrary roster edits; use reference/follower|Existing add/remove, with del as remove alias|
|cal show|Report build antenna/power policy and unvalidated calibration|Existing ADS correction and antenna settings|
|cal on / off|Unsupported runtime calibration switch; reject|Existing additive ADS calibration flag; never passive TDoA|
|wifi on / off|Unsupported hardware; reject|Existing optional Wi-Fi telemetry setting|
|tlm on / off|Unsupported framed telemetry switch; legacy text remains|Existing telemetry enable/disable, preserving interval|
|diag|MUL counters without changing radio/model state|Existing hybrid/peer/link diagnostics as applicable|
|diag reset|Require RF off; clear counters, retaining protocol identities/models|Existing idle-only reset; reject active exchange|
|save|Already autosaved; no redundant write|Existing autosave compatibility alias; no redundant write|

Both platforms autosave successful configuration mutations and avoid redundant
writes. No role/mode/address operation resets antenna or calibration values.
DW1000 settings retain their existing restart-to-activate behavior; STM32 applies
through its existing stopped-radio/preflight/journal/resume transaction. These
activation differences must be visible, rather than a misleading common success
message. Failed or unreadable persistence is reported as unconfirmed; it does not
prove the requested bytes were never committed. Existing damaged EEPROM is retained.

RF on/off means UWB operation, not hardware power or robot control. Off preserves
the selected mode, role, ID and calibration. On uses the selected, validated mode;
no selection means an error, not an inferred transmitter. A pending DW1000 restart
blocks on so it cannot re-enable an old ALR role after selecting rlr. Runtime RF
changes occur in the existing radio owner, with late events ignored while stopped.
Volatile clock/association state is reacquired after resume; no stale model becomes
valid merely because RF was enabled.

Role rlr must never start a transmit path. It selects the passive family with RF
stopped before leaving an incompatible transmitting mode. Existing normal-mode
ADS initiator/responder settings remain separately configurable through cfg set
role; canonical ref/follower/rlr names are protocol roles. Mode selection alone
must not silently enable RF. Ordinary hybrid remains NO_RATE; hybrid_static is
an explicit stationary baseline. Neither this interface nor CFO makes moving
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

Help and diagnostic responses must use bounded output/paging and nonblocking USB
backpressure. No queue/buffer/stack growth or long radio lock is justified by help.
Critical errors/safety service and radio processing continue. Optional FTDI UART
uses its existing stdout path; USB guarantees must not be represented as measured
UART timing. Hardware command responsiveness and RF stop/resume need an operator
retest; the assistant does not flash or operate devices.

## Persistence and verification plan

DW1000 adds an RF-enable TLV without renumbering existing fields. Schema4 on STM32
adds RF enable to a reserved packed-node byte, retaining128-byte journal geometry;
v1/v2/v3 load without migration writes and infer prior enabled state from mode.
Legacy cfg set mode keeps its old apply/start behavior; canonical mode changes
selection while preserving RF state. Unsupported/reserved encodings fail closed.

Test both parsers through actual serial framing. Cover old aliases, case/hex,
unknown syntax/capabilities, read-only UWB-only commands, no mutation on errors,
calibration/peer retention, schema migration/power cuts, on/off with selected mode
retained, pending-restart protection, late IRQ/timeouts, and role rlr from an
active transmitter. Exercise fresh passive reconstruction after resume and prove
no RLR TX. Run stock/mixed timing builds, STM profiles, host replay/transport and
safety regressions. Commit tested D changes separately and preserve unrelated work.
