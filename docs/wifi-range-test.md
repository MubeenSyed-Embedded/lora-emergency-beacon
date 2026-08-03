# WiFi Range Test — Survivor Beacon Access Point

**Project:** LoRa Emergency Beacon
**Author:** Muhammad Mubeen Syed — Electrical Engineering, GIKI
**Date:** 3 August 2026
**Location:** Rawalpindi, Punjab, Pakistan
**Raw data:** [`wifi-range-data.csv`](wifi-range-data.csv)

---

## 1. Purpose

The system contains two independent wireless links:

```
Phone ──WiFi 2.4 GHz──► Node 1 ──LoRa 915 MHz──► Node 2 ──WiFi 2.4 GHz──► Phone
```

Earlier testing characterised only the LoRa hop. This test characterises the WiFi hop, which determines how far a survivor can be from a beacon and still use it.

That distance sets the coverage radius of a single node and therefore the density of nodes required to cover an area. It is arguably the more operationally significant of the two measurements: a beacon whose LoRa signal reaches 693 m is of no use to a survivor who cannot connect to it in the first place.

---

## 2. Method

### 2.1 Configuration

| Parameter | Value |
|---|---|
| Access point | Heltec WiFi LoRa 32 V3 (ESP32-S3FN8) |
| Firmware | `wifi_range_test.ino` |
| SSID | `SurvivorBeacon`, open network |
| Band | 2.4 GHz |
| AP transmit power | Set to maximum via `esp_wifi_set_max_tx_power(78)` |
| Beacon height | 3 ft (0.91 m) above ground |
| Client device | iPhone 14 Pro Max |
| Client position | Held at waist height, carried by the operator |
| Distance method | GPS |
| Stride length (recorded) | 0.76 m |
| Path | Outdoor, clear |

### 2.2 Measurement definition

RSSI was measured **at the access point**, not at the phone, using:

```cpp
wifi_sta_list_t staList;
esp_wifi_ap_get_sta_list(&staList);
int rssi = staList.sta[0].rssi;
```

This is the signal strength of the phone's transmission as received by the beacon. It characterises the uplink path — which is the direction that carries a survivor's message and therefore the direction that matters.

The value was served back to the phone on the test page, allowing it to be read in the field without a tethered laptop.

### 2.3 Tests performed

**Test A — page reload.** Connection maintained; the page at `192.168.4.1` was reloaded at each distance.

**Test B — fresh connection.** WiFi disabled and re-enabled, forcing full re-association, DHCP, and captive portal detection.

Three thresholds were checked independently at each point: SSID visibility, successful association, and successful page load.

---

## 3. Results

### 3.1 Raw measurements

| Distance (m) | RSSI (dBm) | SSID visible | Connects | Page loads |
|---|---|---|---|---|
| 10 | −72 | Y | Y | Y |
| 20 | −65 | Y | Y | Y |
| 30 | −74 | Y | Y | Y |
| 40 | −78 | Y | Y | Y |
| 50 | −74 | Y | Y | Y |
| 60 | −83 | Y | Y | Y |
| 70 | −80 | Y | Y | Y |
| 80 | −90 | Y | Y | Y |
| **84** | **−92** | **N** | **N** | **N** |

### 3.2 Range limits

| Threshold | Distance |
|---|---|
| Maximum distance page loaded | **80 m** |
| Maximum distance associated | **80 m** |
| Maximum distance SSID visible | **80 m** |
| First distance of total failure | **84 m** |

### 3.3 Test A and Test B produced identical results

This was not expected. The working hypothesis before testing was that association would persist beyond the point at which a page could be loaded, because association requires less sustained throughput than an HTTP transaction.

**The three thresholds failed together.** No distance existed at which the phone remained associated but could not load the page, and none at which the SSID was visible but association failed.

The practical implication is useful: **SSID visibility is a reliable proxy for usable connectivity in this configuration.** A survivor who can see the network in their WiFi list can use it. This simplifies both field diagnosis and any future user guidance.

