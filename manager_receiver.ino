#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "mask_protocol.h"

// Serial receiver/reference parser. The project manager firmware additionally
// applies pairing policy, persists its 12-device roster, and drives the TFT.
static uint32_t lastSequence[12]={};
static uint32_t knownIds[12]={};
static uint8_t knownMac[12][6]={};
static bool enrolled[12]={};
static const char* lightName(uint8_t v){return v==0?"GREEN":v==1?"YELLOW":v==2?"RED":"UNKNOWN";}
static uint16_t le16(const uint8_t*p){return uint16_t(p[0])|(uint16_t(p[1])<<8);}
static uint32_t le32(const uint8_t*p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
void onReceive(const uint8_t* mac,const uint8_t* data,int len){
  if(len<(int)sizeof(MaskFrameHeader)||len>MASK_NOW_MAX_FRAME)return;
  MaskFrameHeader h;memcpy(&h,data,sizeof(h));
  if(h.magic!=MASK_NOW_MAGIC||h.version!=MASK_NOW_VERSION||h.deviceId==0||h.bootId==0||h.payloadLength!=len-sizeof(h))return;
  char name[MASK_DEVICE_NAME_MAX+1]={};uint8_t light=255,activity=255,battery=255;uint16_t co2=0xFFFF,tvoc=0xFFFF;bool hasLight=false;
  size_t off=sizeof(h);while(off<(size_t)len){if((size_t)len-off<2)return;uint8_t t=data[off++],n=data[off++];if(n>(size_t)len-off)return;const uint8_t*v=data+off;
    if(t==MASK_TLV_DEVICE_NAME&&n&&n<=MASK_DEVICE_NAME_MAX){memcpy(name,v,n);name[n]=0;}
    else if(t==MASK_TLV_LIGHT_CODE&&n==1){light=v[0];hasLight=true;}
    else if(t==MASK_TLV_MOTION_LEVEL&&n==1)activity=v[0];
    else if(t==MASK_TLV_CO2_PPM&&n==2)co2=le16(v);
    else if(t==MASK_TLV_TVOC_PPB&&n==2)tvoc=le16(v);
    else if(t==MASK_TLV_BATTERY_PERCENT&&n==1)battery=v[0];
    off+=n;
  }
  if(h.type==MASK_MSG_PAIR_HELLO){Serial.printf("HELLO id=%08lX name=%s MAC=%02X:%02X:%02X:%02X:%02X:%02X\n",(unsigned long)h.deviceId,name,mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);return;}
  if(h.type!=MASK_MSG_TELEMETRY||!hasLight)return;
  int slot=-1;for(int i=0;i<12;i++)if(enrolled[i]&&knownIds[i]==h.deviceId&&memcmp(knownMac[i],mac,6)==0){slot=i;break;}
  if(slot<0){Serial.printf("IGNORED unpaired telemetry id=%08lX\n",(unsigned long)h.deviceId);return;}
  if(h.sequence<=lastSequence[slot])return;lastSequence[slot]=h.sequence;
  Serial.printf("DEVICE %s light=%s activity=%u CO2=%u ppm TVOC=%u ppb battery=%u%% seq=%lu\n",name[0]?name:"(unnamed)",lightName(light),activity,co2,tvoc,battery,(unsigned long)h.sequence);
}
void setup(){Serial.begin(115200);WiFi.mode(WIFI_STA);if(esp_now_init()!=ESP_OK){Serial.println("ESP-NOW init failed");return;}esp_now_register_recv_cb(onReceive);Serial.printf("Manager reference receiver MAC=%s channel=%u\n",WiFi.macAddress().c_str(),WiFi.channel());}
void loop(){delay(100);}
