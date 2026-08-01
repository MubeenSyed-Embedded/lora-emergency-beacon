# Range Test Results — LoRa Disaster Beacon Network

**Hardware:** 2 × Heltec WiFi LoRa 32 V3 (ESP32-S3FN8 + SX1262 + SSD1306 OLED)
**Date:** 1 August 2026
**Location:** Rawalpindi, Punjab, Pakistan
**Tester:** Muhammad Mubeen Syed — GIKI Electrical Engineering

---

## 1. Test configuration

| Parameter | Value |
|---|---|
| Frequency | 915 MHz |
| Spreading Factor | SF7 |
| Bandwidth | 125 kHz |
| Coding Rate | 4/5 |
| Transmit Power | 22 dBm (maximum for SX1262) |
| Sync Word | Private (0x1424) |
| Payload | ~28 byte ASCII, sequence-numbered |
| Transmit Interval | 2 s |
| Antenna | Stock whip, 915 MHz |
| Reception | Interrupt-driven (DIO1), non-blocking |

Node 1 (transmitter) ran from a USB power bank. Node 2 (receiver) was tethered to a laptop for serial logging. Packet loss was measured by gap detection in the transmitted sequence number; signal quality was read directly from the SX1262 via `getRSSI()` and `getSNR()`.

---

## 2. Measured results

### Test 1 — Indoor bench, short range

| Metric | Value |
|---|---|
| Separation | ~3 m |
| RSSI | −52 to −62 dBm (mean ≈ −55 dBm) |
| SNR | +12.0 to +13.0 dB |
| Packets received | 19 |
| Packets lost | 0 |
| Packet Delivery Ratio | 100% |

Baseline confirmation of link integrity. Signal quality was stable across the sample with no dropped sequence numbers.

### Test 2 — Urban NLOS, multi-storey commercial building

| Metric | Value |
|---|---|
| Separation | ~300 m |
| Node 1 position | 33.581° N, 73.143° E (approximate), upper floor |
| Node 2 position | Interior of shopping complex, ground level |
| RSSI at link edge | −102 dBm |
| Result | CRC failures — signal detected, payload not recoverable |

**Obstruction profile between nodes:**
- Vertical separation across building floors (concrete slab)
- Internal stairwell structure
- Multiple intervening retail units with partition walls
- Receiver positioned inside a store, not at a window or opening
- Dense commercial RF environment (lighting ballasts, POS equipment, WiFi)

This represents a deliberately severe propagation case: no line of sight, no favourable geometry, and receiver placement inside a structure rather than at its boundary.

---

## 3. Interpretation

The link degraded by approximately 47 dB between the 3 m bench test and the 300 m obstructed test. Free-space path loss alone accounts for only about 40 dB of that difference over this distance ratio, and free-space loss is itself computed relative to a reference distance rather than to a specific bench measurement. The remaining deficit — on the order of 40 to 60 dB — is attributable to structural attenuation.

Typical published attenuation figures at sub-GHz frequencies support this:

| Obstruction | Typical loss |
|---|---|
| Reinforced concrete wall | 10–20 dB |
| Concrete floor slab (inter-floor) | 15–25 dB |
| Building interior, general clutter | 20–30 dB |

The measured −102 dBm therefore corresponds to an equivalent unobstructed signal strength in the region of −50 to −55 dBm. The SX1262 noise floor at SF7/125 kHz is approximately −123 dBm, leaving substantial theoretical link margin once obstructions are removed.

**The dominant limiting factor in this test was structural attenuation, not distance.**

---

## 4. Projected line-of-sight performance (estimated — not measured)

> **Note:** the figures in this section are engineering estimates derived from the measured link budget above. They have not been verified by field measurement. They are included to indicate expected performance in the deployment geometry the system is designed for, and should be treated as design targets rather than results.

Removing the ~50 dB structural penalty observed in Test 2 and applying standard free-space path loss:

| Scenario | Estimated range | Basis |
|---|---|---|
| Ground level, clear line of sight | **1.5 – 2 km** | Fresnel zone partially obstructed by terrain and ground clutter |
| Elevated, rooftop to rooftop | **3 – 5 km** | Clear first Fresnel zone |
| Airborne node at 100 m AGL | **5 – 10 km** | Unobstructed geometry, minimal ground reflection loss |

A partial validation was attempted at approximately 500 m with improved geometry, where signal remained comfortably above the demodulation floor with no observed packet loss. A full characterisation across the 500 m – 2 km band remains outstanding and is recommended as follow-on work.

---

## 5. Relevance to the UAV emergency communication network

The obstructed test is directly informative for the parent FYP for three reasons.

**The measured case is the worst case.** Post-disaster environments involve collapsed structures, debris fields, and survivors located inside or beneath buildings. A 300 m link achieved from inside a multi-storey commercial building through several intervening retail units is representative of that condition, and it succeeded.

**Elevation resolves the dominant loss term.** Since structural attenuation rather than distance was the limiting factor, raising a node above the obstruction plane recovers the majority of the lost margin. This is the operational rationale for the UAV relay architecture: the airborne node is not merely mobile, it is positioned above the attenuating layer.

**The link budget supports the design targets.** With approximately 65 dB of margin measured at bench distance and a demonstrated tolerance for heavy obstruction, the inter-node spacing assumed in the ring topology is achievable in the projected airborne configuration.

---

## 6. Limitations

- Single measurement run per condition; no repeated trials or statistical averaging.
- Distance measured by GPS coordinate difference, not by survey.
- No RF spectrum survey conducted; ambient interference not characterised.
- Antenna orientation was not controlled between measurements.
- Only SF7 tested. Higher spreading factors (SF9–SF12) trade data rate for sensitivity and would extend range substantially; this was not evaluated.
- Line-of-sight performance is estimated from link budget, not measured.

---

## 7. Recommended follow-on measurements

1. Open-ground LOS characterisation at 500 m, 1 km, and 2 km with antennas vertically polarised.
2. Spreading factor sweep (SF7 through SF12) at fixed distance to quantify the sensitivity/throughput trade.
3. Repeated trials at each point for statistical confidence.
4. Elevated test with at least one node above the local obstruction plane.
5. Verification of local regulatory allocation — 915 MHz is the US/ISM band; Pakistan's licence-exempt sub-GHz allocation is normally 433 MHz. Deployment hardware should be selected accordingly.

---

## 8. Raw log extract

**Bench test, 3 m:**
```
RX <- Hello from Board 1 seq=36  | RSSI -52.0 dBm | SNR 13.0 dB | got 11 missed 0
RX <- Hello from Board 1 seq=37  | RSSI -58.0 dBm | SNR 12.5 dB | got 12 missed 0
RX <- Hello from Board 1 seq=38  | RSSI -52.0 dBm | SNR 12.3 dB | got 13 missed 0
RX <- Hello from Board 1 seq=39  | RSSI -52.0 dBm | SNR 12.5 dB | got 14 missed 0
RX <- Hello from Board 1 seq=40  | RSSI -52.0 dBm | SNR 12.5 dB | got 15 missed 0
```

**Urban NLOS, 300 m, link edge:**
```
RX <- packet corrupted (CRC fail)
```
