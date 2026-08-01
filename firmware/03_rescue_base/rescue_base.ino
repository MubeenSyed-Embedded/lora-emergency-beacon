/*
 * LoRa Disaster Beacon Network - Phase 5
 * rescue_base_v2.ino  ->  FLASH TO BOARD 2
 *
 * Hardware: Heltec WiFi LoRa 32 V3 (ESP32-S3FN8 + SX1262 + SSD1306)
 * Author:   Muhammad Mubeen Syed - GIKI Electrical Engineering
 *
 * CHANGE FROM PHASE 4: the base can now reply.
 *
 * Each survivor card on the dashboard carries a reply box and a set
 * of one-tap quick responses. Replies are addressed to the node the
 * message came from and transmitted back over LoRa.
 *
 * Packet formats:
 *   inbound   NODE|MSG|NAME|PEOPLE|STATUS|MESSAGE
 *   inbound   NODE|SOS|NAME|?|EMERGENCY|SOS button
 *   outbound  BASE|RPY|TARGET|MESSAGE
 *
 * Libraries: RadioLib (Jan Gromes), U8g2 (oliver)
 * WARNING:   antenna attached before power. Every time.
 */

#include <RadioLib.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

// ---- Heltec WiFi LoRa 32 V3 pin map ----
#define LORA_CS     8
#define LORA_SCK    9
#define LORA_MOSI  10
#define LORA_MISO  11
#define LORA_RST   12
#define LORA_BUSY  13
#define LORA_DIO1  14

#define OLED_SDA   17
#define OLED_SCL   18
#define OLED_RST   21
#define VEXT_PIN   36
#define LED_PIN    35

// ---- Radio settings (must match beacon exactly) ----
#define LORA_FREQ   915.0
#define LORA_BW     125.0
#define LORA_SF     7
#define LORA_CR     5
#define LORA_POWER  22
#define LORA_TCXO   1.8

#define MAX_RECORDS 20
#define BASE_ID     "BASE"

const char *AP_SSID = "RescueBase";

SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA);

WebServer server(80);
DNSServer  dns;
const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);

struct Record {
  String node, type, name, people, status, msg;
  String lastReply;
  float  rssi, snr;
  unsigned long stamp;
  bool   used;
};

Record records[MAX_RECORDS];
int  recCount   = 0;
int  writeIndex = 0;
long totalRx    = 0;
long sosCount   = 0;
long repliesSent = 0;

volatile bool packetFlag = false;
unsigned long lastOledDraw = 0;

void IRAM_ATTR onPacket() {
  packetFlag = true;
}

// ---------------------------------------------------------------
//  Radio
// ---------------------------------------------------------------

bool sendLoRa(String packet) {
  delay(random(20, 180));            // collision backoff

  digitalWrite(LED_PIN, HIGH);
  int state = radio.transmit(packet);
  digitalWrite(LED_PIN, LOW);

  radio.startReceive();              // back to listening immediately

  Serial.print("TX -> ");
  Serial.print(packet);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("   [ok]");
    repliesSent++;
    return true;
  }
  Serial.print("   [FAILED code ");
  Serial.print(state);
  Serial.println("]");
  return false;
}

String field(String s, int n) {
  int start = 0;
  for (int i = 0; i < n; i++) {
    int p = s.indexOf('|', start);
    if (p < 0) return "";
    start = p + 1;
  }
  int end = s.indexOf('|', start);
  if (end < 0) end = s.length();
  return s.substring(start, end);
}

void storePacket(String raw, float rssi, float snr) {
  Record &r = records[writeIndex];

  r.node   = field(raw, 0);
  r.type   = field(raw, 1);
  r.name   = field(raw, 2);
  r.people = field(raw, 3);
  r.status = field(raw, 4);
  r.msg    = field(raw, 5);
  r.rssi   = rssi;
  r.snr    = snr;
  r.stamp  = millis();
  r.used   = true;
  r.lastReply = "";

  if (r.name.length() == 0) r.name = "Unknown";
  if (r.msg.length()  == 0) r.msg  = "-";

  if (r.type == "SOS") sosCount++;

  writeIndex = (writeIndex + 1) % MAX_RECORDS;
  if (recCount < MAX_RECORDS) recCount++;
  totalRx++;
}

