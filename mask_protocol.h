#pragma once
#include <stdint.h>

constexpr uint16_t MASK_NOW_MAGIC = 0x534D;
constexpr uint8_t MASK_NOW_VERSION = 1;
constexpr uint16_t MASK_NOW_MAX_FRAME = 250;
constexpr uint8_t MASK_DEVICE_NAME_MAX = 23;
enum MaskMessageType : uint8_t { MASK_MSG_PAIR_HELLO=0x01, MASK_MSG_TELEMETRY=0x02, MASK_MSG_PAIR_ACCEPT=0x81, MASK_MSG_PAIR_REJECT=0x82 };
enum MaskTlvType : uint8_t { MASK_TLV_DEVICE_NAME=0x01, MASK_TLV_LIGHT_CODE=0x10, MASK_TLV_MOTION_LEVEL=0x11, MASK_TLV_CO2_PPM=0x12, MASK_TLV_TVOC_PPB=0x13, MASK_TLV_BATTERY_PERCENT=0x14, MASK_TLV_CAPABILITIES=0x20, MASK_TLV_PAIR_RESULT=0x21, MASK_TLV_ASSIGNED_SLOT=0x22, MASK_TLV_MANAGER_ID=0x23, MASK_TLV_MANAGER_CHANNEL=0x24, MASK_TLV_MAX_FRAME=0x25 };
enum MaskLightCode : uint8_t { MASK_LIGHT_GREEN=0, MASK_LIGHT_YELLOW=1, MASK_LIGHT_RED=2, MASK_LIGHT_UNKNOWN=255 };
enum MaskPairResult : uint8_t { MASK_PAIR_OK=0, MASK_PAIR_WINDOW_CLOSED=1, MASK_PAIR_ROSTER_FULL=2, MASK_PAIR_ID_CONFLICT=3, MASK_PAIR_BAD_REQUEST=4 };
enum MaskCapability : uint32_t { MASK_CAP_LIGHT=1u<<0, MASK_CAP_MOTION=1u<<1, MASK_CAP_CO2=1u<<2, MASK_CAP_TVOC=1u<<3, MASK_CAP_BATTERY=1u<<4 };
struct __attribute__((packed)) MaskFrameHeader { uint16_t magic; uint8_t version,type; uint16_t payloadLength; uint32_t deviceId,bootId,sequence; };
static_assert(sizeof(MaskFrameHeader)==18, "wire header must be 18 bytes");
