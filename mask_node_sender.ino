#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "mask_protocol.h"

// Configure uniquely for every mask and match the manager's current channel.
static constexpr uint32_t DEVICE_ID = 0xA0010001;
static constexpr char DEVICE_NAME[] = "MASK-A01";
static constexpr uint8_t MANAGER_CHANNEL = 6;
static uint8_t MANAGER_MAC[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}; // set manager STA/AP MAC for unicast after pairing
static constexpr uint32_t PAIR_INTERVAL_MS=1500, TELEMETRY_INTERVAL_MS=1000;

static uint8_t packet[MASK_NOW_MAX_FRAME];
static uint32_t bootId, sequenceNo=0, lastTx=0;
static bool paired=false;
static uint8_t assignedSlot=255;
static uint32_t managerId=0;

struct Telemetry { uint8_t light, activity, tempHumidScore, worn; uint16_t co2, tvoc; };
Telemetry readTelemetry() {
  // Replace these demo values with sensor reads. 255/0xFFFF means unavailable.
  return {MASK_LIGHT_UNKNOWN, 255, 255, 255, 0xFFFF, 0xFFFF};
}
void addTlv(uint8_t type,const uint8_t* value,uint8_t len,size_t& off) {
  if(off+2+len>MASK_NOW_MAX_FRAME) return;
  packet[off++]=type; packet[off++]=len; memcpy(packet+off,value,len); off+=len;
}
void tlv8(uint8_t type,uint8_t v,size_t& off){addTlv(type,&v,1,off);}
void tlv16(uint8_t type,uint16_t v,size_t& off){uint8_t b[]={uint8_t(v),uint8_t(v>>8)};addTlv(type,b,2,off);}
void tlv32(uint8_t type,uint32_t v,size_t& off){uint8_t b[]={uint8_t(v),uint8_t(v>>8),uint8_t(v>>16),uint8_t(v>>24)};addTlv(type,b,4,off);}
size_t beginFrame(uint8_t type) {
  MaskFrameHeader h{MASK_NOW_MAGIC,MASK_NOW_VERSION,type,0,DEVICE_ID,bootId,++sequenceNo};
  memcpy(packet,&h,sizeof(h)); return sizeof(h);
}
void finishFrame(size_t off) {
  auto* h=reinterpret_cast<MaskFrameHeader*>(packet); h->payloadLength=off-sizeof(*h);
}
void sendHello() {
  size_t off=beginFrame(MASK_MSG_PAIR_HELLO);
  addTlv(MASK_TLV_DEVICE_NAME,reinterpret_cast<const uint8_t*>(DEVICE_NAME),strlen(DEVICE_NAME),off);
  tlv32(MASK_TLV_CAPABILITIES,MASK_CAP_LIGHT|MASK_CAP_MOTION|MASK_CAP_CO2|MASK_CAP_TVOC|MASK_CAP_TEMP_HUMID_SCORE|MASK_CAP_WORN,off);
  finishFrame(off); esp_now_send(nullptr,packet,off);
}
void sendTelemetry() {
  const Telemetry t=readTelemetry(); size_t off=beginFrame(MASK_MSG_TELEMETRY);
  addTlv(MASK_TLV_DEVICE_NAME,reinterpret_cast<const uint8_t*>(DEVICE_NAME),strlen(DEVICE_NAME),off);
  tlv8(MASK_TLV_LIGHT_CODE,t.light,off); tlv8(MASK_TLV_ACTIVITY_LEVEL,t.activity,off);
  tlv16(MASK_TLV_CO2_PPM,t.co2,off); tlv16(MASK_TLV_TVOC_PPB,t.tvoc,off);
  tlv8(MASK_TLV_TEMP_HUMID_SCORE,t.tempHumidScore,off); tlv8(MASK_TLV_WORN,t.worn,off);
  finishFrame(off); esp_now_send(MANAGER_MAC,packet,off);
}
void onReceive(const uint8_t* mac,const uint8_t* data,int len) {
  if(len<sizeof(MaskFrameHeader)||len>MASK_NOW_MAX_FRAME)return;
  MaskFrameHeader h; memcpy(&h,data,sizeof(h));
  if(h.magic!=MASK_NOW_MAGIC||h.version!=MASK_NOW_VERSION||h.deviceId!=DEVICE_ID||h.bootId!=bootId||h.payloadLength!=len-sizeof(h))return;
  bool resultOk=false,hasChannel=false; uint8_t channel=0,slot=255; uint32_t mgr=0;
  size_t off=sizeof(h);
  while(off<(size_t)len) {
    if((size_t)len-off<2)return; uint8_t type=data[off++],n=data[off++]; if(n>(size_t)len-off)return;
    const uint8_t* v=data+off;
    if(type==MASK_TLV_PAIR_RESULT&&n==1)resultOk=v[0]==MASK_PAIR_OK;
    if(type==MASK_TLV_ASSIGNED_SLOT&&n==1)slot=v[0];
    if(type==MASK_TLV_MANAGER_ID&&n==4)mgr=uint32_t(v[0])|(uint32_t(v[1])<<8)|(uint32_t(v[2])<<16)|(uint32_t(v[3])<<24);
    if(type==MASK_TLV_MANAGER_CHANNEL&&n==1){channel=v[0];hasChannel=true;}
    off+=n;
  }
  if(h.type==MASK_MSG_PAIR_ACCEPT&&resultOk&&hasChannel&&channel>=1&&channel<=13&&h.sequence==sequenceNo) {
    memcpy(MANAGER_MAC,mac,6); managerId=mgr; assignedSlot=slot;
    esp_now_del_peer(MANAGER_MAC); esp_wifi_set_channel(channel,WIFI_SECOND_CHAN_NONE);
    esp_now_peer_info_t peer{}; memcpy(peer.peer_addr,MANAGER_MAC,6); peer.channel=channel; peer.ifidx=WIFI_IF_STA; peer.encrypt=false;
    paired=esp_now_add_peer(&peer)==ESP_OK;
    Serial.printf("Paired manager=%08lX slot=%u channel=%u\n",(unsigned long)managerId,assignedSlot,channel);
  } else if(h.type==MASK_MSG_PAIR_REJECT) Serial.println("Pair rejected; check pairing window, ID, and roster capacity.");
}
void setup() {
  Serial.begin(115200); WiFi.mode(WIFI_STA); WiFi.disconnect(); esp_wifi_set_channel(MANAGER_CHANNEL,WIFI_SECOND_CHAN_NONE);
  bootId=esp_random(); if(!bootId)bootId=1;
  if(esp_now_init()!=ESP_OK){Serial.println("ESP-NOW init failed");return;}
  esp_now_register_recv_cb(onReceive);
  esp_now_peer_info_t broadcast{}; memset(broadcast.peer_addr,0xFF,6); broadcast.channel=MANAGER_CHANNEL; broadcast.ifidx=WIFI_IF_STA;
  if(!esp_now_is_peer_exist(broadcast.peer_addr))esp_now_add_peer(&broadcast);
  Serial.printf("Node %s id=%08lX boot=%08lX ch=%u\n",DEVICE_NAME,(unsigned long)DEVICE_ID,(unsigned long)bootId,MANAGER_CHANNEL);
}
void loop() {
  const uint32_t now=millis(); const uint32_t interval=paired?TELEMETRY_INTERVAL_MS:PAIR_INTERVAL_MS;
  if(now-lastTx>=interval){lastTx=now;if(paired)sendTelemetry();else sendHello();}
  delay(5);
}