// ---------------------------------------------------------------
//  OLED
// ---------------------------------------------------------------

void showStatus() {
  display.clearBuffer();

  display.setFont(u8g2_font_7x14_tf);
  display.drawStr(0, 13, "RESCUE BASE");
  display.drawHLine(0, 17, 128);

  display.setFont(u8g2_font_6x12_tf);
  char line[26];

  snprintf(line, sizeof(line), "Rx:%ld  SOS:%ld", totalRx, sosCount);
  display.drawStr(0, 31, line);

  snprintf(line, sizeof(line), "Replies sent: %ld", repliesSent);
  display.drawStr(0, 44, line);

  if (recCount > 0) {
    int last = (writeIndex - 1 + MAX_RECORDS) % MAX_RECORDS;
    snprintf(line, sizeof(line), "Last: %s", records[last].name.c_str());
    display.drawStr(0, 57, line);
  } else {
    display.drawStr(0, 57, "Waiting...");
  }

  display.sendBuffer();
}

// ---------------------------------------------------------------
//  Dashboard
// ---------------------------------------------------------------

String signalLabel(float rssi) {
  if (rssi > -70)  return "Strong";
  if (rssi > -90)  return "Good";
  if (rssi > -105) return "Weak";
  return "Very weak";
}

int signalBars(float rssi) {
  if (rssi > -70)  return 4;
  if (rssi > -85)  return 3;
  if (rssi > -100) return 2;
  return 1;
}

String agoText(unsigned long stamp) {
  unsigned long s = (millis() - stamp) / 1000;
  if (s < 60)   return String(s) + "s ago";
  if (s < 3600) return String(s / 60) + "m ago";
  return String(s / 3600) + "h ago";
}