---

## 4. Analysis

### 4.1 Free-space path loss reference

The free-space path loss between two isotropic antennas is:

```
FSPL(dB) = 20·log₁₀(d) + 20·log₁₀(f) + 32.44
```

with `d` in kilometres and `f` in megahertz. The constant 32.44 absorbs the unit conversion. Taking the 2.4 GHz band centre at 2437 MHz:

**Wavelength:**
```
λ = c / f = 299,792,458 / 2.437×10⁹ = 0.1230 m = 12.3 cm
```

**Worked example at 84 m:**
```
20·log₁₀(0.084)  = 20 × (−1.076) = −21.5 dB
20·log₁₀(2437)   = 20 × 3.387    = +67.7 dB
constant                          = +32.4 dB
                                    ─────────
FSPL                              =  78.7 dB
```

### 4.2 Measured loss versus free-space prediction

Anchoring the model at the 10 m measurement:

| Distance (m) | Measured RSSI | FSPL (dB) | Predicted RSSI | Deviation |
|---|---|---|---|---|
| 10 | −72 | 60.2 | −72.0 | 0.0 (anchor) |
| 20 | −65 | 66.2 | −78.0 | **+13.0** |
| 30 | −74 | 69.7 | −81.5 | +7.5 |
| 40 | −78 | 72.2 | −84.0 | +6.0 |
| 50 | −74 | 74.2 | −86.0 | **+12.0** |
| 60 | −83 | 75.7 | −87.6 | +4.6 |
| 70 | −80 | 77.1 | −88.9 | +8.9 |
| 80 | −90 | 78.2 | −90.1 | +0.1 |
| 84 | −92 | 78.7 | −90.5 | −1.5 |

**Total measured loss, 10 m to 84 m:** 20.0 dB
**Free-space prediction over the same interval:** 18.5 dB
**Difference:** +1.5 dB

The overall attenuation is within 1.5 dB of free-space theory. The path was effectively unobstructed and the measurement method is sound.

Note that the deviation column shows large positive values at intermediate distances. This is a consequence of anchoring at 10 m, which — as §4.4 shows — appears itself to be a locally depressed point. The endpoint agreement is the more meaningful comparison.

### 4.3 Path loss exponent

Real environments are described by:

```
RSSI(d) = RSSI(d₀) − 10·n·log₁₀(d/d₀)
```

where `n` is the path loss exponent: 2 in free space, higher where obstructions or ground effects dominate.

A least-squares fit of RSSI against log₁₀(d):

```
RSSI = −22.40 · log₁₀(d) − 42.50
```

Since the slope equals −10n:

```
n = 22.40 / 10 = 2.24
```

| n | Environment |
|---|---|
| 2.0 | Free space |
| **2.24** | **This measurement** |
| 2–3 | Outdoor with ground reflection |
| 3–5 | Indoor, obstructed |

**n = 2.24** places the environment marginally worse than free space, consistent with an open outdoor path with a ground plane present.

**Goodness of fit:**

```
R² = 0.635
RMS residual = 4.94 dB
```

R² of 0.635 is poor. The trend is real but the scatter about it is substantial, and §4.4 addresses why.

### 4.4 Non-monotonic behaviour

Signal strength increased with distance at three points:

| Interval | Change |
|---|---|
| 10 → 20 m | **+7 dB (stronger)** |
| 40 → 50 m | **+4 dB (stronger)** |
| 60 → 70 m | **+3 dB (stronger)** |

Residuals about the fitted trend:

| Distance (m) | Residual (dB) |
|---|---|
| 10 | −7.1 |
| 20 | **+6.6** |
| 30 | +1.6 |
| 40 | +0.4 |
| 50 | **+6.6** |
| 60 | −0.7 |
| 70 | +3.8 |
| 80 | −4.9 |
| 84 | −6.4 |

The residuals alternate rather than scattering randomly, which suggests a systematic mechanism rather than pure noise.

#### Candidate explanation: two-ray ground reflection

