# Stage 3F: opt-in PHY packet experiment

Status: CP0 feasibility and CP1 diagnostic contract software-defined; hardware
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
Remaining unknowns: actual RF reception, board RF/clock/OTP tuning, TX power,
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
