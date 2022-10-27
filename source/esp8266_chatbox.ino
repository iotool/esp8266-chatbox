// ESP8266 - Chatbox - Captive Portal
// github.com/iotool/esp8266-chatbox
// 
// Chat running on ESP8266 wifi chip.
// 
// Communication and information without
// mobile phone network or internet
// for smartphones / tablets nearby.
// 
// Mesh network to increase the range
// and build a wifi based emergency
// infrastructure for small communities.
// 
// dynamic access point
// dynamic subnet
// captive portal popup
// captive portal redirect
// max client 8 connections
// max client auto disconnect
// serial debug state
// reboot on out of memory
// reboot use rtc memory for backup
// OTA update for ArduinoDroid
// OTA password protect by cli
// admin cli for status, ota, restart
// dynamic html without memory issue
// validate input size
// mesh network by STA connect to AP
// workaround age++ jumps
// editable info page
// current spike to keep powerbank on
// 
// This is a proof-of-concept 
// to combine hotspot and mesh nodes.
// All chat data shared between each
// node of the mesh without a master.
// You can use this for local chat
// without internet access or services.

// overwrite esp-sdk (wifi_set_country)
extern "C" {
  #include "user_interface.h"
}
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <Arduino.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <StreamString.h>
#include <EEPROM.h>

/* --- configuration --- */

#define AP_PHYMOD   WIFI_PHY_MODE_11N
#define AP_POWER    20   // 0..20 Lo/Hi
#define AP_CHANNEL  13   // 1..13 (13)
#define AP_MAXCON   8    // 1..8 conns
#define AP_MAXTOUT  7    // 1..25 sec
#define ESP_OUTMEM  4096 // autoreboot
#define ESP_RTCADR  65   // offset
#define LED_OFF     0    // led on
#define LED_ON      1    // led off
#define CHAT_MLEN   71   // msg length
#define CHAT_MARY   64   // msg array
#define CHAT_MRTC   5    // rtc array
#define CHAT_MAXID  0xFF // limit
#define CHAT_MAXAGE 0xFFFFFFF  // limit
#define INFO_MLEN   508  // txt length
#define CTYPE_HTML  1    // content html
#define CTYPE_TEXT  2    // content text
#define MESH_INIT   0    // mode
#define MESH_SCAN   10   // mode
#define MESH_SCANE  11   // mode
#define MESH_JOIN   20   // mode
#define MESH_JOINE  21   // mode  
#define MESH_DINIT  5000 // delay loop
#define MESH_TSCAN  500  // timeout
#define MESH_DJOIN  2500 // delay next
#define MESH_TJOIN  12000// timeout
#define MESH_DNOOP  100  // delay noop
#define MESH_THTTP  750  // timeout
#define SPIKE_IOD1  5    // gpio5 d1-330R-gnd
#define SPIKE_IOD2  4    // gpio4 d2-330R-gnd
#define SPIKE_PWOFF 4500 // ms off +2mA avg
#define SPIKE_PWON  500  // ms on  +20mA peek

// Don't change defined configuration to
// be compartible to other mesh nodes.
// 
// apPass  ""         open hotspot
// apPass  "Chat!b0x" private hotspot
// 
// cliPwd  "<your-password>" {a..z0..9}
// have to change to edit info page,
// enable ota firmware update and reset.
// /cli?cmd=login-<your-password>

const char* apName = "Chatbox-"; // net
const char* apPass = "";         // pkey
const char* apAuth = "Chat!b0x"; // pkey
const char* apHost = "web";      // host
const char* cliPwd = "Chat.b0x"; // adm

// Each mesh node using the same 
// WiFi channel and hotspot prefix.
// The mesh is very simple, each node
// scan all access point at the same
// channel and connect, if the hotspot
// using the same prefix.
// 
// ESP8266 supports max 8 connections.
// To provide more users this sketch
// automatic disconnect all connections
// after reaching 8 connections.
// We need the disconnect to provide
// access to other mesh nodes.
// 
// The chat structure has a fix size
// and the data segment used for sender,
// receiver and message.
// To sync the chat over multiple nodes
// I use the physical address, sequence
// timestamp/age and length of sender,
// receiver and message.
// Older messages will delete by newer.
// 
// Using ESP8266 in AP_STA mode will 
// block the AP durring using as STA.
// To reduce this negative effect I 
// limited the mesh syncs.

/* --- variables --- */

DNSServer dnsServer;
ESP8266WebServer httpServer(80);

// init once at setup
String apSSID="";
String urlHome="";
String urlChatRefresh="4,url=";
String urlInfoRefresh="4,url=";

// chat structure (static ~ not in DRAM)
// fixed format aligned to 16B RAM block
typedef struct {
  uint8_t  node[2]; // mac address node
  uint8_t  id;      // sequence
  uint8_t  mlen;    // length message
  uint8_t  slen:4;  // length sender
  uint8_t  rlen:4;  // length receiver
  uint32_t age;     // second age
  char     data[CHAT_MLEN]; // buffer
} chatMsgT;
static chatMsgT chatMsg[CHAT_MARY]={0};

// rtc memory as backup
// keep some data after reset
typedef struct { 
  uint32_t check;
  chatMsgT chatMsg[CHAT_MRTC];
} rtcMemT;

// eeprom memory as infopage
// keep data after power off
typedef struct { 
  uint32_t check;
  char info[INFO_MLEN];
} eepromMemT;

