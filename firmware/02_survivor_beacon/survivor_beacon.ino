/*
 * LoRa Disaster Beacon Network - Phase 5
 * survivor_beacon_v2.ino  ->  FLASH TO BOARD 1
 *
 * Hardware: Heltec WiFi LoRa 32 V3 (ESP32-S3FN8 + SX1262 + SSD1306)
 * Author:   Muhammad Mubeen Syed - GIKI Electrical Engineering
 *
 * CHANGE FROM PHASE 3: the link is now two-way.
 *
 * The beacon still serves a form to survivors over its own WiFi AP,
 * but it now also listens on LoRa for replies from the rescue base
 * and displays them on the survivor's phone.
 *
 * Because both nodes share one channel, the radio sits in receive
 * whenever it is not transmitting, and a small random backoff is
 * applied before each transmission to reduce collisions.
 *
 * Packet formats:
 *   outbound  NODE|MSG|NAME|PEOPLE|STATUS|MESSAGE
 *   outbound  NODE|SOS|NAME|?|EMERGENCY|SOS button
 *   inbound   BASE|RPY|NODE|MESSAGE
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

// ---- Radio settings (must match base exactly) ----
#define LORA_FREQ   915.0
#define LORA_BW     125.0
#define LORA_SF     7
#define LORA_CR     5
#define LORA_POWER  22
#define LORA_TCXO   1.8

#define NODE_ID     "N1"
#define MAX_REPLIES  8

const char *AP_SSID = "SurvivorBeacon";

SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA);

WebServer server(80);
DNSServer  dns;
const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);

struct Reply {
  String text;
  unsigned long stamp;
  bool used;
};

Reply replies[MAX_REPLIES];
int  replyCount = 0;
int  replyWrite = 0;

long msgSent     = 0;
long replyRecved = 0;
String lastName  = "";

volatile bool packetFlag = false;
unsigned long lastOledDraw = 0;

void IRAM_ATTR onPacket() {
  packetFlag = true;
}

// ---------------------------------------------------------------
//  Radio helpers
// ---------------------------------------------------------------

// Transmit, then return to listening. The radio must be back in
// receive or replies from the base will be missed.
bool sendLoRa(String packet) {
  // random backoff reduces the chance of colliding with the base
  delay(random(20, 180));

  digitalWrite(LED_PIN, HIGH);
  int state = radio.transmit(packet);
  digitalWrite(LED_PIN, LOW);

  radio.startReceive();

  Serial.print("TX -> ");
  Serial.print(packet);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("   [ok]");
    msgSent++;
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

void storeReply(String text) {
  Reply &r = replies[replyWrite];
  r.text  = text;
  r.stamp = millis();
  r.used  = true;

  replyWrite = (replyWrite + 1) % MAX_REPLIES;
  if (replyCount < MAX_REPLIES) replyCount++;
  replyRecved++;
}

// ---------------------------------------------------------------
//  OLED
// ---------------------------------------------------------------

void showStatus() {
  display.clearBuffer();

  display.setFont(u8g2_font_7x14_tf);
  display.drawStr(0, 13, "SURVIVOR BEACON");
  display.drawHLine(0, 17, 128);

  display.setFont(u8g2_font_6x12_tf);
  char line[26];

  snprintf(line, sizeof(line), "Phones: %d",
           WiFi.softAPgetStationNum());
  display.drawStr(0, 31, line);

  snprintf(line, sizeof(line), "Sent: %ld", msgSent);
  display.drawStr(0, 44, line);

  if (replyRecved > 0) {
    snprintf(line, sizeof(line), "REPLIES: %ld", replyRecved);
  } else {
    snprintf(line, sizeof(line), "No replies yet");
  }
  display.drawStr(0, 57, line);

  display.sendBuffer();
}

// ---------------------------------------------------------------
//  Web page
// ---------------------------------------------------------------

String agoText(unsigned long stamp) {
  unsigned long s = (millis() - stamp) / 1000;
  if (s < 60)   return String(s) + "s ago";
  if (s < 3600) return String(s / 60) + "m ago";
  return String(s / 3600) + "h ago";
}

String pageForm(String notice) {
  String h = R"HTML(<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta charset="UTF-8">
<meta http-equiv="refresh" content="8">
<title>Emergency Beacon</title>
<style>
*{box-sizing:border-box}
body{margin:0;padding:20px;font-family:system-ui,-apple-system,sans-serif;
     background:#111;color:#eee}
.card{max-width:420px;margin:0 auto}
h1{font-size:22px;margin:0 0 4px;color:#ff4444}
.sub{font-size:13px;color:#888;margin-bottom:20px}
label{display:block;margin:14px 0 5px;font-size:14px;color:#bbb}
input,textarea,select{width:100%;padding:12px;font-size:16px;border-radius:8px;
     border:1px solid #333;background:#1c1c1c;color:#fff}
textarea{height:80px;resize:vertical}
button{width:100%;margin-top:20px;padding:15px;font-size:17px;font-weight:600;
     border:0;border-radius:8px;background:#c62828;color:#fff}
button:active{background:#8e1f1f}
.sos{background:#ff8800;margin-top:10px}
.ok{background:#1b5e20;padding:12px;border-radius:8px;margin-bottom:16px;
     font-size:14px}
.inbox{background:#0d2818;border:1px solid #1b5e20;border-radius:10px;
     padding:14px;margin-bottom:20px}
.inbox h2{font-size:15px;margin:0 0 10px;color:#66bb6a}
.rep{background:#14331f;border-radius:8px;padding:11px;margin-bottom:8px}
.rep .t{font-size:15px;color:#fff;line-height:1.4}
.rep .w{font-size:11px;color:#7a9d85;margin-top:5px}
.note{margin-top:22px;font-size:12px;color:#666;line-height:1.5}
</style></head><body><div class="card">
<h1>&#9888; EMERGENCY BEACON</h1>
<div class="sub">No internet needed. Your message is sent by radio.</div>
)HTML";

  if (notice.length()) {
    h += "<div class='ok'>" + notice + "</div>";
  }

  // ---- inbox: replies from the rescue base ----
  if (replyCount > 0) {
    h += "<div class='inbox'><h2>&#128225; MESSAGES FROM RESCUE TEAM</h2>";
    for (int i = 0; i < replyCount; i++) {
      int idx = (replyWrite - 1 - i + MAX_REPLIES * 2) % MAX_REPLIES;
      if (!replies[idx].used) continue;
      h += "<div class='rep'><div class='t'>" + replies[idx].text + "</div>";
      h += "<div class='w'>" + agoText(replies[idx].stamp) + "</div></div>";
    }
    h += "</div>";
  }

  h += R"HTML(
<form action="/send" method="POST">
<label>Your name</label>
<input name="name" maxlength="20" required>

<label>How many people with you?</label>
<select name="people">
<option value="1">1 (just me)</option>
<option value="2">2</option>
<option value="3">3</option>
<option value="4">4</option>
<option value="5">5 or more</option>
</select>

<label>Situation</label>
<select name="status">
<option value="SAFE">Safe, need assistance</option>
<option value="INJURED">Someone is injured</option>
<option value="TRAPPED">Trapped, cannot move</option>
<option value="MEDICAL">Urgent medical help</option>
</select>

<label>Message (optional)</label>
<textarea name="msg" maxlength="60"
   placeholder="Landmark, floor number, anything useful"></textarea>

<button type="submit">SEND MESSAGE</button>
</form>

<form action="/sos" method="POST">
<button type="submit" class="sos">&#128680; SOS - SEND NOW</button>
</form>

<div class="note">
Keep this page open. It refreshes every 8 seconds to check for replies.
</div>
</div></body></html>
)HTML";

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
  server.send(200, "text/html", pageForm(""));
}

void handleSend() {
  String name   = clean(server.arg("name"));
  String people = clean(server.arg("people"));
  String status = clean(server.arg("status"));
  String msg    = clean(server.arg("msg"));

  if (name.length() == 0) name = "Unknown";
  if (msg.length()  == 0) msg  = "-";

  lastName = name;

  String packet = String(NODE_ID) + "|MSG|" + name + "|" + people +
                  "|" + status + "|" + msg;

  bool ok = sendLoRa(packet);
  showStatus();

  String notice = ok
    ? "&#10004; Message sent. Waiting for rescue team reply."
    : "&#10007; Send failed. Try again.";

  server.send(200, "text/html", pageForm(notice));
}

void handleSOS() {
  String name = lastName.length() ? lastName : "Unknown";
  String packet = String(NODE_ID) + "|SOS|" + name + "|?|EMERGENCY|SOS button";

  bool ok = sendLoRa(packet);
  showStatus();

  String notice = ok
    ? "&#128680; SOS SENT. Stay where you are if it is safe."
    : "&#10007; SOS failed. Press again.";

  server.send(200, "text/html", pageForm(notice));
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

  for (int i = 0; i < MAX_REPLIES; i++) replies[i].used = false;

  display.begin();
  display.clearBuffer();
  display.setFont(u8g2_font_7x14_tf);
  display.drawStr(0, 20, "BEACON");
  display.drawStr(0, 40, "starting...");
  display.sendBuffer();

  Serial.println();
  Serial.println("=====================================");
  Serial.println("  SURVIVOR BEACON - Phase 5 (2-way)");
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

  server.on("/",     HTTP_GET,  handleRoot);
  server.on("/send", HTTP_POST, handleSend);
  server.on("/sos",  HTTP_POST, handleSOS);
  server.on("/generate_204",        handleRoot);
  server.on("/gen_204",             handleRoot);
  server.on("/hotspot-detect.html", handleRoot);
  server.on("/connecttest.txt",     handleRoot);
  server.on("/ncsi.txt",            handleRoot);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println("Web server started");
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

      Serial.print("RX <- ");
      Serial.print(raw);
      Serial.print("  | RSSI ");
      Serial.println(rssi, 1);

      // BASE|RPY|TARGET|MESSAGE
      String src    = field(raw, 0);
      String type   = field(raw, 1);
      String target = field(raw, 2);

      if (type == "RPY" && (target == NODE_ID || target == "ALL")) {
        String text = field(raw, 3);
        storeReply(text);
        Serial.println("   >>> REPLY FOR THIS NODE - shown on survivor phone");

        // flash to draw attention
        for (int i = 0; i < 3; i++) {
          digitalWrite(LED_PIN, HIGH); delay(80);
          digitalWrite(LED_PIN, LOW);  delay(80);
        }
      } else {
        Serial.println("   (not addressed to this node - ignored)");
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
