/*
 * ESP32-S3 LoRa Board - Multi-Candidate Identification
 *
 * The previous probe assumed a LilyGO T3-S3 and found nothing on
 * either SPI or I2C. This sketch does not assume. It sweeps the
 * pin maps of the common ESP32-S3 + LoRa boards and reports which
 * one responds.
 *
 * Upload, open Serial Monitor at 115200, press RST, wait ~20s,
 * then send back everything it prints.
 *
 * Author: Muhammad Mubeen Syed - GIKI Electrical Engineering
 *
 * WARNING: antenna must be attached before powering the board.
 */

#include <RadioLib.h>
#include <Wire.h>

void banner(const char *s) {
  Serial.println();
  Serial.println("=====================================");
  Serial.println(s);
  Serial.println("=====================================");
}

// ---------------------------------------------------------------
//  PART 1 - I2C sweep across candidate SDA/SCL pairs
// ---------------------------------------------------------------

struct I2CCandidate {
  const char *board;
  int sda;
  int scl;
};

I2CCandidate i2cPins[] = {
  { "LilyGO T3-S3",            18, 17 },
  { "Heltec WiFi LoRa 32 V3",  17, 18 },
  { "Classic ESP32 / TTGO",    21, 22 },
  { "Heltec Wireless Stick V3",  4, 15 },
  { "Generic S3 A",            42, 41 },
  { "Generic S3 B",             8,  9 },
  { "Generic S3 C",            43, 44 },
};

void scanI2C(const char *label, int sda, int scl) {
  Serial.print("  ");
  Serial.print(label);
  Serial.print("  (SDA=");
  Serial.print(sda);
  Serial.print(" SCL=");
  Serial.print(scl);
  Serial.print(") -> ");

  Wire.end();
  delay(20);
  Wire.begin(sda, scl);
  Wire.setClock(100000);
  delay(20);

  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      if (found == 0) Serial.print("FOUND ");
      Serial.print("0x");
      if (addr < 16) Serial.print("0");
      Serial.print(addr, HEX);
      Serial.print(" ");
      found++;
    }
  }
  if (found == 0) Serial.println("nothing");
  else            Serial.println("  <=== THIS ONE");
}

// ---------------------------------------------------------------
//  PART 2 - Radio sweep across candidate SPI pin maps
// ---------------------------------------------------------------

struct RadioCandidate {
  const char *board;
  int sck, miso, mosi, cs, rst, dio1, busy;
};

RadioCandidate radioPins[] = {
  // board                     SCK MISO MOSI  CS  RST DIO1 BUSY
  { "LilyGO T3-S3",              5,   3,   6,  7,   8,  33,  34 },
  { "Heltec WiFi LoRa 32 V3",    9,  11,  10,  8,  12,  14,  13 },
  { "LilyGO T-Beam S3",          5,   3,   6,  7,   5,  33,  34 },
  { "Generic S3 LoRa",          12,  13,  11, 10,   9,  14,  15 },
};

void probeRadio(RadioCandidate c) {
  Serial.print("  ");
  Serial.print(c.board);
  Serial.print("  (SCK=");
  Serial.print(c.sck);
  Serial.print(" CS=");
  Serial.print(c.cs);
  Serial.print(" BUSY=");
  Serial.print(c.busy);
  Serial.print(") -> ");

  SPI.end();
  delay(20);
  SPI.begin(c.sck, c.miso, c.mosi, c.cs);
  delay(20);

  SX1262 r = new Module(c.cs, c.dio1, c.rst, c.busy);
  int state = r.begin(915.0);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("SX1262 FOUND   <=== THIS ONE");
    return;
  }

  // try the SX127x wiring on the same bus
  SX1276 r2 = new Module(c.cs, c.busy, c.rst, c.dio1);
  int state2 = r2.begin(915.0);
  if (state2 == RADIOLIB_ERR_NONE) {
    Serial.println("SX1276 FOUND   <=== THIS ONE");
    return;
  }

  Serial.print("nothing (");
  Serial.print(state);
  Serial.print(" / ");
  Serial.print(state2);
  Serial.println(")");
}

// ---------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(3000);

  banner("ESP32-S3 LoRa BOARD IDENTIFICATION");

  // Some boards (Heltec V3) gate power to the OLED behind a control
  // pin. Pull the usual suspects low so peripherals are actually on.
  Serial.println("Enabling possible peripheral power rails...");
  int powerPins[] = { 36, 21, 3, 35 };
  for (int i = 0; i < 4; i++) {
    pinMode(powerPins[i], OUTPUT);
    digitalWrite(powerPins[i], LOW);
  }
  delay(200);

  // Pulse common OLED reset pins
  int oledRst[] = { 21, 16 };
  for (int i = 0; i < 2; i++) {
    pinMode(oledRst[i], OUTPUT);
    digitalWrite(oledRst[i], LOW);
    delay(30);
    digitalWrite(oledRst[i], HIGH);
  }
  delay(200);

  banner("PART 1: I2C SWEEP (finding the OLED)");
  for (unsigned i = 0; i < sizeof(i2cPins) / sizeof(i2cPins[0]); i++) {
    scanI2C(i2cPins[i].board, i2cPins[i].sda, i2cPins[i].scl);
    delay(50);
  }

  banner("PART 2: RADIO SWEEP (finding the LoRa chip)");
  for (unsigned i = 0; i < sizeof(radioPins) / sizeof(radioPins[0]); i++) {
    probeRadio(radioPins[i]);
    delay(100);
  }

  banner("DONE - send all of the above back");
  Serial.println("If every line says 'nothing', the RF module may be");
  Serial.println("unseated in its socket, or this is a board not yet");
  Serial.println("in the candidate list. A photo will settle it.");
}

void loop() {
  delay(10000);
}