Over a reflective ground plane the receiver sees two signals — the direct path and the ground-reflected path. They arrive with a path length difference and therefore a phase difference. Where they arrive in phase they add; where out of phase they partially cancel. The result is a standing-wave pattern in distance.

For heights `h_t` (transmitter) and `h_r` (receiver), the path difference at distance `d` (for `d ≫ h`) is approximately:

```
Δl ≈ 2·h_t·h_r / d
```

**Breakpoint distance** — beyond which the two rays combine destructively and loss rises steeply toward n ≈ 4:

```
d_b = 4·h_t·h_r / λ
```

With h_t = 0.91 m (3 ft beacon), h_r ≈ 1.0 m (waist-held phone), λ = 0.123 m:

```
d_b = (4 × 0.91 × 1.0) / 0.123 = 29.7 m
```

**Constructive maxima** occur where Δl = (2k−1)·λ/4:

```
d_peak = 2·h_t·h_r / ((2k−1)·λ/4)
```

| k | Predicted peak (m) |
|---|---|
| 1 | 59.5 |
| 2 | **19.8** |
| 3 | 11.9 |
| 4 | 8.5 |

**The k = 2 prediction falls at 19.8 m. The strongest measured signal in the entire dataset is at 20 m.**

That is a close agreement, and it is the reason this explanation is offered at all.

#### Why this remains a hypothesis, not a conclusion

Honesty requires the counter-evidence be stated with equal prominence:

- **The other predicted peaks do not match.** A constructive maximum is predicted at 59.5 m; the measurement at 60 m is a local *minimum* (−83 dBm). The observed peaks at 50 m and 70 m correspond to no predicted maximum.
- **The 20 m match may be coincidence.** With nine points spaced 10 m apart and predicted peaks spaced irregularly, one alignment is not improbable by chance.
- **Receiver height was not controlled.** The phone was carried by hand while walking, so h_r varied continuously. The model assumes a fixed height; a varying one smears the pattern and would shift every predicted null and peak.
- **A single measurement was taken at each distance.** No repeat, no averaging, no error bars.
- **Body shadowing was uncontrolled.** The operator's torso was between the phone and the beacon for part of the walk. Orientation changed as the operator turned to read the screen. The human body attenuates 2.4 GHz strongly, and several dB of variation from this source alone is plausible.

**The last point is the most likely alternative explanation**, and it cannot be distinguished from ground reflection with the present dataset.

#### The test that would resolve it

Repeat the measurement with the phone mounted at a fixed height on a non-conductive support (a wooden or plastic pole), oriented consistently, with five readings averaged at each distance and readings taken every 5 m rather than every 10 m.

If the ripple persists with the same period, ground reflection is confirmed. If it disappears, the variation was operator-induced.

---

## 5. Comparison with the LoRa link

| Link | Frequency | Range | Ratio |
|---|---|---|---|
| WiFi, phone to beacon | 2.4 GHz | **80 m** | 1× |
| LoRa, beacon to base | 915 MHz | **693 m** | **8.7×** |

Both measured outdoors, both at ground level, both with the transmitter at similar power class.

This ratio is the quantitative justification for the system architecture. Two mechanisms contribute:

**Frequency.** 2.4 GHz has a 12.3 cm wavelength against 32.8 cm at 915 MHz. Shorter waves are absorbed and scattered more readily and diffract less around obstacles. Free-space loss alone is 8.5 dB higher at 2.4 GHz for the same distance:

```
20·log₁₀(2437/915) = 20 × 0.4255 = 8.5 dB
```

**Modulation.** LoRa's chirp spread spectrum provides processing gain, allowing demodulation at signal levels far below what a WiFi receiver requires. WiFi needs roughly −90 dBm to function; LoRa at SF7 operates to approximately −123 dBm and at SF12 to approximately −137 dBm.

The architecture uses each where it is strong: WiFi for the last tens of metres to a device the survivor already owns, LoRa for the hundreds of metres that WiFi cannot cross.

