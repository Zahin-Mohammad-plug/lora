// ===== TX: Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262) =====
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include "HT_SSD1306Wire.h"
#include <WiFi.h>
#include <WebServer.h>
#include <RadioLib.h>

// Heltec V3 SX1262 pins
SX1262 radio = new Module(/*cs*/8, /*irq(DIO1)*/14, /*rst*/12, /*busy*/13);

// Wi-Fi creds
const char* SSID = "logi_mouse_02";
const char* PASS = "gg1234567";

// OLED
static SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED,
                           GEOMETRY_128_64, RST_OLED);
void VextON()  { pinMode(Vext, OUTPUT); digitalWrite(Vext, LOW);  }
void VextOFF() { pinMode(Vext, OUTPUT); digitalWrite(Vext, HIGH); }

WebServer server(80);

// Metrics
uint32_t pktSent = 0, pktAck = 0;
int lastAckRSSI = 0;
float lastAckSNR = 0.0;
uint32_t lastRTTms = 0;
int lastTxState = 0;           // 0 == OK
const int   TX_POWER_DBM = 17;
const float FREQ_MHZ     = 915.0;
const int   SF           = 10;
const float BW_KHZ       = 125.0;
const int   CRx          = 5;  // 4/5

void setRadioParams() {
  radio.setSpreadingFactor(SF);
  radio.setBandwidth(BW_KHZ);
  radio.setCodingRate(CRx);
  radio.setSyncWord(0x34); // Changed from 0x12
}

// Message history
struct MessageLog {
  String msg;
  bool acked;
  bool sent;  // true if transmit succeeded
  uint32_t timestamp;
};
const int MAX_MSG_HISTORY = 20;
MessageLog msgHistory[MAX_MSG_HISTORY];
int msgHistoryCount = 0;

void addToHistory(const String& msg, bool sent, bool acked) {
  if (msgHistoryCount >= MAX_MSG_HISTORY) {
    // Shift array left
    for (int i = 1; i < MAX_MSG_HISTORY; i++) {
      msgHistory[i-1] = msgHistory[i];
    }
    msgHistoryCount = MAX_MSG_HISTORY - 1;
  }
  msgHistory[msgHistoryCount].msg = msg;
  msgHistory[msgHistoryCount].sent = sent;
  msgHistory[msgHistoryCount].acked = acked;
  msgHistory[msgHistoryCount].timestamp = millis();
  msgHistoryCount++;
}

void drawStatus(const String& ip, const String& stat) {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, "TX @ 915 MHz");
  display.drawString(0, 12, "WiFi: " + WiFi.SSID());
  
  // Show WiFi signal strength (RSSI)
  int wifiRSSI = WiFi.RSSI();
  display.drawString(0, 24, "W:" + String(wifiRSSI) + "dBm");
  
  // Show LoRa signal strength to RX (from last ACK)
  if (lastAckRSSI != 0) {
    display.drawString(64, 24, "L:" + String(lastAckRSSI) + "dBm");
  }
  
  // Show last sent message with status
  if (msgHistoryCount > 0) {
    MessageLog& last = msgHistory[msgHistoryCount - 1];
    String status = last.acked ? "[ACK]" : (last.sent ? "[SENT]" : "[FAIL]");
    String line = status + " " + last.msg;
    if (line.length() > 21) line = line.substring(0, 21);
    display.drawString(0, 40, line);
  }
  
  char line[64];
  snprintf(line, sizeof(line), "S:%lu A:%lu", (unsigned long)pktSent, (unsigned long)pktAck);
  display.drawString(0, 52, line);
  display.display();
}