void setup() {

  // builtin led
  // normaly turned off all the time
  pinMode(LED_BUILTIN,OUTPUT);
  digitalWrite(LED_BUILTIN,HIGH);
    
  // RTC restore after reset
  // use data, if magic DE49 was set
  rtcMemT rtcMem;
  system_rtc_mem_read(
    ESP_RTCADR, &rtcMem, sizeof(rtcMem)
  );
  if (rtcMem.check == 0xDE49) {
    for (int i=0;i<CHAT_MRTC;i++){
      chatMsg[i]=rtcMem.chatMsg[i];
    }
  } else {
    digitalWrite(LED_BUILTIN,LOW);
  }
 
  // serial debug
  // debug state, memory usage, etc
  Serial.begin(115200);
  Serial.println(F("boot"));
  delay(50);

  // wifi: don't write setup to flash
  // increase life time of the mcu
  WiFi.persistent(false); 
    
  // wifi: dynamic SSID by physical MAC
  // seperare suffix for each mesh node
  uint8_t apMAL = WL_MAC_ADDR_LENGTH;
  uint8_t apMAC[apMAL];
  WiFi.softAPmacAddress(apMAC);
  apSSID += (apMAC[apMAL-2]<16?"0":"");
  apSSID += String(apMAC[apMAL-2],HEX);
  apSSID += (apMAC[apMAL-1]<16?"0":"");
  apSSID += String(apMAC[apMAL-1],HEX);
  apSSID.toUpperCase(); 
  apSSID = String(apName)+apSSID;
  apSSID += F(" (http://");
  apSSID += String(apHost);
  apSSID += F(".local)");

  // wifi: fast network scan (1 channel)
  // reduce scan time 2400ms to 150ms
  wifi_country_t apCountry = {
    .cc = "EU",          // country
    .schan = AP_CHANNEL, // start chnl
    .nchan = 1,          // num of chnl
    .policy = WIFI_COUNTRY_POLICY_MANUAL 
  };
  wifi_set_country(&apCountry);

  // wifi: cap.portal popup public ip
  // IPAddress apIP(172,mac,mac,1);
  uint8_t apIP2=apMAC[apMAL-2]%224+32;
  uint8_t apIP3=apMAC[apMAL-1];
  IPAddress apIP(172,apIP2,apIP3,1);
  IPAddress apNetMsk(255,255,255,0);
  
  // wifi: start access point
  // call twice to set the channel
  WiFi.mode(WIFI_AP_STA);
  WiFi.setPhyMode(AP_PHYMOD);
  WiFi.setOutputPower(AP_POWER);
  WiFi.hostname(apHost);
  WiFi.softAPConfig(apIP,apIP,apNetMsk);
  WiFi.softAP(apSSID,apPass,AP_CHANNEL);
  if (String(apPass).length()==0) {
    WiFi.softAP(apSSID);
  }
    
  // wifi: increase ap clients
  // increase from 4 to 8 connections
  struct softap_config apConfig;
  wifi_softap_get_config(&apConfig);
  apConfig.max_connection = AP_MAXCON;
  wifi_softap_set_config(&apConfig);

  // dns: spoofing for http
  dnsServer.start(53, "*", apIP);

  // dns: multicast DNS web.local
  // user friendly promotion url
  if (MDNS.begin(apHost)) {
    MDNS.addService("http","tcp",80);
  }
  
  // http: webserver
  // map path and handler
  urlHome = F("http://");
  urlHome += WiFi.softAPIP().toString();
  urlChatRefresh += urlHome;
  urlInfoRefresh += urlHome;
  urlHome += F("/home");
  urlChatRefresh += F("/chat");
  urlInfoRefresh += F("/info");
  httpServer.onNotFound(onHttpToHome);
  httpServer.on("/home",onHttpHome);
  httpServer.on("/chat",onHttpChat);
  httpServer.on("/chatf",onHttpChatFrm);
  httpServer.on("/chata",onHttpChatAdd);
  httpServer.on("/chatr",onHttpChatRaw);
  httpServer.on("/info",onHttpInfo);
  httpServer.on("/infof",onHttpInfoFrm);
  httpServer.on("/infos",onHttpInfoSet);
  httpServer.on("/cli",onHttpCli);
  httpServer.begin();

  // ota: update
  // update firmware via wifi
  ArduinoOTA.setPort(8266);
  /* ArduinoOTA.setPassword(
    (const char *)"Chat!b0x"); 
   * dont work with arduinoDroid */
  ArduinoOTA.onStart([]()
    {Serial.println("OTA Start");});
  ArduinoOTA.onEnd([]()
    {Serial.println("\nOTA End");});
  /* ArduinoOTA.begin();
   * enabled via CLI login */
  
  delay(100);
  digitalWrite(LED_BUILTIN,HIGH);
}

/* --- main --- */

// Flags
// application state and flags
struct {
  uint8_t enableCliCommand:1;
  uint8_t enableOtaUpdate:1;
  uint8_t beginOtaServer:1;
  uint8_t disconnectClients:1;
  uint8_t builtinLedMode:2;
  uint8_t meshMode:2;
  uint8_t reserve:4;
} flag = {0,0,0,0,0,0};

// timer
// handle multiple tasks on single cpu
uint32_t uptime=0;
uint32_t uptimeLast=0;

void loop() {
  uptimeLast = uptime;
  uptime = millis();
  if (uptime!=uptimeLast) {
    // once per millisecond
    doLed();
    doDebug();
    doServer();
    doDisconnect();
    doReboot();
    doMesh();
    doUpdateChatAge();
    doSpike();
  }
  yield();
}

