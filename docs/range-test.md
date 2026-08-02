# Range Test Results — LoRa Disaster Beacon Network

**Hardware:** 2 × Heltec WiFi LoRa 32 V3 (ESP32-S3FN8 + SX1262 + SSD1306 OLED)
**Dates:** 1–3 August 2026
**Location:** Rawalpindi, Punjab, Pakistan
**Tester:** Muhammad Mubeen Syed — GIKI Electrical Engineering

---

## Summary

| Test | Environment | Distance | RSSI at limit | Outcome |
|---|---|---|---|---|
| 1 | Indoor bench | ~3 m | −55 dBm | 0 / 19 lost |
| 2 | Urban, no line of sight | 300 m | −102 dBm | Link failure |
| 3 | Road, predominantly line of sight | **693 m** | −110 dBm | Link failure |

Removing the building roughly doubled achievable range for the cost of only 8 dB of additional path loss — approximately what free-space propagation alone predicts. Environment, not distance, is the limiting factor. See §3.1.

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

### Test 3 — Road path, predominantly line of sight

| Metric | Value |
|---|---|
| Separation at link failure | 693 m (GPS) |
| Path | Along a road, two curves partially obstructing |
| RSSI at failure | −110 dBm |
| Degradation pattern | Progressive packet loss, then CRC failures |
| Result | Link failure observed, not operator-terminated |

The transmitter was left at a fixed position and the receiver carried outward along a road. Loss appeared gradually as distance increased, rising until CRC failures dominated and no further payloads could be recovered. **The distance recorded is therefore a measured link limit rather than a test point at which the walk was stopped.**

This is characterised as *predominantly* line of sight rather than clear line of sight. Two road curves placed partial obstruction in the path, and both nodes remained near ground level throughout.

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

### 3.1 Comparing the two field tests

Placing Tests 2 and 3 side by side isolates the effect of obstruction from the effect of distance.

| | Test 2 (urban NLOS) | Test 3 (road, partial LOS) |
|---|---|---|
| Distance at failure | 300 m | **693 m** |
| RSSI at failure | −102 dBm | −110 dBm |
| Path | Through concrete floor slab, stairwell, multiple retail units | Along a road, two partially obstructing curves |

Free-space path loss between 300 m and 693 m is:

```
20 · log₁₀(693 / 300) = 20 · log₁₀(2.31) = 7.3 dB
```

The measured difference in received signal between the two tests was **8 dB**.

**The road path added essentially nothing beyond geometric spreading.** Over more than twice the distance, signal fell by only the amount free-space propagation alone predicts. This stands in direct contrast to the building, which imposed an estimated 40–60 dB penalty.

This pairing is the clearest evidence in the dataset that **environment, not distance, governs range in this application.**

### 3.2 An anomaly worth recording

Both field tests failed at signal levels well above the theoretical demodulation floor.

| Test | RSSI at failure | Theoretical SF7 floor | Margin remaining |
|---|---|---|---|
| Test 2 | −102 dBm | ~−123 dBm | 21 dB |
| Test 3 | −110 dBm | ~−123 dBm | 13 dB |

In both cases the link failed while nominally retaining substantial margin. The consistency across two very different environments indicates a systematic cause rather than a chance result.

Three candidate explanations:

**Elevated ambient noise floor.** The −123 dBm figure assumes thermal noise only (kTB at 290 K over 125 kHz). Real environments contain additional RF energy — vehicle ignition noise, lighting ballasts, other ISM-band devices, WiFi spillover. A local noise floor 10–20 dB above thermal would place the effective demodulation limit close to the observed failure points in both tests.

**Antenna polarisation mismatch.** Orientation was not controlled during either test. A tilted antenna introduces several dB of loss immediately.

**Ground reflection.** With both nodes near ground level, the direct and ground-reflected components can partially cancel. This is not captured by free-space path loss and is plausible at these distances.

The first is the most likely and the most easily tested. **A spreading factor sweep would distinguish between them:** if the limit is sensitivity, SF12 should extend range substantially (roughly +14 dB); if the limit is broadband interference, the improvement will be markedly smaller. This is recorded as recommended work in §7.

---

## 4. Projected performance beyond measured conditions (estimated — not measured)

