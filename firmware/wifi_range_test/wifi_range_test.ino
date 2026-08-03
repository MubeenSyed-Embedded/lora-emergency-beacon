/*
 * LoRa Disaster Beacon Network - WiFi Range Test Build
 * wifi_range_test.ino  ->  FLASH TO BOARD 1
 *
 * Hardware: Heltec WiFi LoRa 32 V3 (ESP32-S3FN8 + SX1262 + SSD1306)
 * Author:   Muhammad Mubeen Syed - GIKI Electrical Engineering
 *
 * This is the survivor beacon with instrumentation added for
 * measuring WiFi range. It reports, once per second:
 *
 *   - how many phones are associated
 *   - each phone's RSSI as measured at the access point
 *   - whether any HTTP request has arrived recently
 *
 * The last point matters. A phone can remain associated to an AP
 * long after the link is too weak to actually carry a page, so
 * association alone is not proof of a usable connection. Only a
 * completed HTTP request proves the link works end to end.
 *
 * The OLED shows the same data so the board can be read in the
 * field with no laptop attached.
 *
 * Libraries: RadioLib, U8g2
 * WARNING:   antenna attached before power. Every time.
 */

#include <RadioLib.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <esp_wifi.h>

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

// ---- Radio (kept running so the system is realistic) ----
#define LORA_FREQ   915.0
#define LORA_BW     125.0
#define LORA_SF     7
#define LORA_CR     5
#define LORA_POWER  22
#define LORA_TCXO   1.8

const char *AP_SSID = "SurvivorBeacon";

SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA);

WebServer server(80);
DNSServer  dns;
const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);

// ---- instrumentation state ----
unsigned long lastHttpRequest = 0;      // millis() of last page served
long  httpRequestCount = 0;
long  formSubmitCount  = 0;
int   lastClientRssi   = 0;
int   worstClientRssi  = 0;
unsigned long lastReport = 0;

// ---------------------------------------------------------------
//  Client signal strength, measured at the access point
// ---------------------------------------------------------------

// Returns the number of associated stations, and writes the RSSI of
// the first one into rssiOut. The AP measures this, not the phone,
// so it reflects the phone's transmit path rather than its receive
// path - the two are usually similar but not identical.
int getClientInfo(int *rssiOut) {
  wifi_sta_list_t staList;
  esp_err_t err = esp_wifi_ap_get_sta_list(&staList);

  if (err != ESP_OK || staList.num == 0) {
    *rssiOut = 0;
    return 0;
  }

  *rssiOut = staList.sta[0].rssi;
  return staList.num;
}

// ---------------------------------------------------------------
//  OLED
// ---------------------------------------------------------------

void showStatus(int clients, int rssi) {
  display.clearBuffer();

  display.setFont(u8g2_font_7x14_tf);
  display.drawStr(0, 13, "WIFI RANGE TEST");
  display.drawHLine(0, 17, 128);

  display.setFont(u8g2_font_6x12_tf);
  char l[26];

  snprintf(l, sizeof(l), "Phones: %d", clients);
  display.drawStr(0, 30, l);

  if (clients > 0) {
    snprintf(l, sizeof(l), "RSSI: %d dBm", rssi);
  } else {
    snprintf(l, sizeof(l), "RSSI: --");
  }
  display.drawStr(0, 43, l);

  // seconds since the last successful page load
  if (lastHttpRequest > 0) {
    unsigned long ago = (millis() - lastHttpRequest) / 1000;
    if (ago < 999) {
      snprintf(l, sizeof(l), "HTTP: %lus ago (%ld)", ago, httpRequestCount);
    } else {
      snprintf(l, sizeof(l), "HTTP: stale (%ld)", httpRequestCount);
    }
  } else {
    snprintf(l, sizeof(l), "HTTP: none yet");
  }
  display.drawStr(0, 56, l);

  display.sendBuffer();
}

// ---------------------------------------------------------------
//  Web page - deliberately small
//
//  A large page fails at shorter range than a small one, because
//  more packets means more chances for one to be lost. Keeping the
//  test page minimal measures the link rather than the payload.
// ---------------------------------------------------------------