void doServer() {
  // execute different server handlers
  // dns   domain name
  // http  webserver
  // ota   over the air update
  if (uptime%11==0) {
    // reduce the number of responces
    // 9: 111 Req/s ~  5.4 KB/Req
    // 11: 90 Req/s ~  6.6 KB/Req
    // 13: 76 Req/s ~  7.7 KB/Req
    // 17: 58 Req/s ~ 10.3 KB/Req
    dnsServer.processNextRequest();
    yield();
    httpServer.handleClient();
    yield();
    if (flag.enableOtaUpdate == 1) {
      if (flag.beginOtaServer == 0) {
        flag.beginOtaServer = 1;
        ArduinoOTA.begin();
      } else {
        ArduinoOTA.handle();
      }
      yield();
    }
  }
}

/* --- powerbank --- */

uint16_t spikeTimer=0;
uint8_t spikeMode=LOW;
    
void doSpike() {
  // current spike for powerbank
  // some pb turn off on low current
  if(SPIKE_PWON>0){
    spikeTimer++;
    if(spikeTimer>=SPIKE_PWOFF
    && spikeMode==LOW){
      spikeTimer=0;
      spikeMode=HIGH;
      //--- draw extra current
      pinMode(SPIKE_IOD1,OUTPUT);
      pinMode(SPIKE_IOD2,OUTPUT);
      digitalWrite(SPIKE_IOD1,HIGH);
      digitalWrite(SPIKE_IOD2,HIGH);
      //---
    }
    if(spikeTimer>=SPIKE_PWON
    && spikeMode==HIGH){
      spikeTimer=0;
      spikeMode=LOW;
      //--- turn off extra current
      digitalWrite(SPIKE_IOD1,LOW);
      digitalWrite(SPIKE_IOD2,LOW); 
      pinMode(SPIKE_IOD1,INPUT_PULLUP);
      pinMode(SPIKE_IOD2,INPUT_PULLUP);
      //---
    }
  }
}

/* --- resilience --- */

uint8_t disconnectTimer=0;

void doDisconnect() {
  // disconnect all on max connected
  // enable other nodes to connect
  if (uptime%107==0) {
    // wait 7 seconds before disconnect
    // to ignore short mesh connections
    if (WiFi.softAPgetStationNum()
        == AP_MAXCON) {
      if (disconnectTimer==0) {
        // start timer 7s
        disconnectTimer=10*AP_MAXTOUT;
      }
    } else {
      // stop timer
      disconnectTimer=0;
    }
    if (disconnectTimer>1) {
      // timer countdown 9.3ms(1000/107)
      disconnectTimer--;
    } else if (disconnectTimer==1) {
      // end timer, if max client > 7s
      disconnectTimer=0;
      flag.disconnectClients=1;
    }
  }
  if (flag.disconnectClients==1) {
    // disconnect all clients
    // waiting clients able to connect
    flag.disconnectClients=0;
    WiFi.softAPdisconnect(false);
    WiFi.softAP(
      apSSID,apPass,AP_CHANNEL
    );
    yield();
    if (String(apPass).length()==0) {
      WiFi.softAP(apSSID);
    }
    yield();
    struct softap_config apConfig;
    wifi_softap_get_config(&apConfig);
    apConfig.max_connection = AP_MAXCON;
    wifi_softap_set_config(&apConfig);
    yield();
  }
}

void doReboot() {
  // reboot before out of memory freeze
  // workaround to keep service alive
  if (uptime%103==0) {
    if (system_get_free_heap_size()
        < ESP_OUTMEM) {
      doBackup();
      delay(100);
      WiFi.softAPdisconnect(true);
      delay(200);
      yield();
      ESP.restart();
      delay(200);
      yield();
    }
  }
}

/* --- debugging --- */

void doLed() {
  switch(flag.builtinLedMode) {
    case LED_OFF:
      digitalWrite(LED_BUILTIN,HIGH);
      break;
    case LED_ON:
      digitalWrite(LED_BUILTIN,LOW);
      break;
  }
  yield();
}

uint8_t requests=0;

void doDebug() {
  // debug system state via serial
  // also provided by CLI via WiFi
  if (uptime%1009==0) {
    // serial print only every 1s 
    // to reduce mcu workload
    rtcMemT rtcMem;
    system_rtc_mem_read(
      ESP_RTCADR,&rtcMem,sizeof(rtcMem)
    );
    // timestamp (milliseconds)
    Serial.print(uptime);
    Serial.print(F("ms,"));
    // free DRAM memory (bytes)
    Serial.print(
      ESP.getFreeHeap());
    Serial.print(F("B,"));
    // http request per second
    Serial.print(requests);
    Serial.print(F("R,"));
    // wifi STA connections
    Serial.print(
      WiFi.softAPgetStationNum());    
    Serial.print(F("C,"));
    // max conn timer (countdown)
    Serial.print(disconnectTimer);    
    Serial.print(F("D,"));
    // rtc backup flag (check)
    Serial.print(rtcMem.check);    
    Serial.print(F("T"));
    Serial.println();
    // restart kpi
    requests=0;
    yield();
  }
}

/* --- chat data --- */

