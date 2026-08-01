/*
 * LoRa Disaster Beacon Network - Phase 1 (revised)
 * heltec_receiver.ino  ->  FLASH TO BOARD 2
 *
 * Hardware: Heltec WiFi LoRa 32 V3  (ESP32-S3FN8 + SX1262 + SSD1306)
 * Author:   Muhammad Mubeen Syed - GIKI Electrical Engineering
 *
 * CHANGE FROM v1: reception is now interrupt-driven.
 *
 * The previous version used blocking radio.receive(), which left the
 * radio deaf while the sketch printed to serial and redrew the OLED.
 * That dropped roughly every second packet. Here DIO1 raises a flag
 * on packet arrival, the payload is read out, the radio is put back
 * into receive IMMEDIATELY, and only then do we do slow housekeeping.
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
#define VEXT_PIN   36
#define LED_PIN    35

// ---- Radio settings (must match sender exactly) ----
#define LORA_FREQ   915.0
#define LORA_BW     125.0
#define LORA_SF     7
#define LORA_CR     5
#define LORA_POWER  14
#define LORA_TCXO   1.8

// Redraw the OLED at most this often. The display is slow (I2C) and
// there is no value in redrawing it faster than the eye can follow.
#define OLED_INTERVAL_MS 500

SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA);

volatile bool packetFlag = false;

long  rxCount = 0;
long  lastSeq = 0;
long  missed  = 0;
float lastRssi = 0;
float lastSnr  = 0;
unsigned long lastOledDraw = 0;
bool  needsRedraw = false;

// Interrupt service routine. Must live in IRAM and do nothing but
// set a flag - no serial, no I2C, no allocation.
void IRAM_ATTR onPacket() {
  packetFlag = true;
}

void showOLED(const char *l1, const char *l2, const char *l3, const char *l4) {
  display.clearBuffer();
  display.setFont(u8g2_font_7x14_tf);
  display.drawStr(0, 14, l1);
  display.setFont(u8g2_font_6x12_tf);
  display.drawStr(0, 30, l2);
  display.drawStr(0, 44, l3);
  display.drawStr(0, 58, l4);
  display.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  pinMode(LED_PIN, OUTPUT);

  pinMode(VEXT_PIN, OUTPUT);
  digitalWrite(VEXT_PIN, LOW);      // LOW = peripheral power ON
  delay(100);

  display.begin();
  showOLED("BOARD 2", "LoRa receiver", "starting...", "");

  Serial.println();
  Serial.println("=====================================");
  Serial.println("  HELTEC V3 - RECEIVER (interrupt)");
  Serial.println("=====================================");

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);

  int state = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR,
                          RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
                          LORA_POWER, 8, LORA_TCXO, false);

  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("Radio init FAILED, code ");
    Serial.println(state);
    showOLED("RADIO FAIL", "init error", "see serial", "");
    while (true) { delay(1000); }
  }

  radio.setDio2AsRfSwitch(true);

  // Attach the interrupt, then arm continuous receive
  radio.setDio1Action(onPacket);

  state = radio.startReceive();
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("startReceive FAILED, code ");
    Serial.println(state);
    while (true) { delay(1000); }
  }

  Serial.println("Radio init OK - listening (interrupt driven)");
  Serial.println();

  showOLED("RESCUE RX", "Radio OK", "waiting...", "");
}

void loop() {

  if (packetFlag) {
    packetFlag = false;

    String msg;
    int state = radio.readData(msg);

    // Get back on air before doing anything slow. This single line
    // is the difference between hearing every packet and half of them.
    radio.startReceive();

    if (state == RADIOLIB_ERR_NONE) {
      lastRssi = radio.getRSSI();
      lastSnr  = radio.getSNR();
      rxCount++;

      int i = msg.indexOf("seq=");
      if (i >= 0) {
        long seq = msg.substring(i + 4).toInt();
        if (lastSeq > 0 && seq > lastSeq + 1) {
          missed += (seq - lastSeq - 1);
        }
        lastSeq = seq;
      }

      Serial.print("RX <- ");
      Serial.print(msg);
      Serial.print("  | RSSI ");
      Serial.print(lastRssi, 1);
      Serial.print(" dBm | SNR ");
      Serial.print(lastSnr, 1);
      Serial.print(" dB | got ");
      Serial.print(rxCount);
      Serial.print(" missed ");
      Serial.print(missed);

      if (lastRssi > -20.0) {
        Serial.print("   [RSSI SATURATED - move boards apart]");
      }
      Serial.println();

      needsRedraw = true;

    } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
      Serial.println("RX <- packet corrupted (CRC fail)");
    } else {
      Serial.print("readData error, code ");
      Serial.println(state);
    }
  }

  // Slow housekeeping happens outside the receive path, rate limited.
  if (needsRedraw && (millis() - lastOledDraw > OLED_INTERVAL_MS)) {
    lastOledDraw = millis();
    needsRedraw = false;

    char l2[24], l3[24], l4[24];
    snprintf(l2, sizeof(l2), "RSSI: %.0f dBm", lastRssi);
    snprintf(l3, sizeof(l3), "SNR:  %.1f dB", lastSnr);
    snprintf(l4, sizeof(l4), "Rx:%ld Lost:%ld", rxCount, missed);
    showOLED("RESCUE RX", l2, l3, l4);
  }
}