// NOTE: AJAX-based send with RX screen preview
String htmlIndex() {
  String h;
  h.reserve(4000);
  h += F(
"<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
"<style>body{font-family:sans-serif;max-width:720px;margin:24px auto}textarea{width:100%;height:120px}"
".msg-list{max-height:300px;overflow-y:auto;border:1px solid #ccc;padding:8px;margin:12px 0}"
".msg-item{padding:4px 0;font-family:monospace;font-size:13px}"
".ack{color:green}.noack{color:orange}.fail{color:red}"
".rx-screen{background:#000;color:#0f0;font-family:monospace;font-size:11px;padding:8px;"
"border:2px solid #333;height:80px;overflow:hidden;white-space:pre-wrap;word-wrap:break-word;margin:12px 0}"
"button{padding:8px 16px;font-size:14px;cursor:pointer;background:#007bff;color:white;border:none;border-radius:4px}"
"button:hover{background:#0056b3}"
"</style>"
"<h2>LoRa Sender</h2>"
"<form id=f>"
"<textarea id=msg name=msg maxlength=60 placeholder='Type and press Enter to send'></textarea><br>"
"<button type=button onclick='sendMsg()'>Send Message</button>"
"</form>"
"<h3>RX Screen Preview</h3>"
"<div class=rx-screen id=rxPreview>RX display will appear here...</div>"
"<h3>Message History</h3>"
"<div class=msg-list id=msgList>");

  // Show message history (newest first)
  for (int i = msgHistoryCount - 1; i >= 0; i--) {
    MessageLog& m = msgHistory[i];
    String statusIcon;
    String cssClass;
    
    if (m.acked) {
      statusIcon = "&#x2713;";  // ✓
      cssClass = "ack";
    } else if (m.sent) {
      statusIcon = "&#x231B;";  // ⏳
      cssClass = "noack";
    } else {
      statusIcon = "&#x2717;";  // ✗
      cssClass = "fail";
    }
    
    h += "<div class='msg-item " + cssClass + "'>";
    h += statusIcon + " " + m.msg;
    h += "</div>";
  }
  
  if (msgHistoryCount == 0) {
    h += "<div style='color:#999'>No messages sent yet</div>";
  }
  
  h += F("</div>"
"<script>"
"const t=document.getElementById('msg');"
"const msgList=document.getElementById('msgList');"
"const rxPreview=document.getElementById('rxPreview');"
"t.focus();"
"t.addEventListener('keydown',e=>{"
"  if(e.key==='Enter'&&!e.shiftKey){e.preventDefault();sendMsg();}"
"});"
"async function sendMsg(){"
"  const msg=t.value.trim();"
"  if(!msg) return;"
"  const btn=document.querySelector('button');"
"  const origText=btn.textContent;"
"  btn.textContent='Sending...';"
"  btn.disabled=true;"
"  t.disabled=true;"
"  try{"
"    const resp=await fetch('/send',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
"      body:'msg='+encodeURIComponent(msg)});"
"    if(resp.ok){"
"      const data=await resp.json();"
"      if(data.rxScreen!==undefined){"
"        rxPreview.textContent=data.rxScreen||'(empty)';"
"      }"
"      t.value='';"
"      refreshHistory();"
"    }"
"  }catch(e){console.error('Send error:',e);}"
"  finally{"
"    btn.textContent=origText;"
"    btn.disabled=false;"
"    t.disabled=false;"
"    t.focus();"
"  }"
"}"
"async function refreshHistory(){"
"  try{"
"    const resp=await fetch('/');"
"    if(resp.ok){"
"      const html=await resp.text();"
"      const parser=new DOMParser();"
"      const doc=parser.parseFromString(html,'text/html');"
"      const newMsgList=doc.getElementById('msgList');"
"      if(newMsgList){"
"        msgList.innerHTML=newMsgList.innerHTML;"
"      }"
"    }"
"  }catch(e){console.error('Refresh error:',e);}"
"}"
"setInterval(async()=>{"
"  try{"
"    const resp=await fetch('/status');"
"    if(resp.ok){"
"      const data=await resp.json();"
"      if(data.rxScreen!==undefined){"
"        rxPreview.textContent=data.rxScreen||'(empty)';"
"      }"
"      if(data.sent!==undefined){"
"        document.getElementById('pktSent').textContent=data.sent;"
"      }"
"      if(data.acked!==undefined){"
"        document.getElementById('pktAck').textContent=data.acked;"
"      }"
"      if(data.txState!==undefined){"
"        document.getElementById('txState').textContent=data.txState;"
"      }"
"      if(data.ackRSSI!==undefined){"
"        document.getElementById('ackRSSI').textContent=data.ackRSSI;"
"      }"
"      if(data.ackSNR!==undefined){"
"        document.getElementById('ackSNR').textContent=data.ackSNR;"
"      }"
"      if(data.rtt!==undefined){"
"        document.getElementById('rtt').textContent=data.rtt;"
"      }"
"    }"
"  }catch(e){}"
"},3000);"
"</script>"
"<h3>Status</h3><table border=1 cellpadding=6 cellspacing=0>"
"<tr><td>Packets Sent</td><td id=pktSent>");
  h += String(pktSent);
  h += F("</td></tr><tr><td>Packets Acked</td><td id=pktAck>");
  h += String(pktAck);
  h += F("</td></tr><tr><td>Last TX State</td><td id=txState>");
  h += String(lastTxState);
  h += F("</td></tr><tr><td>Last ACK RSSI</td><td id=ackRSSI>");
  h += String(lastAckRSSI);
  h += F("</td></tr><tr><td>Last ACK SNR</td><td id=ackSNR>");
  h += String(lastAckSNR,1);
  h += F("</td></tr><tr><td>Last RTT (ms)</td><td id=rtt>");
  h += String(lastRTTms);
  h += F("</td></tr></table><h3>Radio Params</h3><ul><li>Freq: ");
  h += String(FREQ_MHZ,1);
  h += F(" MHz</li><li>SF: ");
  h += String(SF);
  h += F("</li><li>BW: ");
  h += String((int)BW_KHZ);
  h += F(" kHz</li><li>CR: 4/");
  h += String(CRx);
  h += F("</li><li>TX Power: ");
  h += String(TX_POWER_DBM);
  h += F(" dBm</li></ul>");
  return h;
}