String getChat(byte ctype,byte send) {
  // create chat output for html or text
  // option direct stream to http client
  String data="",temp,ms="",mr="",mb="";
  uint8_t count=0;
  uint8_t apMAL = WL_MAC_ADDR_LENGTH;
  uint8_t apMAC[apMAL];
  // node mac
  WiFi.softAPmacAddress(apMAC);
  if (ctype==CTYPE_TEXT){
    data += "\nN"+String(
        apMAC[apMAL-2]*256+
        apMAC[apMAL-1]);
  }
  // count messages
  for (uint8_t i=0;i<CHAT_MARY;i++) {
    if (chatMsg[i].data[0]!=0) {
      // count++;
      count = count+1;
    }
  }
  if (ctype==CTYPE_TEXT){
    data += "\nC"+String(count);
  }
  // messages
  for (uint8_t i=0;i<CHAT_MARY;i++) {
    if (chatMsg[i].data[0]!=0) {
      temp = String(chatMsg[i].data);
      if (chatMsg[i].slen>0) {
        ms = temp.\
          substring(0,chatMsg[i].slen);
      }
      if (chatMsg[i].rlen>0) {
        mr = temp.\
          substring(chatMsg[i].slen,
                chatMsg[i].slen+
                chatMsg[i].rlen);
      }
      if (chatMsg[i].mlen>0) {
        mb = temp.\
          substring(chatMsg[i].slen+
                chatMsg[i].rlen,
                chatMsg[i].slen+
                chatMsg[i].rlen+
                chatMsg[i].mlen);
      }
      if (ctype==CTYPE_HTML){
        data += ms;
        if (chatMsg[i].rlen>0) {
          data += "@";
          data += mr;
        }
        if (chatMsg[i].slen>0
         || chatMsg[i].rlen>0) {
          data += ": ";
        }
        data += mb;
        data += "<br><br>";
      }
      if (ctype==CTYPE_TEXT){
        data += "\nN"+String(
          chatMsg[i].node[0]*256+
          chatMsg[i].node[1]);
        data += "\nI"+String(
          chatMsg[i].id);
        data += "\nA"+String(
          chatMsg[i].age);
        data += "\nS"+ms;
        data += "\nR"+mr;
        data += "\nM"+mb;
      }
      ms=""; mr=""; mb="";
      if (send==1 && data.length()>0) {
        httpServer.sendContent(data);
        data="";
      }
    }
  }
  if (send==1 && data.length()>0) {
    httpServer.sendContent(data);
    data="";
  }
  return data;
}
    
void addChat(
  String ms, 
  String mr,
  String mb) {
  // append a new chat to the buffer
  // the oldest message will deleted   

  // shift item to get free msg[0]
  if (chatMsg[0].data[0]!=0) {
    for (uint8_t i=CHAT_MARY-1;i>0;i--){
      chatMsg[i] = chatMsg[i-1];
    }
  }
  // add item
  chatMsg[0] = {0};
  // node mac
  uint8_t apMAL = WL_MAC_ADDR_LENGTH;
  uint8_t apMAC[apMAL];
  WiFi.softAPmacAddress(apMAC);
  chatMsg[0].node[0]=apMAC[apMAL-2];
  chatMsg[0].node[1]=apMAC[apMAL-1];
  // find newest messages of this node
  chatMsg[0].age = CHAT_MAXAGE;
  for (int i=0;i<CHAT_MARY;i++) {
    if (chatMsg[0].node[0]==
          chatMsg[i].node[0]
     && chatMsg[0].node[1]==
          chatMsg[i].node[1]
     && chatMsg[0].age>chatMsg[i].age){
     chatMsg[0].age = chatMsg[i].age;    
    }
  }
  // find next id for this node
  chatMsg[0].id = 0;
  for (int i=0;i<CHAT_MARY;i++) {
    if (chatMsg[0].node[0]==
          chatMsg[i].node[0]
     && chatMsg[0].node[1]==
          chatMsg[i].node[1]
     && chatMsg[0].age==chatMsg[i].age
     && chatMsg[0].id<chatMsg[i].id){
     chatMsg[0].id = chatMsg[i].id;    
    }
  }
  if (chatMsg[0].id >= CHAT_MAXID) {
    chatMsg[0].id=0;
  }
  // chatMsg[0].id++;
  chatMsg[0].id = chatMsg[0].id+1;
  chatMsg[0].age = 0;
  chatMsg[0].slen = ms.length();
  chatMsg[0].rlen = mr.length();
  chatMsg[0].mlen = mb.length();
  String data=ms+mr+mb;
  data=data.substring(0,CHAT_MLEN);
  data.toCharArray(
    chatMsg[0].data,data.length()+1
  );
  data="";
  ms="";
  mr="";
  mb="";
  doBackup();
}

uint32_t updateLast = 0;

void doUpdateChatAge() {
  // increase the age of each message
  // workaround: uptime mod 1013
  uint32_t periode,age_b,age_1,age_2;
  if (uptime%1013==0) {
    // the is a bug with millis() and
    // void sometimes called multiple
    periode=millis()-updateLast;
    if (periode >= 1000) {
      periode -= 1000;
      updateLast = millis()+periode;
      for (int i=0;i<CHAT_MARY;i++) {
        if (chatMsg[i].data[0]!=0 &&
          chatMsg[i].age<CHAT_MAXAGE-1){
          // bug: chatMsg[i].age++
          chatMsg[i].age =
            chatMsg[i].age+1;
        }
        yield();
      }
    }
  }
}

