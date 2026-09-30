# Mask fleet manager and telemetry protocol

## Product behavior

- The ESP32-CAM is the manager: landscape ST7735S, buttons, Wi-Fi setup, manager OTA, ESP-NOW receive, roster, and a phone-facing Bluetooth Classic SPP feed.
- It manages at most 12 named mask devices, six cards per screen. Cards show the device name and only its live light color. Names are limited to 23 ASCII bytes in the protocol and clipped to seven characters on the small TFT; the full name stays in the roster/BLE data.
- Unpaired positions display `PAIR`; paired devices with no recent telemetry display `NO DATA`. These are connection labels, not sensor/light interpretations.
- GPIO0 short press goes to the previous page; GPIO3 short press goes to the next page. Hold GPIO3 for 1.2 seconds to open the 60-second pairing window. GPIO0 must be released during boot.
- Sensor readings are decoded and held separately from the display state: activity level, CO2, TVOC, temperature/humidity score, and worn state. The TFT renderer never uses those values to choose a light color.

## Roles and data flow

```text
Mask sensors
    │  ESP-NOW HELLO / TELEMETRY (binary envelope + TLV fields)
    ▼
ESP32-CAM manager ─── TFT: device name + light only
    │
    ├── NVS: paired device ID, MAC, and name
    ├── RAM cache: light + latest sensor values + sequence/time
    ├── Wi-Fi: manager's own firmware OTA (ArduinoOTA)
    └── Bluetooth Classic SPP: phone reads detailed sensor data and can remove one paired node
```

ESP-NOW and Wi-Fi share the 2.4 GHz radio. Nodes must use the manager's current channel: channel 6 while the setup AP is active, or the router's channel when joined to home Wi-Fi. Each paired node sends unicast to the manager MAC. BLE is intended for the phone to connect to the manager; it is separate from mask-node ESP-NOW pairing.

## TFT and microSD pin conflict

The current TFT wiring uses GPIO14 (SCK), GPIO13 (MOSI), GPIO15 (CS), and GPIO2 (DC). On the AI Thinker ESP32-CAM, the microSD/SDMMC signals use GPIO14 (CLK), GPIO15 (CMD), GPIO2 (D0), GPIO4 (D1), GPIO12 (D2), and GPIO13 (D3). Thus the display already occupies CLK, CMD, D0, and D3. Even SD 1-bit mode uses GPIO14, GPIO15, and GPIO2, so the microSD slot cannot be used at the same time as this TFT wiring. The slot itself remains physically present; simultaneous use would require rewiring the display to different GPIOs or using an external SD interface. The pin map follows Espressif's [Arduino ESP32 SD_MMC documentation](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/sdmmc.html) and the [AI-Thinker ESP32-CAM schematic](https://docs.ai-thinker.com/_media/esp32/docs/esp32_cam_sch.pdf).

## V1 frame envelope

The on-air format is binary, not JSON, to keep ESP-NOW packets compact. A JSON rendering is included below for readability. All multi-byte integers are little-endian. The maximum full frame is 250 bytes. `MaskFrameHeader` in `include/mask_protocol.h` is 18 bytes:

| Offset | Size | Field | Meaning |
| ---: | ---: | --- | --- |
| 0 | 2 | `magic` | `0x534D`, bytes `MS` |
| 2 | 1 | `version` | `1` |
| 3 | 1 | `type` | Message type below |
| 4 | 2 | `payloadLength` | Number of bytes after the header |
| 6 | 4 | `deviceId` | Stable, unique node ID; not the MAC address |
| 10 | 4 | `bootId` | Random nonzero value regenerated on every node boot |
| 14 | 4 | `sequence` | Increments for each frame during this boot |

The payload is a sequence of TLVs: `type:u8`, `length:u8`, then `length` value bytes. Unknown TLVs are skipped. A known TLV with an invalid length or value rejects the whole frame. This permits optional sensors and later protocol extensions without changing the envelope. The current manager implementation parses v1 HELLO and TELEMETRY frames and retains unknown fields only by skipping them.