String buildPage(String notice) {
  String h = R"HTML(<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta charset="UTF-8">
<meta http-equiv="refresh" content="10">
<title>Rescue Base</title>
<style>
*{box-sizing:border-box}
body{margin:0;padding:16px;font-family:system-ui,-apple-system,sans-serif;
     background:#0d0d0d;color:#eee}
.wrap{max-width:520px;margin:0 auto}
h1{font-size:20px;margin:0 0 2px;color:#4fc3f7}
.sub{font-size:12px;color:#777;margin-bottom:16px}
.stats{display:flex;gap:8px;margin-bottom:16px}
.stat{flex:1;background:#1a1a1a;border-radius:8px;padding:10px;text-align:center}
.stat b{display:block;font-size:20px;color:#fff}
.stat span{font-size:11px;color:#888}
.ok{background:#0d47a1;padding:11px;border-radius:8px;margin-bottom:14px;
    font-size:14px}
.card{background:#1a1a1a;border-radius:10px;padding:13px;margin-bottom:10px;
      border-left:4px solid #444}
.card.sos{border-left-color:#ff1744;background:#2a1416}
.card.trapped{border-left-color:#ff9100}
.card.injured{border-left-color:#ffc400}
.card.medical{border-left-color:#ff1744}
.row1{display:flex;justify-content:space-between;align-items:baseline}
.nm{font-size:17px;font-weight:600}
.tm{font-size:11px;color:#777}
.badge{display:inline-block;font-size:11px;padding:3px 8px;border-radius:4px;
       background:#333;margin-top:6px;margin-right:5px}
.badge.sos{background:#ff1744;color:#fff;font-weight:700}
.msg{font-size:14px;color:#ccc;margin-top:8px;line-height:1.4}
.sig{font-size:11px;color:#666;margin-top:8px}
.bars{display:inline-block;margin-right:5px;letter-spacing:1px}
.b-on{color:#4caf50}
.b-off{color:#333}
.sent{margin-top:9px;padding:8px;background:#0d2818;border-radius:6px;
      font-size:12px;color:#81c784}
.rbox{margin-top:11px;padding-top:11px;border-top:1px solid #2a2a2a}
.quick{display:flex;flex-wrap:wrap;gap:6px;margin-bottom:8px}
.quick button{flex:1 1 auto;padding:8px 10px;font-size:12px;border:0;
      border-radius:6px;background:#263238;color:#b0bec5}
.rin{display:flex;gap:6px}
.rin input{flex:1;padding:10px;font-size:15px;border-radius:6px;
      border:1px solid #333;background:#111;color:#fff}
.rin button{padding:10px 16px;font-size:14px;font-weight:600;border:0;
      border-radius:6px;background:#0288d1;color:#fff}
.empty{text-align:center;padding:40px 20px;color:#555;font-size:14px}
</style></head><body><div class="wrap">
<h1>RESCUE BASE STATION</h1>
<div class="sub">Live LoRa feed &middot; two-way &middot; refresh 10s</div>
)HTML";

  if (notice.length()) h += "<div class='ok'>" + notice + "</div>";

  h += "<div class='stats'>";
  h += "<div class='stat'><b>" + String(totalRx) + "</b><span>RECEIVED</span></div>";
  h += "<div class='stat'><b>" + String(sosCount) + "</b><span>SOS</span></div>";
  h += "<div class='stat'><b>" + String(repliesSent) + "</b><span>REPLIES</span></div>";
  h += "</div>";

  if (recCount == 0) {
    h += "<div class='empty'>No survivor messages received yet.<br>"
         "Listening on 915 MHz.</div>";
  }

  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < recCount; i++) {
      int idx = (writeIndex - 1 - i + MAX_RECORDS * 2) % MAX_RECORDS;
      Record &r = records[idx];
      if (!r.used) continue;

      bool isSos = (r.type == "SOS");
      if (pass == 0 && !isSos) continue;
      if (pass == 1 &&  isSos) continue;

      String cls = "card";
      if (isSos)                      cls += " sos";
      else if (r.status == "TRAPPED") cls += " trapped";
      else if (r.status == "INJURED") cls += " injured";
      else if (r.status == "MEDICAL") cls += " medical";

      h += "<div class='" + cls + "'>";
      h += "<div class='row1'><span class='nm'>" + r.name + "</span>";
      h += "<span class='tm'>" + agoText(r.stamp) + "</span></div>";

      if (isSos) h += "<span class='badge sos'>SOS</span>";
      h += "<span class='badge'>" + r.status + "</span>";
      h += "<span class='badge'>" + r.people + " people</span>";
      h += "<span class='badge'>" + r.node + "</span>";

      h += "<div class='msg'>" + r.msg + "</div>";

      int bars = signalBars(r.rssi);
      h += "<div class='sig'><span class='bars'>";
      for (int b = 1; b <= 4; b++)
        h += (b <= bars) ? "<span class='b-on'>|</span>"
                         : "<span class='b-off'>|</span>";
      h += "</span>" + signalLabel(r.rssi) + " &middot; " +
           String(r.rssi, 0) + " dBm &middot; SNR " + String(r.snr, 1) + " dB</div>";

      if (r.lastReply.length()) {
        h += "<div class='sent'>&#10004; Sent: " + r.lastReply + "</div>";
      }

      // ---- reply controls ----
      h += "<div class='rbox'>";

      h += "<div class='quick'>";
      h += "<form action='/reply' method='POST' style='flex:1'>"
           "<input type='hidden' name='to' value='" + r.node + "'>"
           "<input type='hidden' name='text' value='Help is on the way'>"
           "<button type='submit'>Help coming</button></form>";
      h += "<form action='/reply' method='POST' style='flex:1'>"
           "<input type='hidden' name='to' value='" + r.node + "'>"
           "<input type='hidden' name='text' value='Stay where you are'>"
           "<button type='submit'>Stay put</button></form>";
      h += "<form action='/reply' method='POST' style='flex:1'>"
           "<input type='hidden' name='to' value='" + r.node + "'>"
           "<input type='hidden' name='text' value='Message received'>"
           "<button type='submit'>Acknowledge</button></form>";
      h += "</div>";

      h += "<form action='/reply' method='POST' class='rin'>";
      h += "<input type='hidden' name='to' value='" + r.node + "'>";
      h += "<input name='text' maxlength='50' placeholder='Custom reply...'>";
      h += "<button type='submit'>Send</button>";
      h += "</form>";

      h += "</div></div>";
    }
  }

  h += "</div></body></html>";
  return h;
}

// ---------------------------------------------------------------
//  Handlers
// ---------------------------------------------------------------

String clean(String s) {
  s.replace("|", " ");
  s.replace("\n", " ");
  s.replace("\r", " ");
  s.trim();
  return s;
}

void handleRoot() {
  server.send(200, "text/html", buildPage(""));
}

void handleReply() {
  String to   = clean(server.arg("to"));
  String text = clean(server.arg("text"));

  if (to.length() == 0)   to = "ALL";
  if (text.length() == 0) {
    server.send(200, "text/html", buildPage("Reply was empty - nothing sent."));
    return;
  }

  String packet = String(BASE_ID) + "|RPY|" + to + "|" + text;
  bool ok = sendLoRa(packet);

  // record what was sent against every card from that node
  if (ok) {
    for (int i = 0; i < recCount; i++) {
      if (records[i].used && records[i].node == to) {
        records[i].lastReply = text;
      }
    }
  }

  showStatus();

  String notice = ok
    ? "&#10004; Reply transmitted to " + to + ": " + text
    : "&#10007; Transmission failed. Try again.";

  server.send(200, "text/html", buildPage(notice));
}

void handleNotFound() {
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}

// ---------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(2000);

  randomSeed(esp_random());

  pinMode(LED_PIN, OUTPUT);
  pinMode(VEXT_PIN, OUTPUT);
  digitalWrite(VEXT_PIN, LOW);
  delay(100);

  for (int i = 0; i < MAX_RECORDS; i++) records[i].used = false;

  display.begin();
  display.clearBuffer();
  display.setFont(u8g2_font_7x14_tf);
  display.drawStr(0, 20, "RESCUE BASE");
  display.drawStr(0, 40, "starting...");
  display.sendBuffer();

  Serial.println();
  Serial.println("=====================================");
  Serial.println("  RESCUE BASE - Phase 5 (2-way)");
  Serial.println("=====================================");

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);

  int state = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR,
                          RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
                          LORA_POWER, 8, LORA_TCXO, false);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("Radio init FAILED, code ");
    Serial.println(state);
    display.clearBuffer();
    display.drawStr(0, 20, "RADIO FAIL");
    display.sendBuffer();
    while (true) { delay(1000); }
  }

  radio.setDio2AsRfSwitch(true);
  radio.setDio1Action(onPacket);
  radio.startReceive();
  Serial.println("Radio OK - transmit and receive");

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID);

  Serial.print("AP up: ");
  Serial.println(AP_SSID);
  Serial.print("IP:    ");
  Serial.println(WiFi.softAPIP());

  dns.start(DNS_PORT, "*", apIP);

  server.on("/",      HTTP_GET,  handleRoot);
  server.on("/reply", HTTP_POST, handleReply);
  server.on("/generate_204",        handleRoot);
  server.on("/gen_204",             handleRoot);
  server.on("/hotspot-detect.html", handleRoot);
  server.on("/connecttest.txt",     handleRoot);
  server.on("/ncsi.txt",            handleRoot);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println("Dashboard started");
  Serial.println();

  showStatus();
}

void loop() {
  dns.processNextRequest();
  server.handleClient();

  if (packetFlag) {
    packetFlag = false;

    String raw;
    int state = radio.readData(raw);
    radio.startReceive();

    if (state == RADIOLIB_ERR_NONE) {
      float rssi = radio.getRSSI();
      float snr  = radio.getSNR();

      Serial.print("RX <- ");
      Serial.print(raw);
      Serial.print("  | RSSI ");
      Serial.print(rssi, 1);
      Serial.print(" dBm | SNR ");
      Serial.println(snr, 1);

      String type = field(raw, 1);

      // ignore our own replies echoing back
      if (type == "RPY") {
        Serial.println("   (own reply - ignored)");
      } else if (raw.indexOf('|') > 0) {
        storePacket(raw, rssi, snr);
        digitalWrite(LED_PIN, HIGH);
        delay(40);
        digitalWrite(LED_PIN, LOW);
      } else {
        Serial.println("   (not a beacon packet - ignored)");
      }

    } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
      Serial.println("RX <- packet corrupted (CRC fail)");
    }
  }

  if (millis() - lastOledDraw > 1000) {
    lastOledDraw = millis();
    showStatus();
  }
}