void doBackup() {
  // backup data to limited rtc memory
  // dont use flash or eeprom for chat
  rtcMemT rtcMem = {0};
  rtcMem.check = 0xDE49;
  for (int i=0;i<CHAT_MRTC;i++){
    rtcMem.chatMsg[i]=chatMsg[i];
  }
  system_rtc_mem_write(
    ESP_RTCADR,&rtcMem,sizeof(rtcMem)
  );
  yield();
}

/* --- mesh network --- */

struct {
  uint32_t timerScan;
  uint32_t timerJoin;
  uint32_t timer1Delay;
  uint32_t timer2Ready;
  uint8_t  mode;
  uint8_t  wifi;
} mesh = {0};

void doMesh() {
  // async mesh network workflow
  // scan net, connect, download, merge
  uint8_t i;
  if (mesh.mode == MESH_INIT) {
    // initiate scan after re-boot
    mesh.mode = MESH_SCAN;
    mesh.timerScan = 0;
    Serial.println("mesh.init V34");
  } else 
  if (mesh.mode == MESH_SCAN
   && millis()-mesh.timerScan
        > MESH_DINIT) {
    // scan network start async
    mesh.mode = MESH_SCANE;
    mesh.timerScan = millis();
    WiFi.scanNetworks(true);
  } else 
  if (mesh.mode == MESH_SCANE
   && millis()-mesh.timerScan
        > MESH_TSCAN) {
    // scan network end
    mesh.wifi = WiFi.scanComplete();
    if (mesh.wifi == 0) {
      // no networks, scan again
      mesh.mode = MESH_SCAN;
      mesh.timerScan = millis();
      WiFi.scanDelete();
    } else {
      // found networks, connect
      mesh.mode = MESH_JOIN;
      mesh.timerJoin = millis();
    }
  } else 
  if (mesh.mode == MESH_JOIN
   && millis()-mesh.timerJoin
        > MESH_DJOIN) {
    // start connect to wifi
    i = mesh.wifi-1;
    if (mesh.wifi==0) {
      // end of list, scan again
      mesh.mode = MESH_SCAN;
      mesh.timerScan = millis();
      WiFi.scanDelete();
    } else
    if (WiFi.channel(i)!=AP_CHANNEL
    || !String(WiFi.SSID(i).c_str()).\
         startsWith(apName)) {  
      // ignore other channel or wifi
      mesh.mode = MESH_JOIN;
      mesh.timerJoin = millis();
      mesh.wifi--;
    } else {
      // join same mesh network
      mesh.mode = MESH_JOINE;
      mesh.timerJoin = millis();
      mesh.timer2Ready = millis();
      mesh.wifi--;
      if (WiFi.encryptionType(i) ==
            ENC_TYPE_NONE) {
        WiFi.begin(WiFi.SSID(i));
      } else {
        WiFi.begin(WiFi.SSID(i),apAuth);
      }
    }
  } else 
  if (mesh.mode == MESH_JOINE
   && millis()-mesh.timerJoin<MESH_TJOIN
   && millis()-mesh.timer2Ready
        > MESH_DNOOP) {
    // detect connection before timeout
    mesh.timer2Ready = millis();
    if (WiFi.status()==WL_CONNECTED) {
      mesh.timerJoin -= MESH_TJOIN;
    }
  } else 
  if (mesh.mode == MESH_JOINE
   && millis()-mesh.timerJoin
        > MESH_TJOIN) {
    // connected or timeout
    if (WiFi.status()==WL_CONNECTED) {
      // webserver mesh node
      IPAddress meshIP = IPAddress(
        WiFi.localIP()[0],
        WiFi.localIP()[1],
        WiFi.localIP()[2],
        1
      );
      // url chat raw data
      String url = F("http://");
      url += meshIP.toString();
      url += F("/chatr");
      Serial.println(url);
      // http request
      WiFiClient wifiClient; 
      HTTPClient httpClient;      
      httpClient.begin(
        wifiClient, 
        url.c_str()
      );
      httpClient.setTimeout(MESH_THTTP);
      // http response
      int httpRC = httpClient.GET();
      yield();
      if (httpRC>0) {
        Serial.println("http.ok");
        // use streaming interface
        // to handle line by line
        StreamString streamHttp;        
        httpClient.writeToStream(
          &streamHttp);
        String line;
        doMeshResponseBegin();
        while(streamHttp.available()>0){
          line = streamHttp.\
            readStringUntil('\n');
          yield();
          doMeshResponseData(line);
          yield();
        }
        doMeshResponseEnd();
        // Serial.println(
        //  streamHttp.readString());
        // Serial.println(
        //   httpClient.getString());
      } else {
        Serial.println("http.error");
      }
      httpClient.end();
      WiFi.disconnect();
    }
    if (mesh.wifi == 0) {
      // end of list, scan again
      mesh.mode = MESH_SCAN;
      mesh.timerScan = millis();
      WiFi.scanDelete();
    } else {
      // connect next
      mesh.mode = MESH_JOIN;
      mesh.timerJoin = millis();
    }
  } 
  yield();
}

chatMsgT meshMsg;
String meshMsgData;

