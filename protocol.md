# Protocol contract

## Values and validation

| TLV | Length | Meaning |
|---|---:|---|
| `0x01` DEVICE_NAME | 1–23 | printable ASCII, stable user-facing device name |
| `0x10` LIGHT_CODE | 1 | 0 green, 1 yellow, 2 red, 255 unknown; required on telemetry |
| `0x11` MOTION_LEVEL | 1 | 0–100, or 255 unavailable |
| `0x12` CO2_PPM | 2 | CO2 concentration in ppm, `0xFFFF` unavailable |
| `0x13` TVOC_PPB | 2 | TVOC concentration in ppb, `0xFFFF` unavailable |
| `0x14` BATTERY_PERCENT | 1 | 0–100, or 255 unavailable |
| `0x20` CAPABILITIES | 4 | bit 0 light, 1 motion, 2 CO2, 3 TVOC, 4 battery |
| `0x21` PAIR_RESULT | 1 | 0 accepted, 1 pairing closed, 2 full, 3 ID conflict, 4 invalid request |
| `0x22` ASSIGNED_SLOT | 1 | zero-based manager slot 0–11 |
| `0x23` MANAGER_ID | 4 | stable manager identifier |
| `0x24` MANAGER_CHANNEL | 1 | Wi-Fi channel 1–13 |
| `0x25` MAX_FRAME | 2 | maximum full frame length, currently 250 bytes |

Header fields `magic`, `version`, and `payloadLength` must match; device and boot IDs must be nonzero. TLV lengths must fit the frame. Unknown TLVs may be skipped. A receiver drops malformed frames and duplicate/older telemetry sequence numbers for the current boot. A sender creates a fresh nonzero boot ID at every restart and starts its sequence at one.

## Example exchange

Assume device `0xA0010001`, boot `0x11223344`, manager channel 6, HELLO sequence 1, name `MASK-A01`. The frame header is 18 bytes; payload has name and capability TLVs. The manager response uses message type `0x81`, echoes device ID, boot ID, and sequence 1, then carries result 0, assigned slot, manager ID, channel 6, and max frame 250.

Then the node sends `0x02` telemetry once per second. A sample payload is light=red, activity=35, CO2=810 ppm, TVOC=120 ppb. Those are separate typed fields so the manager can show only the light while retaining sensor values for a later phone interface.

## Extension and reliability

- Add new fields with unused TLV numbers. Do not change existing field units or meanings within v1.
- A missing optional reading means “not reported”; the unavailable sentinel means the sensor exists but has no current value.
- Sequence numbers are compared only within the same boot ID. After reboot the new boot ID resets the replay window.
- Pair replies echo the HELLO sequence and boot ID, so a node ignores delayed responses from previous attempts or boots.
- Nodes should send telemetry periodically (1 s in the sample). The manager marks a node stale after 10 s without a valid update.
- ESP-NOW and Wi-Fi share the radio. The manager's active Wi-Fi channel is authoritative; a node must be on that channel before it sends HELLO.

## Future updates

Manager firmware OTA over its Wi-Fi is separate from node FOTA. The FOTA IDs are reserved but there is no chunk transfer, signing, rollback, or node bootloader implementation in v1. Before FOTA, define signed manifests, device compatibility, chunk integrity, resume/ack behavior, rollback, and per-device authorization. Do not treat pairing as secure identity proof.