| TLV | Type | Length / encoding | Meaning |
| --- | ---: | --- | --- |
| Device name | `0x01` | 1–23 ASCII bytes | Stable display/phone name; e.g. `MASK-A01` |
| Light code | `0x10` | 1 byte | `0` green, `1` yellow, `2` red, `255` unavailable |
| Activity level | `0x3F` | 1 byte | 0–4 enum: 0 still, 1 light, 2 medium, 3 high, 4 impact; 255 unavailable |
| CO2 | `0x12` | uint16 ppm | `0xFFFF` unavailable |
| TVOC | `0x13` | uint16 ppb | `0xFFFF` unavailable |
| Worn state | `0x32` | 1 byte | 0 not worn, 1 worn, 255 unavailable |
| Temperature/humidity score | `0x3E` | 1 byte | 0–3 score; 255 unavailable |
| Capabilities | `0x20` | uint32 bitmap | HELLO: bit 0 light, 1 activity, 2 CO2, 3 TVOC, 4 temperature/humidity score, 5 worn |
| Pair result | `0x21` | 1 byte enum | `0` accepted, `1` window closed, `2` full, `3` ID conflict, `4` malformed request |
| Assigned slot | `0x22` | 1 byte, zero-based | Internal roster index; UI still shows the name |
| Manager ID | `0x23` | uint32 | Manager's stable ID, HELLO response |
| Manager channel | `0x24` | 1 byte | Current Wi-Fi/ESP-NOW channel |
| Maximum frame | `0x25` | uint16 | Manager's accepted frame limit (250) |

TELEMETRY carries only the seven product fields listed above, and must include one valid Light TLV. Other fields are optional. Missing optional fields leave the manager's latest cached value unchanged; an explicit unavailable sentinel marks that measurement unavailable. Light codes select display colors only. No health/alarm meaning is inferred from color by the manager.

### Readable TELEMETRY example

The following is a decoded view of a node packet; the actual frame is the binary header followed by these TLVs:

```json
{
  "version": 1,
  "type": "TELEMETRY",
  "device_id": "0x01A20B31",
  "boot_id": "0x7D5A1201",
  "sequence": 42,
  "name": "MASK-A01",
  "light": 2,
  "activity_level": 2,
  "co2_ppm": 846,
  "tvoc_ppb": 127,
  "temp_humid_score": 2,
  "worn": 1
}
```

For that example, the header payload length is 27 (`0x001B`), and the complete frame is 45 bytes. In hex, the start is `4D 53 01 02 1B 00 31 0B A2 01 01 12 5A 7D 2A 00 00 00`; the TLVs follow in table order. This makes field offsets deterministic while keeping the sensor extension points optional.

## Pairing handshake (the first exchange)

1. User holds GPIO3; manager opens pairing for 60 seconds.
2. An unpaired node broadcasts `PAIR_HELLO` every 1.5 seconds until it receives a response. It includes the required name and capability bitmap TLVs. Header has stable `deviceId`, fresh `bootId`, and current `sequence`.
3. During pairing, the manager validates the frame and source MAC, rejects duplicate IDs with a different MAC, assigns the first free internal slot, and persists ID/MAC/name. A full roster gets `PAIR_REJECT` with `ROSTER_FULL`.
4. Manager unicasts `PAIR_ACCEPT` to the source MAC. It echoes `deviceId`, `bootId`, and the HELLO `sequence` so the node can match the response; TLVs contain result, assigned slot, manager ID, current channel, and maximum frame size.
5. The node verifies the echoed boot ID and sequence, stores manager ID/MAC/slot/channel, then begins unicast TELEMETRY. If it reboots, it sends HELLO again; an already enrolled ID+MAC is acknowledged even if the pairing window is closed.
6. For a conflicting ID/MAC, malformed HELLO, or full roster, manager replies with the corresponding reject code when pairing is open. An unknown node outside pairing receives no response.

**Pair HELLO decoded example**