void doDebugMeshChat() {
  // debug chat structur
  String data="",temp,ms="",mr="",mb="";
  temp = String(meshMsg.data);
  if (meshMsg.slen>0) {
    ms = temp.\
      substring(0,meshMsg.slen);
  }
  if (meshMsg.rlen>0) {
    mr = temp.\
      substring(meshMsg.slen,
                meshMsg.slen+
                meshMsg.rlen);
  }
  if (meshMsg.mlen>0) {
    mb = temp.\
      substring(meshMsg.slen+
                meshMsg.rlen,
                meshMsg.slen+
                meshMsg.rlen+
                meshMsg.mlen);
  }
  data = "\nN"+String(
    meshMsg.node[0]*256+
    meshMsg.node[1]);
  data += "\nI"+String(
    meshMsg.id);
  data += "\nA"+String(
    meshMsg.age);
  data += "\nS"+ms;
  data += "\nR"+mr;
  data += "\nM"+mb;
  Serial.println(data);
}

void doMergeMeshChat() {
  // sync between mesh nodes
  boolean exists=false;
  uint8_t position=CHAT_MARY;
  // search message exists / position
  for (int i=0;i<CHAT_MARY;i++) {
    if (meshMsg.node[0]==
          chatMsg[i].node[0]
     && meshMsg.node[1]==
          chatMsg[i].node[1]
     && meshMsg.data[1]==
          chatMsg[i].data[1]
     && meshMsg.id==chatMsg[i].id
     && meshMsg.slen==chatMsg[i].slen
     && meshMsg.rlen==chatMsg[i].rlen
     && meshMsg.mlen==chatMsg[i].mlen){
      // dont store existing messages
      exists=true;
    }
    if (position==CHAT_MARY
     && meshMsg.age<=chatMsg[i].age
     && chatMsg[i].data[0]!=0) {
      // sort position by age
      position=i;
    }
    if (position==CHAT_MARY
     && chatMsg[i].data[0]==0) {
      // use next empty struct
      position=i;
    }
    yield();
  }
  // insert
  if (exists==false 
   && position<CHAT_MARY) {
    if (chatMsg[position].data[0]!=0){
      // shift items to the end
      for (uint8_t i=CHAT_MARY-1;
            i>position;i--){
        chatMsg[i] = chatMsg[i-1];
        yield();
      }
    } 
    // insert item ordered by age
    chatMsg[position]=meshMsg;
  }
}

void doMeshResponseBegin() {
  // process at the begin of http result
}

void doMeshResponseData(String line) {
  // parse each line of http response
  // the first char contains linetype
  char lineType;
  if (line.length()==0) {
    lineType = 0;
  } else {
    lineType = line.charAt(0);
  }
  int num;
  switch(lineType) {
    case 'N': // node = struc begin
      meshMsg = {0};
      meshMsgData = "";
      num = line.substring(1).toInt();
      meshMsg.node[1] = num%256;
      num -= meshMsg.node[1];
      num /= 256;
      meshMsg.node[0] = num%256;
      break;
    case 'I': // id
      num = line.substring(1).toInt();
      meshMsg.id = num;
      break;
    case 'A': // age
      num = line.substring(1).toInt();
      meshMsg.age = num;
      break;
    case 'S': // sender
      meshMsg.slen = line.length()-1;
      if (line.length()>1){
        meshMsgData+=line.substring(1);
      }
      break;
    case 'R': // receiver
      meshMsg.rlen = line.length()-1;
      if (line.length()>1){
        meshMsgData+=line.substring(1);
      }
      break;
    case 'M': // message / struc end
      meshMsg.mlen = line.length()-1;
      if (line.length()>1){
        meshMsgData+=line.substring(1);
      }
      meshMsgData.toCharArray(
        meshMsg.data,
        meshMsgData.length()+1
      );
      meshMsgData="";
      // handle the struc
      doMergeMeshChat();
      // doDebugMeshChat();
      break;
  }
  // Serial.print("line:");
  // Serial.println(line);
}

void doMeshResponseEnd() {
  // process at the end of http result
}
    
/* --- http page --- */

uint32_t httpTimeStart;

void doHttpStreamBegin(byte ctype) {
  // template http begin / dynamic html
  // without content-length
  httpTimeStart = millis();
  httpServer.sendHeader(
    F("Connection"), F("close")
  );
  httpServer.setContentLength(
    CONTENT_LENGTH_UNKNOWN
  );
  switch(ctype){
    case CTYPE_HTML:
      httpServer.send(
        200,F("text/html"),F("")
      );
      break;
    case CTYPE_TEXT:
      httpServer.send(
        200,F("text/plain"),F("")
      );
      break;
  }
  yield();
}

void doHttpStreamEnd() {
  // template http end / dynamic html
  // read until client disconnect
  httpServer.sendContent(F(""));
  while (
    httpServer.client().available()) {
    httpServer.client().read();
    yield();
    if (millis()-httpTimeStart>500) {
      break;
    }
  }
  yield();
  httpServer.client().stop();
  requests++;
  yield();
}

void doHtmlPageHeader() {
  // template html / header
  // zoom-in by meta viewport
  httpServer.sendContent(
    F("<html><head><meta name='viewport' content='width=device-width, initial-scale=1' />"));
}

void doHtmlPageBody() {
  // template html / body
  // dark colors to reduce power usage
  httpServer.sendContent(
    F("</head><body bgcolor=#003366 text=#FFFFCC link=#66FFFF vlink=#66FFFF alink=#FFFFFF>"));
}

void doHtmlPageFooter() {
  // template html / footer
  httpServer.sendContent(
    F("</body></html>"));
}

/* --- http handler --- */

String maskHttpArg(String id,byte lf) {
  // get CGI request parameter
  // disable line feed and html tags
  String prm = httpServer.arg(id);
  prm.replace("<","&lt;");
  prm.replace(">","&gt;");
  if (lf == 0){
    prm.replace("\r","\n");
    prm.replace("\n"," ");
  }
  prm.replace("  "," ");
  return prm;
}

