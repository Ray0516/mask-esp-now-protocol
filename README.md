# Mask ESP-NOW protocol v1

Current seven-field telemetry package: [mask-esp-now-protocol-v1.3.zip](mask-esp-now-protocol-v1.3.zip).

This repository contains the wire contract and two Arduino ESP32 examples for a mask node and a manager. The manager firmware in the main workspace is the TFT/Wi-Fi manager application; it is intentionally not copied into this package.

## What is implemented

- Binary ESP-NOW frames with an 18-byte header and extensible TLV payload.
- HELLO → ACCEPT/REJECT pairing, with device ID, boot ID, sequence echo, assigned slot, manager ID, channel, and maximum frame size.
- Periodic telemetry carrying exactly seven product fields: light, activity level, CO2, TVOC, device name, temperature/humidity score, and worn state. The manager screen uses only the light value.
- `mask_node_sender.ino`: transmitter example. Set its device ID, name, manager MAC, and channel before flashing. The code sends HELLO until accepted and then sends telemetry.
- `manager_receiver.ino`: standalone receiver. Send `p` in Serial Monitor to open pairing for 60 seconds; accepted nodes are held in a 12-slot RAM roster and telemetry is printed to Serial. For the TFT manager, use the manager application already in the parent workspace; it persists pairing and displays device names and light states.

The phone app can remove a saved mask pairing over the manager's Bluetooth Classic SPP connection. The node is only accepted again while the manager's physical pairing window is open.

The protocol has no authentication or encryption in v1. Pair only in a controlled environment. OTA/FOTA is not implemented in this package; protocol IDs are reserved for a later authenticated update design.

## Hardware and setup

See [`docs/wiring.md`](docs/wiring.md) for the ESP32-CAM ↔ TFT and button pin-by-pin wiring, boot cautions, and SD-card conflict.

Use ESP32 boards with ESP-NOW support. Both endpoints must be on the same 2.4 GHz Wi-Fi channel. The manager uses its active AP/router channel; set `MANAGER_CHANNEL` on the node to that channel. Put the manager's station MAC in `MANAGER_MAC` (shown in the manager serial log or router/ESP tools). Open the manager pairing window, then boot the node. The node is added to the first free roster position, up to 12.

For each mask, assign a unique nonzero `DEVICE_ID` and a readable ASCII `DEVICE_NAME` (1–23 bytes). Replace the example sensor values in `readTelemetry()` with actual sensor reads. Values use `255` / `0xFFFF` when unavailable.

## Build

Download and extract `mask-esp-now-protocol.zip` to preserve the sketch folders and shared header. Open either sketch folder's `.ino` in Arduino IDE with an ESP32 Arduino core installed, select the target ESP32 board, edit the configuration constants, and upload. The examples use the Arduino ESP32 core 2.x callback signature; on core 3.x update the ESP-NOW receive callback to use `esp_now_recv_info_t` and read the source MAC from `info->src_addr`.

## Frame contract

All multi-byte values are little-endian. Header: `magic:u16=0x534D`, `version:u8=1`, `type:u8`, `payloadLength:u16`, `deviceId:u32`, `bootId:u32`, `sequence:u32`. Payload is zero or more `[type:u8, length:u8, value:length]` TLVs. Maximum complete frame is 250 bytes. Unknown TLVs are skipped.

| Type | ID | Fields |
|---|---:|---|
| HELLO | `0x01` | name (`0x01`), capabilities u32 (`0x20`) |
| TELEMETRY | `0x02` | light (`0x10`); activity level (`0x3F`); CO2 ppm (`0x12`); TVOC ppb (`0x13`); device name (`0x01`); temperature/humidity score (`0x3E`); worn state (`0x32`) |
| ACCEPT | `0x81` | result `0` (`0x21`), slot u8 (`0x22`), manager ID u32 (`0x23`), channel u8 (`0x24`), max frame u16 (`0x25`) |
| REJECT | `0x82` | result code (`0x21`), manager ID, channel, max frame |

Light values: green `0`, yellow `1`, red `2`, unknown `255`. Activity uses `0` still, `1` light, `2` medium, `3` high, `4` impact; `255` means unavailable. CO2 is ppm and TVOC is ppb; both use `65535` for unavailable. Temperature/humidity score is 0–3; worn state is `0` not worn and `1` worn. Both use `255` when unavailable. Battery is not part of telemetry.

## Pairing exchange

1. The node broadcasts HELLO on the configured manager channel every 1.5 s. It includes its stable ID, a fresh boot ID, incrementing sequence, name and capability bitmap.
2. The manager accepts only while its pairing window is open. It replies unicast to the node MAC. ACCEPT echoes the HELLO boot ID and sequence, and returns manager channel and assigned slot.
3. The node validates the echoed boot ID/sequence and returned channel, adds the manager peer and switches to unicast telemetry every 1 s.
4. The manager associates telemetry by source MAC and stable ID, rejects stale sequence numbers within a boot, and caches sensor fields independently.

The pairing window is a physical enrollment control, not cryptographic authentication. A node with a cloned ID can impersonate a device; add a per-device key and encrypted ESP-NOW peers before deploying sensitive data.

## Manager and SD wiring

The parent project's ESP32-CAM manager uses TFT GPIO14 (SCK), GPIO13 (MOSI), GPIO15 (CS), and GPIO2 (DC). On AI Thinker ESP32-CAM, these overlap SD_MMC CLK/CMD/D0/D3; SD_MMC 1-bit also needs GPIO14/15/2. The card slot cannot operate with that TFT wiring.

See `protocol.md` for packet examples, error behavior, and extension rules.
