/*
 * Heltec WiFi LoRa 32 V3 - Radio Chip Identification
 * radio_id.ino
 *
 * Determines definitively whether the fitted LoRa radio is an
 * SX1262 (SX126x family) or an SX1276 (SX127x family).
 *
 * The two families are distinguished by four independent tests, so
 * a single ambiguous result does not decide the answer. Read the
 * VERDICT block at the end of the serial output.
 *
 * Author: Muhammad Mubeen Syed - GIKI Electrical Engineering
 *
 * Requires: RadioLib (Jan Gromes)
 * WARNING:  antenna attached before power. Every time.
 *
 * Upload, open Serial Monitor at 115200, press RST, wait ~15 s,
 * then read the verdict.
 */

#include <RadioLib.h>
#include <SPI.h>

// ---- Heltec WiFi LoRa 32 V3 pin map ----
#define LORA_CS     8
#define LORA_SCK    9
#define LORA_MOSI  10
#define LORA_MISO  11
#define LORA_RST   12
#define LORA_BUSY  13
#define LORA_DIO1  14

#define VEXT_PIN   36     // LOW = peripheral power ON

// Radio objects. Only one of these can be correct.
// SX1262 signature: (cs, irq, rst, busy)
// SX1276 signature: (cs, irq, rst, gpio)
SX1262 radio1262 = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);
SX1276 radio1276 = new Module(LORA_CS, LORA_DIO1, LORA_RST, RADIOLIB_NC);

int score1262 = 0;
int score1276 = 0;

void line() {
  Serial.println("---------------------------------------------");
}

void header(const char *s) {
  Serial.println();
  line();
  Serial.println(s);
  line();
}

// ---------------------------------------------------------------
//  Low-level SPI helper
//
//  Talks to the chip directly, bypassing RadioLib, so we can read
//  raw registers without a driver imposing its own assumptions.
// ---------------------------------------------------------------

void spiSelect(bool on) {
  digitalWrite(LORA_CS, on ? LOW : HIGH);
  delayMicroseconds(10);
}

// SX127x: read one register. Address byte with MSB clear = read.
uint8_t sx127x_readReg(uint8_t addr) {
  SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  spiSelect(true);
  SPI.transfer(addr & 0x7F);
  uint8_t val = SPI.transfer(0x00);
  spiSelect(false);
  SPI.endTransaction();
  return val;
}

// SX126x: command-based. 0x1D = ReadRegister, 16-bit address.
uint8_t sx126x_readReg(uint16_t addr) {
  SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  spiSelect(true);
  SPI.transfer(0x1D);
  SPI.transfer((addr >> 8) & 0xFF);
  SPI.transfer(addr & 0xFF);
  SPI.transfer(0x00);              // status byte, discarded
  uint8_t val = SPI.transfer(0x00);
  spiSelect(false);
  SPI.endTransaction();
  return val;
}

void sx126x_writeReg(uint16_t addr, uint8_t val) {
  SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  spiSelect(true);
  SPI.transfer(0x0D);              // WriteRegister
  SPI.transfer((addr >> 8) & 0xFF);
  SPI.transfer(addr & 0xFF);
  SPI.transfer(val);
  spiSelect(false);
  SPI.endTransaction();
}

// ---------------------------------------------------------------
//  TEST 1 - the BUSY pin
//
//  SX126x drives a BUSY line high while processing a command.
//  SX127x has no such pin. On a Heltec V3 with an SX1276 fitted,
//  GPIO13 would be unconnected and float.
// ---------------------------------------------------------------

void testBusyPin() {
  header("TEST 1: BUSY pin behaviour (GPIO 13)");

  Serial.println("SX126x drives this pin. SX127x has no BUSY pin.");
  Serial.println();

  pinMode(LORA_BUSY, INPUT);

  // Sample the resting state
  int high = 0;
  for (int i = 0; i < 100; i++) {
    if (digitalRead(LORA_BUSY)) high++;
    delay(1);
  }

  Serial.print("Resting state, 100 samples: ");
  Serial.print(high);
  Serial.println(" high");

  // A driven pin is stable. A floating pin usually is not.
  bool stable = (high < 5 || high > 95);
  Serial.print("Pin is ");
  Serial.println(stable ? "STABLE (driven)" : "UNSTABLE (likely floating)");

  // Now check whether it responds to a reset, which SX126x does
  Serial.println();
  Serial.println("Pulsing RESET and watching BUSY...");

  pinMode(LORA_RST, OUTPUT);
  digitalWrite(LORA_RST, LOW);
  delay(5);
  digitalWrite(LORA_RST, HIGH);

  // SX126x holds BUSY high briefly while it boots
  unsigned long t0 = millis();
  bool sawHigh = false;
  while (millis() - t0 < 50) {
    if (digitalRead(LORA_BUSY)) { sawHigh = true; break; }
  }
  delay(20);
  bool nowLow = (digitalRead(LORA_BUSY) == LOW);

  Serial.print("BUSY went high after reset: ");
  Serial.println(sawHigh ? "YES" : "no");
  Serial.print("BUSY settled low afterwards: ");
  Serial.println(nowLow ? "YES" : "no");

  if (stable && nowLow) {
    Serial.println();
    Serial.println(">> Consistent with SX126x (+2)");
    score1262 += 2;
  } else if (!stable) {
    Serial.println();
    Serial.println(">> Floating pin is consistent with SX127x (+2)");
    score1276 += 2;
  } else {
    Serial.println();
    Serial.println(">> Inconclusive");
  }
}