void onHttpToHome() {
  // redirect undefined url to home
  // static and small size
  httpServer.sendHeader(
    F("Location"), urlHome
  );
  httpServer.send(
    302, F("text/plain"), F("")
  );
  requests++;
  yield();
}

void onHttpHome() {
  // landing page redirected to
  // static and small size
  httpServer.send(
    200, F("text/html"), F("<html><head><meta name='viewport' content='width=device-width, initial-scale=1' /></head><body bgcolor=#003366 text=#FFFFCC link=#66FFFF vlink=#66FFFF alink=#FFFFFF><h1>Chatbox</h1><hr><br>Welcome to the Chatbox hotspot. You can communicate anonymously with your neighbors through this access point. Please behave decently!<br><br>Willkommen beim WiFi Chat. Du kannst &uuml;ber den Hotspot anonym mit deinen Nachbarn kommunizieren. Bitte verhalte dich zivilisiert!<br><br><a href=/chat>OK - accept (akzeptiert)</a></body></html>")
  );
  requests++;
  yield();
}

void onHttpChat() {
  // chat messages with autorefresh
  // 8sec interval to reduce requests 
  httpServer.sendHeader(
    F("Refresh"), F("8")
  );
  doHttpStreamBegin(CTYPE_HTML);
  doHtmlPageHeader();
  doHtmlPageBody();
  httpServer.sendContent(
    F("<h1>Chatbox</h1><hr><a href=/chatf>create (erstellen)</a>&nbsp; - &nbsp;<a href=/info>Info</a><hr><br>"));
  getChat(CTYPE_HTML,1);
  doHtmlPageFooter();
  doHttpStreamEnd();
}

void onHttpChatFrm() {
  // create new char form
  // number of chars by javascript
doHttpStreamBegin(CTYPE_HTML);
  doHtmlPageHeader();
  httpServer.sendContent(
    F("<script>function onInp(){var df=document.forms.mf,cl="));
  httpServer.sendContent(String(CHAT_MLEN));
  httpServer.sendContent(
    F("-(df.ms.value.length+df.mr.value.length+df.mb.value.length); document.getElementById('mn').innerText=cl;}</script>")); 
  doHtmlPageBody();
  httpServer.sendContent(
    F("<h1>Chatbox</h1><hr><a href=/chat>cancle (abbrechen)</a><hr><br><form name=mf action=/chata method=POST>Sender (Absender):<br><input type=text name=ms maxlength=16 onChange=onInp() onkeyup=onInp()><br><br>Receiver (Empf&auml;nger):<br><input type=text name=mr maxlength=16 onChange=onInp() onkeyup=onInp()><br><br>Message (Nachricht):<br><textarea name=mb rows=3 cols=22 maxlength=")); httpServer.sendContent(String(CHAT_MLEN));
  httpServer.sendContent(
    F(" onChange=onInp() onkeyup=onInp()></textarea><br><br><input type=submit value=senden> <span id=mn></span></form>"));
  doHtmlPageFooter();
  doHttpStreamEnd();
}

void onHttpChatAdd() {
  // handle new chat post request
  // autoredirect after 4 seconds
  httpServer.sendHeader(
    F("Refresh"), urlChatRefresh
  );
  String ms = maskHttpArg("ms",0);
  if (ms.length()>0) {
    ms.replace("~","-");
    ms = ms.substring(0,16);
  }
  String mr = maskHttpArg("mr",0);
  if (mr.length()>0) {
    mr = mr.substring(0,16);
  }
  String mb = maskHttpArg("mb",0);
  mb=mb.substring(
    0,
    CHAT_MLEN-ms.length()-mr.length());
  if ((ms.length()+
       mr.length()+
       mb.length())>0) {
    addChat(ms,mr,mb);
  }
  String html="";
  html+=F("<html><head><meta name='viewport' content='width=device-width, initial-scale=1' /></head><body bgcolor=#003366 text=#FFFFCC link=#66FFFF vlink=#66FFFF alink=#FFFFFF><h1>Chatbox</h1><hr><a href=/chat>next (weiter)</a><hr><br>");
  html+=ms;
  if (mr.length()>0) {
    html+=F("@");
    html+=mr;
  }
  html+=F(": ");
  html+=mb;
  html+=F("</body></html>");
  httpServer.send(
    200, F("text/html"),html
  );
  html=""; ms=""; mr=""; mb="";
  requests++;
  yield();
}

void onHttpChatRaw() {
  // raw chat messages for mesh nodes
  // format is linetype with data
  doHttpStreamBegin(CTYPE_TEXT);
  httpServer.sendContent(
    F("V1\nT"));
  httpServer.sendContent(
    String(millis()));
  getChat(CTYPE_TEXT,1);
  httpServer.sendContent(
    F("\nX\n"));
  doHttpStreamEnd();
}

void onHttpInfo() {
  // info page
  doHttpStreamBegin(CTYPE_HTML);
  doHtmlPageHeader();
  doHtmlPageBody();
  httpServer.sendContent(
    F("<h1>Chatbox</h1><hr><a href=/infof>update (&auml;ndern)</a>&nbsp; - &nbsp;<a href=/chat>Chat</a><hr><br><pre>"));
  eepromMemT eepromMem = {0};
  EEPROM.begin(sizeof(eepromMem));
  EEPROM.get(0, eepromMem);
  EEPROM.end();
  if (eepromMem.check==0xDE49){
    httpServer.sendContent(
      String(eepromMem.info)
    );
  } else {
    httpServer.sendContent(
      F("..."));
  }
  httpServer.sendContent(
    F("</pre>"));
  doHtmlPageFooter();
  doHttpStreamEnd();
}