String testPage() {
  int rssi;
  int clients = getClientInfo(&rssi);

  String h = R"HTML(<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta charset="UTF-8">
<title>Range Test</title>
<style>
body{margin:0;padding:24px;font-family:system-ui,sans-serif;
     background:#111;color:#eee;text-align:center}
h1{font-size:26px;color:#4caf50;margin:0 0 8px}
.big{font-size:44px;font-weight:700;margin:18px 0;color:#fff}
.sub{font-size:14px;color:#888}
.n{font-size:13px;color:#666;margin-top:22px;line-height:1.6}
button{margin-top:20px;padding:16px 28px;font-size:17px;border:0;
       border-radius:8px;background:#c62828;color:#fff;width:100%}
</style></head><body>
<h1>&#10004; PAGE LOADED</h1>
<div class="sub">The link works at this distance.</div>
)HTML";

  h += "<div class='big'>" + String(rssi) + " dBm</div>";
  h += "<div class='sub'>signal measured at the beacon</div>";

  h += "<form action='/submit' method='POST'>";
  h += "<button type='submit'>SEND TEST MESSAGE</button>";
  h += "</form>";

  h += "<div class='n'>";
  h += "Page loads: " + String(httpRequestCount) + "<br>";
  h += "Form submits: " + String(formSubmitCount) + "<br>";
  h += "Phones connected: " + String(clients);
  h += "</div>";

  h += "</body></html>";
  return h;
}

void handleRoot() {
  httpRequestCount++;
  lastHttpRequest = millis();

  int rssi;
  getClientInfo(&rssi);
  lastClientRssi = rssi;
  if (rssi < worstClientRssi || worstClientRssi == 0) worstClientRssi = rssi;

  Serial.print(">>> PAGE SERVED  | client RSSI ");
  Serial.print(rssi);
  Serial.print(" dBm | total loads ");
  Serial.println(httpRequestCount);

  server.send(200, "text/html", testPage());
}

void handleSubmit() {
  formSubmitCount++;
  lastHttpRequest = millis();

  int rssi;
  getClientInfo(&rssi);

  Serial.print(">>> FORM SUBMITTED | client RSSI ");
  Serial.print(rssi);
  Serial.println(" dBm");

  // send it over LoRa, so the full path is exercised
  String packet = "N1|TEST|RangeTest|1|SAFE|rssi=" + String(rssi);
  radio.transmit(packet);
  radio.startReceive();

  Serial.print("    LoRa TX: ");
  Serial.println(packet);

  server.send(200, "text/html", testPage());
}

void handleNotFound() {
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}

// ---------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(2000);

  pinMode(LED_PIN, OUTPUT);
  pinMode(VEXT_PIN, OUTPUT);
  digitalWrite(VEXT_PIN, LOW);
  delay(100);

  display.begin();
  display.clearBuffer();
  display.setFont(u8g2_font_7x14_tf);
  display.drawStr(0, 20, "WIFI RANGE");
  display.drawStr(0, 40, "starting...");
  display.sendBuffer();

  Serial.println();
  Serial.println("=====================================");
  Serial.println("  WIFI RANGE TEST - Survivor Beacon");
  Serial.println("=====================================");

  // ---- Radio ----
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  int state = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR,
                          RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
                          LORA_POWER, 8, LORA_TCXO, false);
  if (state == RADIOLIB_ERR_NONE) {
    radio.setDio2AsRfSwitch(true);
    radio.startReceive();
    Serial.println("Radio OK");
  } else {
    Serial.print("Radio init failed, code ");
    Serial.println(state);
    Serial.println("(continuing - WiFi test does not require it)");
  }

  // ---- Access point ----
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID);

  // Maximum transmit power, so the test measures the best case.
  // 78 is the maximum value accepted; units are 0.25 dBm.
  esp_wifi_set_max_tx_power(78);

  int8_t power;
  esp_wifi_get_max_tx_power(&power);
  Serial.print("AP up: ");
  Serial.println(AP_SSID);
  Serial.print("TX power: ");
  Serial.print(power * 0.25);
  Serial.println(" dBm");
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());

  dns.start(DNS_PORT, "*", apIP);

  server.on("/",       HTTP_GET,  handleRoot);
  server.on("/submit", HTTP_POST, handleSubmit);
  server.on("/generate_204",        handleRoot);
  server.on("/gen_204",             handleRoot);
  server.on("/hotspot-detect.html", handleRoot);
  server.on("/connecttest.txt",     handleRoot);
  server.on("/ncsi.txt",            handleRoot);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println("Web server started");
  Serial.println();
  Serial.println("Reporting once per second:");
  Serial.println("  clients | RSSI | page loads | form submits");
  Serial.println();
}

void loop() {
  dns.processNextRequest();
  server.handleClient();

  if (millis() - lastReport > 1000) {
    lastReport = millis();

    int rssi;
    int clients = getClientInfo(&rssi);

    Serial.print("clients=");
    Serial.print(clients);

    if (clients > 0) {
      Serial.print("  RSSI=");
      Serial.print(rssi);
      Serial.print(" dBm");
      lastClientRssi = rssi;
    } else {
      Serial.print("  RSSI=--     ");
    }

    Serial.print("  loads=");
    Serial.print(httpRequestCount);
    Serial.print("  submits=");
    Serial.print(formSubmitCount);

    if (lastHttpRequest > 0) {
      Serial.print("  last page ");
      Serial.print((millis() - lastHttpRequest) / 1000);
      Serial.print("s ago");
    }
    Serial.println();

    showStatus(clients, rssi);
  }
}
