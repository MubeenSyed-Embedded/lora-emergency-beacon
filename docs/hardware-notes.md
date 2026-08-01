# Hardware Notes — Heltec WiFi LoRa 32 V3

Everything I had to work out the hard way, written down so the next person doesn't.

---

## 1. Identifying the board

This project was planned around a **TTGO LoRa32 T3 V1.6** (ESP32 + SX1276). The hardware that arrived was something else entirely. The first upload failed with:

```
A fatal error occurred: This chip is ESP32-S3, not ESP32. Wrong chip argument?
```

Knowing it was an ESP32-S3 narrowed things down but didn't settle it — several manufacturers ship visually near-identical ESP32-S3 + LoRa boards with mutually incompatible pinouts and different radio silicon.

I first assumed a LilyGO T3-S3 and wrote a probe against its pinout. It found nothing on **either** SPI or I²C. Two independent buses coming back empty was the useful signal: the odds of both peripherals being dead are negligible, so the pin map had to be wrong.

The second probe ([`firmware/00_hardware_id`](../firmware/00_hardware_id)) sweeps candidate pin maps across the common board families rather than assuming any one of them.

What actually settled it was mundane: **the factory firmware's OLED splash screen reads "Heltec."**

### What differed from the plan

| | Documentation assumed | Actual hardware |
|---|---|---|
| Board | TTGO LoRa32 T3 V1.6 | Heltec WiFi LoRa 32 V3 |
| MCU | ESP32 | ESP32-S3FN8 |
| Radio | SX1276 | **SX1262** |
| Arduino library | LoRa (Sandeep Mistry) | **RadioLib** |
| LoRa CS / SCK / MOSI / MISO | 18 / 5 / 27 / 19 | **8 / 9 / 10 / 11** |
| LoRa RST / BUSY / DIO1 | 23 / — / 26 | **12 / 13 / 14** |
| OLED SDA / SCL | 21 / 22 | **17 / 18** |
| OLED power | always on | **gated behind Vext (GPIO 36)** |

The library change is not optional. The `LoRa` library by Sandeep Mistry drives SX127x-family radios only — it cannot talk to an SX1262 under any pin configuration, because the register map and command interface are entirely different.

---

## 2. Pin map

| Signal | GPIO |
|---|---|
| LoRa NSS / CS | 8 |
| LoRa SCK | 9 |
| LoRa MOSI | 10 |
| LoRa MISO | 11 |
| LoRa RST | 12 |
| LoRa BUSY | 13 |
| LoRa DIO1 | 14 |
| OLED SDA | 17 |
| OLED SCL | 18 |
| OLED RST | 21 |
| Vext (peripheral power) | 36 — **LOW = ON** |
| Onboard LED | 35 |
| PRG button | 0 |
| Battery ADC | 1 |

```cpp
SX1262 radio = new Module(8, 14, 12, 13);   // cs, irq, rst, busy
SPI.begin(9, 11, 10, 8);                    // sck, miso, mosi, cs

U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, 21, 18, 17);
```

LoRa GPIOs 8–14 are not broken out to headers — they connect directly to the SX1262 on the PCB.

---

## 3. Gotchas

### Vext gates the OLED

`GPIO 36` controls power to the peripheral rail. **LOW turns it on.** Until you do this, the OLED does not appear on the I²C bus at all — an address scan finds nothing, which looks identical to a wiring fault.

```cpp
pinMode(36, OUTPUT);
digitalWrite(36, LOW);   // peripherals ON
delay(100);
display.begin();
```

Note this is inverted relative to some other Heltec boards (the Tracker uses HIGH = on). Check per board.

### TCXO voltage must be set

The SX1262 uses a temperature-compensated oscillator whose supply voltage has to be configured before the radio will start. On the V3 that is 1.8 V. Omit it and the radio either fails to initialise or runs at badly degraded sensitivity — the second failure mode is quiet and easy to misdiagnose as an antenna or range problem.