```json
{
  "version": 1,
  "type": "PAIR_HELLO",
  "device_id": "0x01A20B31",
  "boot_id": "0x7D5A1201",
  "sequence": 1,
  "name": "MASK-A01",
  "capabilities": ["light", "activity", "co2", "tvoc", "temperature_humidity_score", "worn"]
}
```

**Manager PAIR_ACCEPT decoded example**

```json
{
  "version": 1,
  "type": "PAIR_ACCEPT",
  "device_id": "0x01A20B31",
  "boot_id": "0x7D5A1201",
  "sequence": 1,
  "pair_result": "OK",
  "assigned_slot": 0,
  "manager_id": "0x64E8D0",
  "manager_channel": 6,
  "max_frame": 250
}
```

## Multi-node and freshness behavior

- Each node has its own stable device ID, boot ID, sequence, and MAC. MAC is learned from ESP-NOW metadata; a different MAC claiming an enrolled ID is not silently enrolled.
- Nodes send unicast TELEMETRY every 1 second. The manager has a 64-frame receive queue; the callback only copies bytes, while parsing, NVS, and TFT drawing happen in the main loop.
- Per-node sequence numbers reject duplicate/out-of-order frames within one boot. A new boot ID resets the sequence baseline.
- After 10 seconds without a valid fresh frame, the card retains its name but shows `NO DATA` and a gray indicator. The sensor cache remains available for a phone snapshot and becomes stale with the node.
- Store only roster identity/name in NVS; keep rapidly changing sensor samples and sequence counters in RAM to avoid flash wear.

## Phone Bluetooth Classic SPP

The Android app connects to `VitaAir-Manager` over Bluetooth Classic SPP. The manager sends newline-delimited JSON snapshots, one roster slot per interval. A mask snapshot exposes only the seven telemetry fields plus manager metadata such as slot, paired/online, ID, sequence, and sample age. The phone can issue `UNPAIR <slot> <device-id>` followed by a newline. The manager verifies both slot and device ID, removes its ESP-NOW peer and persisted roster entry, and replies with a `command_result` JSON line. The device may only rejoin while the manager's physical pairing window is open; holding the manager's pairing button remains required.

The phone command does not remotely open pairing. This keeps roster removal reversible without allowing an app-side command to enroll arbitrary radios. Bluetooth must never change the TFT's rule: only the light code selects its indicator color.

## OTA / FOTA boundary

- **Manager OTA is implemented:** update this ESP32-CAM over its saved Wi-Fi using ArduinoOTA and the `ota` PlatformIO environment. Its dual app partitions are `ota_0` and `ota_1` in `partitions_ota.csv`.
- **Mask-node FOTA is not implemented.** The reserved v1 message IDs (`FOTA_OFFER`, `FOTA_READY`, `FOTA_CHUNK`, `FOTA_ACK`, `FOTA_FINISH`) are placeholders, not accepted by the current parser. Before implementing node FOTA, each mask-node board needs its own dual-image partition layout, signed image verification, transfer ID, target version/size/SHA-256, resumable offsets/ACK bitmap, timeout/retry policy, and rollback confirmation. Do not stream firmware in normal telemetry packets.
- Proposed FOTA control metadata: offer carries transfer ID, hardware ID, semantic version, image length, SHA-256 and signature; ready/reject returns capacity/status; chunk carries transfer ID and offset plus bounded data; ACK returns highest contiguous offset and a received-chunk bitmap; finish verifies hash/signature and reports boot/rollback result. Actual chunk transport should be selected after node hardware and OTA partition sizes are known.

## Security and implementation state

Current ESP-NOW v1 pairing and telemetry are unencrypted. Physical pairing-window gating limits accidental roster changes but is not authentication. Before field deployment, add a per-node enrollment secret, encrypted unicast peers, replay protection persisted or boot-session-scoped, and signed FOTA images. Node-side protocol firmware and phone GATT implementation remain separate follow-on work; this repository currently implements the manager-side parser/cache/display and protocol contract.
