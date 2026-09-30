#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "mask_protocol.h"

// Standalone manager receiver. Send 'p' in Serial Monitor to open pairing for
// 60 seconds. It holds a 12-node RAM roster and prints light/sensor telemetry.
static constexpr uint8_t MANAGER_CHANNEL=6, MAX_NODES=12;
static constexpr uint32_t PAIR_MS=60000, STALE_MS=10000;
struct Node { bool used=false; uint8_t mac[6]={}; uint32_t id=0,boot=0,seq=0,lastSeen=0; char name[MASK_DEVICE_NAME_MAX+1]={}; };
static Node nodes[MAX_NODES]; static uint32_t pairUntil=0;
static uint8_t out[MASK_NOW_MAX_FRAME];
static uint16_t le16(const uint8_t*p){return uint16_t(p[0])|(uint16_t(p[1])<<8);}
static uint32_t le32(const uint8_t*p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
static const char* lightName(uint8_t v){return v==0?"GREEN":v==1?"YELLOW":v==2?"RED":"UNKNOWN";}
static void addTlv(uint8_t t,const uint8_t*v,uint8_t n,size_t& off){out[off++]=t;out[off++]=n;memcpy(out+off,v,n);off+=n;}
static void tlv8(uint8_t t,uint8_t v,size_t&o){addTlv(t,&v,1,o);}
static void tlv16(uint8_t t,uint16_t v,size_t&o){uint8_t b[]={uint8_t(v),uint8_t(v>>8)};addTlv(t,b,2,o);}
static void tlv32(uint8_t t,uint32_t v,size_t&o){uint8_t b[]={uint8_t(v),uint8_t(v>>8),uint8_t(v>>16),uint8_t(v>>24)};addTlv(t,b,4,o);}
static int findNode(const uint8_t*mac,uint32_t id){for(int i=0;i<MAX_NODES;i++)if(nodes[i].used&&nodes[i].id==id&&memcmp(nodes[i].mac,mac,6)==0)return i;return -1;}
static int freeNode(){for(int i=0;i<MAX_NODES;i++)if(!nodes[i].used)return i;return -1;}
static bool addPeer(const uint8_t*mac){if(esp_now_is_peer_exist(mac))return true;esp_now_peer_info_t p{};memcpy(p.peer_addr,mac,6);p.channel=MANAGER_CHANNEL;p.ifidx=WIFI_IF_STA;p.encrypt=false;return esp_now_add_peer(&p)==ESP_OK;}
static void reply(const uint8_t*mac,const MaskFrameHeader&request,bool accepted,uint8_t slot,uint8_t result){
  size_t n=sizeof(MaskFrameHeader);MaskFrameHeader h{MASK_NOW_MAGIC,MASK_NOW_VERSION,uint8_t(accepted?MASK_MSG_PAIR_ACCEPT:MASK_MSG_PAIR_REJECT),0,request.deviceId,request.bootId,request.sequence};memcpy(out,&h,sizeof(h));
  tlv8(MASK_TLV_PAIR_RESULT,result,n);if(accepted)tlv8(MASK_TLV_ASSIGNED_SLOT,slot,n);
  tlv32(MASK_TLV_MANAGER_ID,uint32_t(ESP.getEfuseMac()),n);tlv8(MASK_TLV_MANAGER_CHANNEL,MANAGER_CHANNEL,n);tlv16(MASK_TLV_MAX_FRAME,MASK_NOW_MAX_FRAME,n);
  h.payloadLength=n-sizeof(h);memcpy(out,&h,sizeof(h));esp_now_send(mac,out,n);
}
void onReceive(const uint8_t*mac,const uint8_t*data,int len){
  if(len<(int)sizeof(MaskFrameHeader)||len>MASK_NOW_MAX_FRAME)return;MaskFrameHeader h;memcpy(&h,data,sizeof(h));
  if(h.magic!=MASK_NOW_MAGIC||h.version!=MASK_NOW_VERSION||!h.deviceId||!h.bootId||h.payloadLength!=len-sizeof(h))return;
  char name[MASK_DEVICE_NAME_MAX+1]={};uint8_t light=255,activity=255,score=255,worn=255;uint16_t co2=0xFFFF,tvoc=0xFFFF;bool hasName=false,hasLight=false,hasCaps=false;size_t off=sizeof(h);
  while(off<(size_t)len){if((size_t)len-off<2)return;uint8_t t=data[off++],n=data[off++];if(n>(size_t)len-off)return;const uint8_t*v=data+off;
    if(t==MASK_TLV_DEVICE_NAME&&n&&n<=MASK_DEVICE_NAME_MAX){for(uint8_t i=0;i<n;i++)if(v[i]<0x20||v[i]>0x7E)return;memcpy(name,v,n);name[n]=0;hasName=true;}
    else if(t==MASK_TLV_CAPABILITIES&&n==4)hasCaps=true;
    else if(t==MASK_TLV_LIGHT_CODE&&n==1){light=v[0];hasLight=true;}
    else if(t==MASK_TLV_ACTIVITY_LEVEL&&n==1)activity=v[0];
    else if(t==MASK_TLV_CO2_PPM&&n==2)co2=le16(v);
    else if(t==MASK_TLV_TVOC_PPB&&n==2)tvoc=le16(v);
    else if(t==MASK_TLV_TEMP_HUMID_SCORE&&n==1)score=v[0];
    else if(t==MASK_TLV_WORN&&n==1)worn=v[0];off+=n;
  }
  int slot=findNode(mac,h.deviceId);
  if(h.type==MASK_MSG_PAIR_HELLO){
    if(!hasName||!hasCaps)return;
    if(slot<0){if(int32_t(pairUntil-millis())<=0)return;slot=freeNode();if(slot<0){if(addPeer(mac))reply(mac,h,false,255,MASK_PAIR_ROSTER_FULL);return;}if(!addPeer(mac))return;nodes[slot].used=true;nodes[slot].id=h.deviceId;memcpy(nodes[slot].mac,mac,6);}
    Node& node=nodes[slot];strlcpy(node.name,name,sizeof(node.name));node.boot=h.bootId;node.seq=h.sequence;node.lastSeen=millis();reply(mac,h,true,slot,MASK_PAIR_OK);
    Serial.printf("PAIRED slot=%u name=%s id=%08lX\n",slot,node.name,(unsigned long)node.id);return;
  }
  if(h.type!=MASK_MSG_TELEMETRY||slot<0||!hasLight)return;Node& node=nodes[slot];
  if(node.boot==h.bootId&&int32_t(h.sequence-node.seq)<=0)return;
  if(node.boot!=h.bootId){node.boot=h.bootId;node.seq=0;}node.seq=h.sequence;node.lastSeen=millis();
  Serial.printf("slot=%u name=%s light=%s activity=%u CO2=%u ppm TVOC=%u ppb temp_humid_score=%u worn=%u\n",slot,node.name,lightName(light),activity,co2,tvoc,score,worn);
}
void setup(){Serial.begin(115200);WiFi.mode(WIFI_STA);WiFi.disconnect();esp_wifi_set_channel(MANAGER_CHANNEL,WIFI_SECOND_CHAN_NONE);
  if(esp_now_init()!=ESP_OK){Serial.println("ESP-NOW init failed");return;}esp_now_register_recv_cb(onReceive);
  Serial.printf("Receiver MAC=%s channel=%u. Send p to pair for 60 seconds.\n",WiFi.macAddress().c_str(),MANAGER_CHANNEL);}
void loop(){if(Serial.available()&&Serial.read()=='p'){pairUntil=millis()+PAIR_MS;Serial.println("Pairing open for 60 seconds");}
  for(uint8_t i=0;i<MAX_NODES;i++)if(nodes[i].used&&millis()-nodes[i].lastSeen>STALE_MS&&nodes[i].lastSeen!=0){nodes[i].lastSeen=0;Serial.printf("STALE slot=%u name=%s\n",i,nodes[i].name);}delay(10);}
