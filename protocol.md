# ESP-NOW protocol v1

## Telemetry fields

The mask node's `TELEMETRY` message (`0x02`) contains only these seven product fields. Each payload item uses TLV encoding: `type:u8`, `length:u8`, then `value` bytes. Multi-byte values are little-endian.

| Field | TLV | Length | Encoding |
|---|---:|---:|---|
| Light status | `0x10` | 1 byte | `0` green, `1` yellow, `2` red, `255` unavailable |
| Activity level | `0x3F` | 1 byte | `0` still, `1` light, `2` medium, `3` high, `4` impact; `255` unavailable |
| CO₂ | `0x12` | 2 bytes | ppm; `0xFFFF` unavailable |
| TVOC | `0x13` | 2 bytes | ppb; `0xFFFF` unavailable |
| Device name | `0x01` | 1–23 bytes | Printable ASCII; also sent in `PAIR_HELLO` |
| Temperature/humidity score | `0x3E` | 1 byte | score `0–3`; `255` unavailable |
| Worn state | `0x32` | 1 byte | `0` not worn, `1` worn, `255` unavailable |

Light status is required in every telemetry frame; the other six fields are optional. Omitted optional TLVs mean “not reported in this frame”; the unavailable sentinel means the device reports the field but has no reading. Unknown TLVs are skipped by receivers. Battery and the legacy activity identifier `0x11` are not part of this telemetry contract.

## Frame envelope

The binary frame starts with an 18-byte header:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | Magic `0x534D` (wire bytes `4D 53`, ASCII `MS`) |
| 2 | 1 | Version `1` |
| 3 | 1 | Message type |
| 4 | 2 | Payload length |
| 6 | 4 | Stable device ID, nonzero |
| 10 | 4 | Boot ID, nonzero and regenerated on reboot |
| 14 | 4 | Sequence number, incremented for each frame in that boot |

Maximum complete frame length is 250 bytes. Receivers reject malformed lengths, duplicate TLVs, invalid values, and duplicate or older telemetry sequence numbers within the same boot ID.

## Message types and handshake-only fields

| Message | Type | Payload |
|---|---:|---|
| `PAIR_HELLO` | `0x01` | Device name (`0x01`) and capabilities (`0x20`) |
| `TELEMETRY` | `0x02` | The seven fields in the table above |
| `PAIR_ACCEPT` | `0x81` | Pair result (`0x21`), assigned slot (`0x22`), manager ID (`0x23`), channel (`0x24`), max frame (`0x25`) |
| `PAIR_REJECT` | `0x82` | Pair result (`0x21`); may include manager ID, channel, and max frame |

Handshake TLVs are protocol control data, not additional sensor/product telemetry fields. The manager echoes the HELLO device ID, boot ID, and sequence in its response. Pair results are `0` accepted, `1` pairing closed, `2` roster full, `3` ID conflict, `4` invalid request. Capabilities use a uint32 bitmap: bit 0 light, bit 1 activity, bit 2 CO₂, bit 3 TVOC, bit 4 temperature/humidity score, bit 5 worn state.

## Example telemetry

Readable decoded example:

```json
{
  "type": "TELEMETRY",
  "light": 1,
  "activity_level": 2,
  "co2_ppm": 846,
  "tvoc_ppb": 127,
  "name": "MASK-A01",
  "temp_humid_score": 2,
  "worn": 1
}
```

For this sample, the payload is 30 bytes and the full frame is 48 bytes. With device ID `0xA0010001`, boot ID `0x11223344`, and sequence `42`, the complete little-endian frame is:

```text
4D 53 01 02 1E 00 01 00 01 A0 44 33 22 11 2A 00 00 00
01 08 4D 41 53 4B 2D 41 30 31
10 01 01 3F 01 02 12 02 4E 03 13 02 7F 00
3E 01 02 32 01 01
```

The name TLV is variable length; all six remaining values are one-byte or two-byte fixed-width fields. No battery value is sent.

## Timing and update boundary

- Nodes broadcast `PAIR_HELLO` every 1.5 seconds until accepted, then send unicast telemetry every 1 second.
- The manager marks a node stale after 10 seconds without a valid telemetry frame.
- ESP-NOW and Wi-Fi share the radio. The node must use the manager's current channel before sending HELLO.
- Manager firmware OTA over Wi-Fi is separate from node FOTA. Node-side FOTA transfer, signing, rollback, and recovery are not implemented by these telemetry messages.