// ---------------------------------------------------------------
//  TEST 2 - SX127x version register
//
//  SX127x holds a fixed version byte at register 0x42.
//  SX1276/77/78/79 all return 0x12.
// ---------------------------------------------------------------

void testSX127xVersion() {
  header("TEST 2: SX127x version register (0x42)");

  Serial.println("SX1276/77/78/79 return 0x12 here. SX126x will not.");
  Serial.println();

  uint8_t v = sx127x_readReg(0x42);

  Serial.print("Register 0x42 = 0x");
  if (v < 16) Serial.print("0");
  Serial.println(v, HEX);

  if (v == 0x12) {
    Serial.println();
    Serial.println(">> SX127x VERSION BYTE FOUND (+3)");
    score1276 += 3;
  } else if (v == 0x00 || v == 0xFF) {
    Serial.println();
    Serial.println(">> No response. Not an SX127x. (+1 to SX126x)");
    score1262 += 1;
  } else {
    Serial.println();
    Serial.println(">> Unexpected value. Not an SX127x version byte. (+1)");
    score1262 += 1;
  }
}

// ---------------------------------------------------------------
//  TEST 3 - SX126x register read/write
//
//  0x06B8 is the sync word register on SX126x. If we can write a
//  value and read it back, the command interface is real.
//  An SX127x will not respond to opcode 0x0D / 0x1D at all.
// ---------------------------------------------------------------

void testSX126xRegisters() {
  header("TEST 3: SX126x register read/write (0x06B8)");

  Serial.println("Writing a test value and reading it back.");
  Serial.println("Only an SX126x implements this command interface.");
  Serial.println();

  // Reset first so the chip is in a known state
  digitalWrite(LORA_RST, LOW);
  delay(5);
  digitalWrite(LORA_RST, HIGH);
  delay(20);

  uint8_t original = sx126x_readReg(0x06B8);
  Serial.print("Initial value: 0x");
  if (original < 16) Serial.print("0");
  Serial.println(original, HEX);

  const uint8_t testVal = 0xA5;
  sx126x_writeReg(0x06B8, testVal);
  delay(5);
  uint8_t readBack = sx126x_readReg(0x06B8);

  Serial.print("Wrote 0xA5, read back: 0x");
  if (readBack < 16) Serial.print("0");
  Serial.println(readBack, HEX);

  // restore
  sx126x_writeReg(0x06B8, original);

  if (readBack == testVal) {
    Serial.println();
    Serial.println(">> SX126x COMMAND INTERFACE CONFIRMED (+3)");
    score1262 += 3;
  } else {
    Serial.println();
    Serial.println(">> No SX126x response (+2 to SX127x)");
    score1276 += 2;
  }
}

// ---------------------------------------------------------------
//  TEST 4 - RadioLib driver initialisation
//
//  RadioLib verifies chip identity during begin(). Whichever driver
//  succeeds is strong evidence on its own.
// ---------------------------------------------------------------

void testDrivers() {
  header("TEST 4: RadioLib driver initialisation");

  // --- SX1262 ---
  Serial.print("SX1262 driver ... ");
  int s = radio1262.begin(915.0, 125.0, 7, 5,
                          RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
                          22, 8, 1.8, false);
  if (s == RADIOLIB_ERR_NONE) {
    Serial.println("SUCCESS");
    Serial.println("   (accepted 22 dBm and TCXO 1.8 V - both SX126x-only)");
    score1262 += 3;
  } else {
    Serial.print("failed, code ");
    Serial.println(s);
  }

  delay(100);

  // --- SX1276 ---
  Serial.print("SX1276 driver ... ");
  int s2 = radio1276.begin(915.0, 125.0, 7, 5, 0x12, 17, 8, 0);
  if (s2 == RADIOLIB_ERR_NONE) {
    Serial.println("SUCCESS");
    score1276 += 3;
  } else {
    Serial.print("failed, code ");
    Serial.println(s2);
  }

  Serial.println();
  Serial.println("Error code reference:");
  Serial.println("  -2   = chip not found");
  Serial.println("  -706 = SPI works but chip did not respond correctly");
  Serial.println("  -13  = invalid TX power for this chip");
}