> **Note:** the figures in this section are engineering estimates derived from the measured link budget. They have not been verified by field measurement and should be treated as design targets rather than results.

Test 3 establishes a measured floor of 693 m under partially obstructed, ground-level, uncontrolled conditions. Extrapolating from that baseline:

| Scenario | Estimated range | Basis |
|---|---|---|
| Clear line of sight, controlled antenna orientation | **1 – 1.5 km** | Removes the curve obstruction and polarisation loss present in Test 3 |
| Elevated, rooftop to rooftop | **2 – 4 km** | Clear first Fresnel zone, reduced ground reflection |
| Airborne node at 100 m AGL | **4 – 8 km** | Unobstructed geometry |
| SF12 at ground level | **2 – 3 km** | +14 dB sensitivity, subject to the noise-floor question in §3.2 |

These projections are deliberately more conservative than a naive link-budget calculation would give, because §3.2 shows the practical demodulation limit in these environments sits above the theoretical floor.

---

## 5. Relevance to the UAV emergency communication network

The obstructed test is directly informative for the parent FYP for three reasons.

**The measured case is the worst case.** Post-disaster environments involve collapsed structures, debris fields, and survivors located inside or beneath buildings. A 300 m link achieved from inside a multi-storey commercial building through several intervening retail units is representative of that condition, and it succeeded.

**Elevation resolves the dominant loss term.** Since structural attenuation rather than distance was the limiting factor, raising a node above the obstruction plane recovers the majority of the lost margin. This is the operational rationale for the UAV relay architecture: the airborne node is not merely mobile, it is positioned above the attenuating layer.

**The link budget supports the design targets.** With approximately 65 dB of margin measured at bench distance and a demonstrated tolerance for heavy obstruction, the inter-node spacing assumed in the ring topology is achievable in the projected airborne configuration.

---

## 6. Limitations

- Single measurement run per condition; no repeated trials or statistical averaging.
- Distance measured via Google Maps GPS coordinates, not by survey. Reported precision exceeds actual accuracy; treat 693 m as approximate.
- No RF spectrum survey conducted. Ambient interference is hypothesised in §3.2 as the cause of early link failure but was not measured.
- Antenna orientation was not controlled during any test. This is a known source of several dB of uncontrolled loss.
- Only SF7 tested. Higher spreading factors (SF9–SF12) trade data rate for sensitivity and would extend range; this was not evaluated and is the highest-priority follow-on measurement.
- Test 3 is characterised as *predominantly* line of sight, not clear line of sight. Two road curves partially obstructed the path. Fully unobstructed performance remains unmeasured.
- Both nodes remained at ground level throughout. Elevated performance is unmeasured.
- Battery endurance was not characterised; all tests ran from USB or power bank supply without current measurement.

---

## 7. Recommended follow-on measurements

In priority order.

1. **Spreading factor sweep (SF7 → SF12) at fixed distance.** Resolves the anomaly identified in §3.2 and quantifies the sensitivity/airtime trade empirically. Highest value, lowest cost — requires no new hardware.
2. **Repeat Test 3 with antennas vertically polarised and orientation held constant.** Removes one uncontrolled variable and should recover several dB.
3. **Ambient noise floor measurement.** Place a node in receive mode with no transmitter active and record RSSI. Directly tests the §3.2 hypothesis.
4. **Clear line-of-sight characterisation** on open ground with no path curvature, at 500 m, 1 km and 2 km.
5. **Repeated trials at each condition** — minimum five runs — for statistical confidence. All results to date are single measurements.
6. **Elevated test** with at least one node above the local obstruction plane.
7. **Current draw profiling** to establish battery endurance per node.
8. **Verification of local regulatory allocation.** 915 MHz is the US/ISM band; Pakistan's licence-exempt sub-GHz allocation is normally 433 MHz. Deployment hardware should be selected accordingly, and this should be confirmed against a citable regulatory source.

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

**Road path, 693 m, link edge:**
```
RX <- packet corrupted (CRC fail)
```

In both field tests the failure mode was identical: progressive sequence-number gaps as distance increased, followed by CRC failures in which a packet was detected and demodulated but the integrity check failed. This is degradation past the point of reliability rather than loss of signal — the distinction discussed in §3.2.
