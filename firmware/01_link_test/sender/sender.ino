/*
 * LoRa Disaster Beacon Network - Phase 1
 * heltec_sender.ino  ->  FLASH TO BOARD 1
 *
 * Hardware: Heltec WiFi LoRa 32 V3  (ESP32-S3FN8 + SX1262 + SSD1306)
 * Author:   Muhammad Mubeen Syed - GIKI Electrical Engineering
 *
 * Transmits a numbered packet every 2 seconds and mirrors the
 * status on the OLED. This replaces the TTGO/SX1276 sketches --
 * the pin map and radio driver are completely different.
 *
 * Libraries: RadioLib (Jan Gromes), U8g2 (oliver)
 * WARNING:   antenna attached before power. Every time.
 */

#include <RadioLib.h>
#include <U8g2lib.h>
#include <Wire.h>

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
#define VEXT_PIN   36     // LOW = peripheral power ON
#define LED_PIN    35
#define PRG_BUTTON  0     // on-board button, free for the SOS key later

// ---- Radio settings (both boards must match) ----
#define LORA_FREQ   915.0   // change to 868.0 or 433.0 to match your variant
#define LORA_BW     125.0
#define LORA_SF     7
#define LORA_CR     5
#define LORA_POWER  14      // dBm. Keep low on the bench; raise to 22 outdoors.
#define LORA_TCXO   1.8     // Heltec V3 drives the TCXO at 1.8 V

SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA);

long seq = 0;

void showOLED(const char *l1, const char *l2, const char *l3) {
  display.clearBuffer();
  display.setFont(u8g2_font_7x14_tf);
  display.drawStr(0, 14, l1);
  display.setFont(u8g2_font_6x12_tf);
  display.drawStr(0, 34, l2);
  display.drawStr(0, 50, l3);
  display.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  pinMode(LED_PIN, OUTPUT);

  // Power the peripheral rail BEFORE touching the OLED.
  // On the V3, LOW enables Vext. This is the step that makes the
  // display appear on the I2C bus at 0x3C.
  pinMode(VEXT_PIN, OUTPUT);
  digitalWrite(VEXT_PIN, LOW);
  delay(100);

  display.begin();
  showOLED("BOARD 1", "LoRa sender", "starting...");

  Serial.println();
  Serial.println("=====================================");
  Serial.println("  HELTEC V3 - LoRa SENDER (Phase 1)");
  Serial.println("=====================================");

  // Point the default SPI bus at the Heltec LoRa pins
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);

  int state = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR,
                          RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
                          LORA_POWER, 8, LORA_TCXO, false);

  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("Radio init FAILED, code ");
    Serial.println(state);
    Serial.println("-2   = chip not found (check pins)");
    Serial.println("-706 = SPI ok but chip silent");
    showOLED("RADIO FAIL", "init error", "see serial");
    while (true) { delay(1000); }
  }

  // DIO2 drives the antenna switch on this board
  radio.setDio2AsRfSwitch(true);

  Serial.println("Radio init OK");
  Serial.print("Freq ");
  Serial.print(LORA_FREQ);
  Serial.print(" MHz | SF");
  Serial.print(LORA_SF);
  Serial.print(" | BW ");
  Serial.print(LORA_BW);
  Serial.print(" kHz | ");
  Serial.print(LORA_POWER);
  Serial.println(" dBm");
  Serial.println();

  showOLED("BOARD 1", "Radio OK", "transmitting");
  delay(1000);
}

void loop() {
  seq++;
  String msg = "Hello from Board 1 seq=" + String(seq);

  digitalWrite(LED_PIN, HIGH);
  int state = radio.transmit(msg);
  digitalWrite(LED_PIN, LOW);

  char l2[24], l3[24];
  snprintf(l2, sizeof(l2), "Seq: %ld", seq);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("TX -> " + msg);
    snprintf(l3, sizeof(l3), "Status: sent");
  } else {
    Serial.print("TX failed, code ");
    Serial.println(state);
    snprintf(l3, sizeof(l3), "Status: ERR %d", state);
  }

  showOLED("BEACON ACTIVE", l2, l3);
  delay(2000);
}