void onHttpInfoFrm() {
  // change info form
  // number of chars by javascript
doHttpStreamBegin(CTYPE_HTML);
  doHtmlPageHeader();
  httpServer.sendContent(
    F("<script>function onInp(){var df=document.forms.mf,cl="));
  httpServer.sendContent(String(INFO_MLEN-1));
  httpServer.sendContent(
    F("-(df.ib.value.length); document.getElementById('in').innerText=cl;}</script>")); 
  doHtmlPageBody();
  httpServer.sendContent(
    F("<h1>Chatbox</h1><hr><a href=/info>cancle (abbrechen)</a><hr><br><form name=mf action=/infos method=POST>Password:<br><input type=password name=ip maxlength=16 onChange=onInp() onkeyup=onInp()><br><br>Info:<br><textarea name=ib rows=10 cols=45 maxlength=")); httpServer.sendContent(String(INFO_MLEN-1));
  httpServer.sendContent(
    F(" onChange=onInp() onkeyup=onInp()>"));
  eepromMemT eepromMem = {0};
  EEPROM.begin(sizeof(eepromMem));
  EEPROM.get(0, eepromMem);
  EEPROM.end();
  if (eepromMem.check==0xDE49){
    httpServer.sendContent(
      String(eepromMem.info)
    );
  }
  httpServer.sendContent(
    F("</textarea><br><br><input type=submit value=save> <span id=in></span></form>"));
  doHtmlPageFooter();
  doHttpStreamEnd();
}

void onHttpInfoSet() {
  // handle set info post request
  // autoredirect after 4 seconds
  httpServer.sendHeader(
    F("Refresh"), urlInfoRefresh
  );
  String ip = maskHttpArg("ip",0);
  if (ip.length()>0) {
    ip = ip.substring(0,16);
  }
  String ib = maskHttpArg("ib",1);
  ib=ib.substring(0,INFO_MLEN);
  if (ip.length()>0
   && ip==String(cliPwd)) {
    // save
    eepromMemT eepromMem = {0};
    EEPROM.begin(sizeof(eepromMem));
    eepromMem.check=0xDE49;
    ib.toCharArray(
      eepromMem.info,ib.length()+1
    );
    EEPROM.put(0, eepromMem);
    EEPROM.commit();
    EEPROM.end();
  } else { 
    ib = F("error: wrong password!");
  }
  String html="";
  html+=F("<html><head><meta name='viewport' content='width=device-width, initial-scale=1' /></head><body bgcolor=#003366 text=#FFFFCC link=#66FFFF vlink=#66FFFF alink=#FFFFFF><h1>Chatbox</h1><hr><a href=/info>next (weiter)</a><hr><br><pre>");
  html+=ib;
  html+=F("</pre></body></html>");
  httpServer.send(
    200, F("text/html"),html
  );
  html=""; ip=""; ib="";
  requests++;
  yield();
}

void onHttpCli() {
  // admin command-line-interface
  // and debug information
  rtcMemT rtcMem;
  system_rtc_mem_read(
    ESP_RTCADR, &rtcMem, sizeof(rtcMem)
  );
  // readme
  String text= F(
    "Version: 20221026-1351\n"
    "/cli?cmd=login-password\n"
    "/cli?cmd=logoff\n"
    "/cli?cmd=restart\n"
    "/cli?cmd=ota-on\n"
    "/cli?cmd=ota-off\n"
    "\n"
    "OTA-Update\n\n"
    "1. wifi connect\n"
    "2. disable firewall\n"
    "3. login-password\n"
    "4. ota-on (led on)\n"
    "5. arduinodroid upload wifi\n"
    "   server: web.local\n"
    "   port: 8266\n"
    "6. on-error use restart\n"
    "\n"
    "ESP-Status\n"
  );
  // debug
  text+="\nUptime:"+String(uptime);
  text+="\nClients:"+String(
    WiFi.softAPgetStationNum());
  text+="\nRequests:"+String(requests);
  text+="\nMemory:"+String(
    system_get_free_heap_size());
  text+="\nRtcCheck:"+String(
    rtcMem.check);
  text+="\nCliLogin:"+String(
    flag.enableCliCommand);
  text+="\nCliOta:"+String(
    flag.enableOtaUpdate);
  httpServer.send(
      200,F("text/plain"),text
  );
  text="";
  // cli
  String cmd=httpServer.arg("cmd");
  if (cmd == String(cliPwd)
    // F("login-password")
    ) {
    flag.enableCliCommand=1;
  }
  if (flag.enableCliCommand==1) {
    if (cmd == F("logoff")) {
      flag.enableCliCommand=0;
      flag.enableOtaUpdate = 0;
      flag.builtinLedMode=LED_OFF;
    }
    if (cmd == F("restart")) {
      ESP.restart();
    }
    if (cmd ==  F("ota-off")) {
      flag.enableOtaUpdate = 0;
      flag.builtinLedMode=LED_OFF;
    }
    if (cmd ==  F("ota-on")) {
      flag.enableOtaUpdate = 1;
      flag.builtinLedMode=LED_ON;
    }
  }
  cmd="";
  requests++;
  yield();
}