// ---------------------------------------------------------------
//  TEST 5 - transmit power ceiling
//
//  SX1262 accepts up to 22 dBm. SX1276 tops out at 17 (20 with
//  PA_BOOST) and RadioLib rejects anything higher.
// ---------------------------------------------------------------

void testPowerCeiling() {
  header("TEST 5: transmit power ceiling");

  Serial.println("SX1262 max 22 dBm. SX1276 max 17 dBm (20 boost).");
  Serial.println();

  int s = radio1262.setOutputPower(22);
  Serial.print("Requesting 22 dBm ... ");
  if (s == RADIOLIB_ERR_NONE) {
    Serial.println("ACCEPTED");
    Serial.println(">> Only an SX1262 accepts 22 dBm (+2)");
    score1262 += 2;
  } else {
    Serial.print("rejected, code ");
    Serial.println(s);
    Serial.println(">> Consistent with SX127x (+1)");
    score1276 += 1;
  }
}

// ---------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(2500);

  // Peripheral power on
  pinMode(VEXT_PIN, OUTPUT);
  digitalWrite(VEXT_PIN, LOW);
  delay(100);

  pinMode(LORA_CS, OUTPUT);
  digitalWrite(LORA_CS, HIGH);
  pinMode(LORA_RST, OUTPUT);
  digitalWrite(LORA_RST, HIGH);

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  delay(50);

  Serial.println();
  Serial.println("=============================================");
  Serial.println("   LoRa RADIO CHIP IDENTIFICATION");
  Serial.println("   Heltec WiFi LoRa 32 V3");
  Serial.println("=============================================");
  Serial.println("Five independent tests. Verdict at the end.");

  testBusyPin();
  delay(200);

  testSX127xVersion();
  delay(200);

  testSX126xRegisters();
  delay(200);

  testDrivers();
  delay(200);

  testPowerCeiling();

  // ---- Verdict ----
  Serial.println();
  Serial.println("=============================================");
  Serial.println("   VERDICT");
  Serial.println("=============================================");
  Serial.print("SX1262 (SX126x family) score: ");
  Serial.println(score1262);
  Serial.print("SX1276 (SX127x family) score: ");
  Serial.println(score1276);
  Serial.println();

  if (score1262 > score1276 + 2) {
    Serial.println("  >>>  RADIO IS SX1262  <<<");
    Serial.println();
    Serial.println("  Family:      SX126x");
    Serial.println("  Max TX:      22 dBm");
    Serial.println("  RX current:  ~4.6 mA");
    Serial.println("  Library:     RadioLib SX1262");
    Serial.println("  BUSY pin:    required (GPIO 13)");
    Serial.println("  TCXO:        must be set (1.8 V)");
    Serial.println();
    Serial.println("  If the seller listed SX1276, the listing is");
    Serial.println("  wrong. Heltec V2 used SX1276; V3 uses SX1262.");
    Serial.println("  Sellers frequently reuse old V2 descriptions.");

  } else if (score1276 > score1262 + 2) {
    Serial.println("  >>>  RADIO IS SX1276  <<<");
    Serial.println();
    Serial.println("  Family:      SX127x");
    Serial.println("  Max TX:      17 dBm (20 with PA_BOOST)");
    Serial.println("  RX current:  ~10.8 mA");
    Serial.println("  Library:     RadioLib SX1276");
    Serial.println("  BUSY pin:    none");
    Serial.println("  TCXO:        not applicable");
    Serial.println();
    Serial.println("  ACTION REQUIRED: your existing firmware sets");
    Serial.println("  22 dBm and TCXO 1.8 V. Neither is valid here.");
    Serial.println("  Every measurement taken so far needs review.");

  } else {
    Serial.println("  >>>  INCONCLUSIVE  <<<");
    Serial.println();
    Serial.println("  Scores too close to call. Check the physical");
    Serial.println("  marking on the shielded module and send the");
    Serial.println("  full output above for interpretation.");
  }

  Serial.println("=============================================");
  Serial.println();
  Serial.println("Copy everything above and send it back.");
}

void loop() {
  delay(10000);
}