void handleRoot() { server.send(200, "text/html", htmlIndex()); }

// Track what's displayed on RX screen (simulated)
String rxScreenSimulation = "";

void updateRxSimulation(const String& msg) {
  if (rxScreenSimulation.length() > 0) {
    rxScreenSimulation += " ";
  }
  rxScreenSimulation += msg;
  
  // Keep only last ~200 chars (roughly what fits on RX with wrapping)
  if (rxScreenSimulation.length() > 200) {
    rxScreenSimulation = rxScreenSimulation.substring(rxScreenSimulation.length() - 200);
  }
}

String buildHistoryHTML() {
  String h = "";
  for (int i = msgHistoryCount - 1; i >= 0; i--) {
    MessageLog& m = msgHistory[i];
    String statusIcon;
    String cssClass;
    
    if (m.acked) {
      statusIcon = "&#x2713;";
      cssClass = "ack";
    } else if (m.sent) {
      statusIcon = "&#x231B;";
      cssClass = "noack";
    } else {
      statusIcon = "&#x2717;";
      cssClass = "fail";
    }
    
    h += "<div class='msg-item " + cssClass + "'>";
    h += statusIcon + " " + m.msg;
    h += "</div>";
  }
  
  if (msgHistoryCount == 0) {
    h += "<div style='color:#999'>No messages sent yet</div>";
  }
  return h;
}

void handleStatus() {
  // Include all status metrics in JSON
  String json = "{";
  json += "\"rxScreen\":\"" + rxScreenSimulation + "\",";
  json += "\"sent\":" + String(pktSent) + ",";
  json += "\"acked\":" + String(pktAck) + ",";
  json += "\"txState\":" + String(lastTxState) + ",";
  json += "\"ackRSSI\":" + String(lastAckRSSI) + ",";
  json += "\"ackSNR\":" + String(lastAckSNR, 1) + ",";
  json += "\"rtt\":" + String(lastRTTms);
  json += "}";
  
  server.send(200, "application/json", json);
}

bool waitForAckWithMetrics(uint32_t timeoutMs) {
  Serial.println("Switching to blocking receive for ACK...");
  
  // Re-apply radio parameters to ensure clean state
  setRadioParams();
  
  String ackMsg;
  // Use blocking receive with a timeout
  int state = radio.receive(ackMsg, timeoutMs);
  
  if (state == RADIOLIB_ERR_NONE) {
    Serial.print("  Received: '" + ackMsg + "'");
    if (ackMsg.length() > 0 && ackMsg[0] == 'A') {
      int c1 = ackMsg.indexOf(',');
      int c2 = ackMsg.indexOf(',', c1 + 1);
      if (c1 > 0 && c2 > c1) {
        lastAckRSSI = ackMsg.substring(c1 + 1, c2).toInt();
        lastAckSNR = ackMsg.substring(c2 + 1).toFloat();
        Serial.println(" -> Valid ACK!");
        radio.standby();
        return true;
      }
    }
    Serial.println(" -> Invalid ACK data.");
  } else if (state == RADIOLIB_ERR_RX_TIMEOUT) {
    Serial.println(" -> TIMEOUT.");
  } else {
    Serial.print(" -> Receive failed, state: ");
    Serial.println(state);
  }
  
  radio.standby();
  return false;
}