```cpp
radio.begin(915.0, 125.0, 7, 5,
            RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
            22, 8, 1.8, false);   // 1.8 = TCXO voltage
```

### DIO2 drives the RF switch

```cpp
radio.setDio2AsRfSwitch(true);
```

Without this the antenna path is not correctly switched between transmit and receive.

### CP210x driver on Windows

The board enumerates through a CP2102 USB-UART bridge. Windows 11 does not ship the virtual COM port driver. Symptom: the device shows up under **Other devices** in Device Manager, often with a yellow warning triangle, and no COM port is ever assigned — so the Arduino IDE port list stays empty.

Install the [Silicon Labs CP210x VCP driver](https://www.silabs.com/developer-tools/usb-to-uart-bridge-vcp-drivers). Extract the zip first — Windows cannot read driver files from inside an archive.

### USB CDC On Boot

Set to **Disabled** for this board. Because serial goes over the CP2102 bridge rather than the ESP32-S3's native USB, enabling CDC routes `Serial` to the wrong interface and the Serial Monitor stays blank.

### Serial Monitor blocks uploads

An open Serial Monitor holds the COM port. The upload then fails with a connection error that looks like a bootloader problem but isn't. Close it before flashing.

### Bootloader entry

If upload stalls at `Connecting......`:

1. Hold **PRG**
2. Press and release **RST**
3. Release **PRG**
4. Upload

### Antenna

Attach before every power-on. Transmitting into an open connector reflects power back into the PA and can destroy it permanently.

---

## 4. The packet-loss bug

The first receiver implementation used RadioLib's blocking `receive()` call and dropped exactly every second packet:

```
RX <- Hello from Board 1 seq=1  | RSSI 0.0 dBm | got 1 missed 0
RX <- Hello from Board 1 seq=3  | RSSI 0.0 dBm | got 2 missed 1
RX <- Hello from Board 1 seq=5  | RSSI 0.0 dBm | got 3 missed 2
```

The alternating pattern is the diagnostic. `receive()` blocks, so the radio was not listening at all while the sketch printed to serial and pushed a full 128×64 frame to the OLED over I²C. That housekeeping took long enough to fall permanently out of phase with the 2-second transmit interval.

### The fix

Three changes:

**Interrupt-driven reception.** DIO1 fires on packet arrival. The ISR sets a flag and does nothing else — no serial, no I²C, no allocation.

```cpp
volatile bool packetFlag = false;
void IRAM_ATTR onPacket() { packetFlag = true; }

radio.setDio1Action(onPacket);
radio.startReceive();
```

**Re-arm before housekeeping, not after.** This single ordering decision is the actual fix.

```cpp
if (packetFlag) {
  packetFlag = false;
  radio.readData(msg);
  radio.startReceive();     // back on air FIRST
  // ... only now do serial, OLED, parsing
}
```

**Rate-limit the display.** The OLED redraws at most twice per second. Pushing a full frame over I²C is slow and there is no value in doing it faster than a human can read.

Result: `missed 0` sustained over 82 consecutive packets.

---

## 5. RSSI reads 0.0 dBm

Not a bug — receiver saturation. At close range with meaningful transmit power the SX1262's RSSI measurement pegs at its ceiling. SNR still reports correctly.

Fix: separate the boards by a few metres, or drop transmit power for bench work. A reading that varies (−17, −19, −21) is tracking a real signal; one that sits at exactly 0.0 is clamped.

---

## 6. Sources

- [Heltec WiFi LoRa 32 V3 product page](https://heltec.org/project/wifi-lora-32-v3/)
- [Heltec documentation](https://wiki.heltec.org/docs/devices/open-source-hardware/esp32-series/lora-32/wifi-lora-32-v3/)
- [RadioLib](https://github.com/jgromes/RadioLib)
- [U8g2](https://github.com/olikraus/u8g2)