---

## 6. Deployment implications

### 6.1 Coverage per node

Treating coverage as circular with radius 80 m:

```
A = π·r² = π × 80² = 20,106 m² ≈ 2.01 hectares
```

### 6.2 Nodes required for area coverage

For a 1 km × 1 km area (1,000,000 m²), ignoring overlap:

```
1,000,000 / 20,106 ≈ 50 nodes
```

Circles cannot tile a plane without gaps. Using a hexagonal packing arrangement, the effective area per node is:

```
A_hex = (3√3/2) · r²  with the inscribed radius equal to the coverage radius
      ≈ 2.598 × 80² / (something less than the full circle)
```

In practice, allowing for the overlap required for reliable coverage, **55 to 65 nodes** would be needed per square kilometre.

### 6.3 What this means for the concept

At roughly $25 per node, 60 nodes is about $1,500 per square kilometre. That is not prohibitive for a pre-positioned municipal deployment, but it is a meaningful cost and it assumes nodes survive the event.

**More importantly, it reframes the system.** With an 80 m radius, this is a *per-building* or *per-block* solution, not an area solution. Realistic deployment would place beacons at known gathering points — schools, mosques, hospitals, community centres — rather than attempting blanket coverage.

The indoor figure, once measured, will be the more relevant number for that use case.

---

## 7. Limitations

- **Single measurement per distance.** No repeats, no averaging, no error bars. The 4.94 dB RMS residual could be reduced substantially by averaging.
- **10 m sampling interval.** Too coarse to resolve an interference pattern with the period the two-ray model predicts. 5 m spacing is needed.
- **Receiver height uncontrolled.** Phone carried by hand while walking; height and orientation varied continuously.
- **Body shadowing uncontrolled and unquantified.** The operator's body was in the path for an unknown fraction of the measurements.
- **Distance by GPS.** At 10–84 m, GPS error of ±5–10 m represents up to 20% uncertainty at the near points. Pace counting would have been more accurate at this scale; stride length was recorded (0.76 m) but GPS was used.
- **Frequency assumed, not measured.** 2437 MHz was taken as the band centre. The actual channel was not recorded, introducing up to ±0.3 dB in the FSPL calculation — negligible relative to other uncertainties.
- **Indoor, through-wall, and through-floor cases not yet measured.** These are the operationally relevant conditions and remain outstanding.
- **AP transmit power not recorded numerically.** Set to maximum in firmware, but the reported value was not noted.

---

## 8. Recommended follow-on work

In priority order:

1. **Through-floor and through-wall measurement.** The disaster use case involves survivors inside structures. This is the missing number that matters most.
2. **Controlled repeat of the outdoor walk.** Phone at fixed height on a non-conductive mast, five readings averaged per point, 5 m spacing. Resolves the §4.4 ambiguity.
3. **Indoor corridor characterisation.** Establishes the realistic per-building coverage figure.
4. **Beacon height sweep.** Measure at 0.9 m, 2 m, and 4 m. The two-ray model predicts the breakpoint scales with height; raising the beacon should extend range disproportionately.
5. **Antenna orientation study.** Quantify the loss from a phone held at various angles relative to the beacon.

---

## 9. Summary

| Metric | Value |
|---|---|
| Maximum usable range (outdoor, clear) | **80 m** |
| Total failure distance | 84 m |
| RSSI at maximum usable range | −90 dBm |
| Path loss exponent | 2.24 |
| Agreement with free-space over full interval | within 1.5 dB |
| Fit quality (R²) | 0.635 |
| Coverage area per node | ~2.0 hectares |
| Ratio to LoRa link range | 1 : 8.7 |

The WiFi link behaves close to free-space theory over an open outdoor path, with an 80 m usable radius. Substantial point-to-point scatter was observed and two candidate explanations are offered without a conclusion being drawn between them, pending a controlled repeat.

The 8.7× disparity between the WiFi and LoRa link ranges is the central quantitative result, and it validates the two-technology architecture directly.