void handleSend() {
  if (!server.hasArg("msg")) { 
    server.send(400, "application/json", "{\"error\":\"missing msg\"}"); 
    return; 
  }
  String msg = server.arg("msg"); 
  msg.trim();
  if (msg.isEmpty()) { 
    server.send(400, "application/json", "{\"error\":\"empty\"}"); 
    return; 
  }
  if (msg.length() > 60) msg = msg.substring(0,60);

  Serial.println("\n=== Sending Message ===");
  Serial.println("Message: '" + msg + "'");
  
  pktSent++;
  uint32_t t0 = millis();
  int st = radio.transmit(msg);
  
  Serial.print("TX State: "); Serial.println(st);
  
  bool ack = false;
  if (st == RADIOLIB_ERR_NONE) {
    Serial.println("TX OK, waiting for ACK...");
    // Start listening immediately, no delay
    ack = waitForAckWithMetrics(1000);  // Increased timeout to 1000ms
    Serial.print("ACK received: "); Serial.println(ack ? "YES" : "NO");
    if (ack) {
      Serial.print("  RSSI: "); Serial.print(lastAckRSSI);
      Serial.print(" SNR: "); Serial.println(lastAckSNR);
    }
  }
  
  lastRTTms = millis() - t0;
  lastTxState = st;
  if (ack) pktAck++;

  // Add to history
  addToHistory(msg, (st == RADIOLIB_ERR_NONE), ack);
  
  // Update RX simulation if acked (only successful ACKs)
  if (ack) {
    updateRxSimulation(msg);
    Serial.println("RX Simulation updated: '" + rxScreenSimulation + "'");
  }

  String stat = (st == RADIOLIB_ERR_NONE) ? (ack ? "sent+ack" : "sent/no-ack") : "send-fail";
  drawStatus(WiFi.localIP().toString(), stat);

  // Build JSON response manually to avoid escaping issues
  String json = "{";
  json += "\"success\":true,";
  json += "\"acked\":";
  json += ack ? "true" : "false";
  json += ",\"sent\":";
  json += (st == RADIOLIB_ERR_NONE) ? "true" : "false";
  json += ",\"rxScreen\":\"";
  json += rxScreenSimulation;
  json += "\"}";
  
  Serial.println("Sending JSON response: " + json);
  
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
  
  Serial.println("=== Done ===\n");
}

void setup() {
  Serial.begin(115200);

  // OLED
  VextON(); delay(100);
  display.init();
  display.setContrast(200);
  display.clear(); display.drawString(0,0,"WiFi joining..."); display.display();

  // Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASS);
  while (WiFi.status() != WL_CONNECTED) delay(200);

  // Web
  server.on("/", HTTP_GET,  handleRoot);
  server.on("/send", HTTP_POST, handleSend);
  server.on("/status", HTTP_GET, handleStatus);
  server.begin();
  Serial.print("Web at http://"); Serial.println(WiFi.localIP());

  // SPI + Radio
  SPI.begin(/*SCK*/9, /*MISO*/11, /*MOSI*/10, /*SS*/8);
  int st = radio.begin(FREQ_MHZ);
  if (st != RADIOLIB_ERR_NONE) {
    display.clear(); display.drawString(0,0,"radio.begin FAIL"); display.display();
    Serial.println("radio.begin FAIL: " + String(st));
  } else {
    radio.setDio2AsRfSwitch(true);
    setRadioParams(); // Use the new function
    radio.setCRC(true);
    radio.setOutputPower(TX_POWER_DBM);
    radio.standby();
  }

  drawStatus(WiFi.localIP().toString(), "Idle");
}

void loop() {
  server.handleClient();
}
