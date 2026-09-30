# Bill of Materials — 36V

Everything needed to build the **36V** variant of AutoLee. See [Wiring](../wiring.md) for
how it all connects, and the [README](../../README.md) for the safety warning **before**
you build or operate it. Building the 24V variant instead? See
[../24V/bill-of-materials.md](../24V/bill-of-materials.md).

> ⚠️ **Mains voltage.** This variant brings 230 V AC into a printed PSU casing. Read the
> [mains voltage warning](../../README.md#-mains-voltage-warning--36v-variant) before you build it.

> ⚠️ **Still open:** the exact M2.5×10 screw count for the XT60 female mount (see
> Bolts/Screws below). Everything else not explicitly called out as 36V-specific is assumed
> identical to the 24V build; double-check that assumption too before relying on it.
>
> **Power architecture differs from the 24V build, not just the voltage:** the 24V variant
> uses an external DC power brick into a 2.5mm barrel jack. The 36V variant instead brings AC
> mains into the enclosure via a panel-mounted IEC C14 inlet, feeding an internal 36V switching
> PSU module (#7) — so the "DC Power Jack" row is replaced, not just re-rated. Inside the
> machine the rails are **36 V → 24 V → 5 V**: 36 V drives the motor (TMC5160 HVIN) and feeds a
> 36→24 V step-down (#18); the 24 V rail feeds the fan, the TMC5160's control-connector 24V pin
> and the 5 V regulator (#3) for the ESP32-C6. See [Wiring](../wiring.md#36v-power-rails).

> **Support this project:** The product links below are affiliate links. If you purchase through them, I earn a small commission at no extra cost to you — it's a simple way to help fund continued development of AutoLee. Thank you!

### Electronics

| # | Component | Specs | Link |
|---|-----------|-------|------|
| 1 | WaveShare 1.47" ESP32-C6 | Touchscreen controller & UI | [Amazon.se](https://www.amazon.se/dp/B0F8B845Y6?tag=kldesign-21) · [Amazon.com](https://www.amazon.com/dp/B0FC5LWVXG?tag=kldesign00-20) |
| 2 | TMC5160T Plus | Silent stepper driver with StallGuard2 | [Amazon.se](https://www.amazon.se/dp/B0D5HQWW1C?tag=kldesign-21) · [Amazon.com](https://www.amazon.com/dp/B0CHFK7VBL?tag=kldesign00-20) |
| 3 | Step-down → 5 V | Switchregulator step-down 5–72 V → 5 V (Pololu #5267, Electrokit #41036155). Fed from the **24 V rail**, not straight from 36 V — this is the regulator upstream's wiring diagram shows | [Electrokit](https://www.electrokit.com/en/switchregulator-step-down-5-72v-in-/-5v-ut) · [DigiKey](https://www.digikey.com/en/products/detail/pololu/5267/28723067) |

### Mechanical

| # | Component | Specs | Link |
|---|-----------|-------|------|
| 4 | NEMA 23 Stepper Motor | 2.4 Nm, 4.0 A, 57×57×82 mm, 8 mm shaft | [Amazon.se](https://www.amazon.se/dp/B091C37FJ2?tag=kldesign-21) · [Amazon.com](https://www.amazon.com/dp/B091C37FJ2?tag=kldesign00-20) |
| 5 | Shaft Coupling | Motor-to-leadscrew (8 mm to 10 mm) | [Amazon.se](https://www.amazon.se/dp/B07CLLW7Z3?tag=kldesign-21) · [Amazon.com](https://www.amazon.com/dp/B08QV1QN81?tag=kldesign00-20) |
| 6 | Ball Screw Kit SFU1605 250 mm | 250mm SFU1605 BK12/BF12 10 mm Shaft | [Amazon.de](https://www.amazon.de/dp/B08WRJRM22?tag=kldesign-21) · [Amazon.com](https://www.amazon.com/dp/B09BQSWPM4?tag=kldesign00-20) |

### Power

| # | Component | Specs | Link |
|---|-----------|-------|------|
| 7 | Power Supply | ANGEEK 36 V 10 A 360 W switching DC power supply, AC 100–240 V in, open frame — enclosed in the printed PSU casing. **Set its input voltage selector to your mains voltage before first power-on** (230 V in Europe, 110 V in the US): set to 110 V and plugged into 230 V, it is destroyed | [Amazon.se](https://www.amazon.se/dp/B0BX2HH4LX?tag=kldesign-21) · [Amazon.com](https://www.amazon.com/dp/B0DQ87D9PT?tag=kldesign00-20) |
| 8 | On/Off Switch | Panel mount | [Amazon.se](https://www.amazon.se/dp/B07GDCNXKP?tag=kldesign-21) · [Amazon.com](https://www.amazon.com/dp/B078KBC5VH?tag=kldesign00-20) |
| 9 | Mains Inlet | IEC C14, 10A 250VAC, blade terminal, snap mount (replaces the 24V build's DC power jack — see the power-architecture note above) | [Electrokit #41035557](https://www.electrokit.com/en/mains-inlet-iec-c14-10a-250v-blade-terminal-snap) · [Amazon.se](https://www.amazon.se/dp/B0BYBS9KVZ?tag=kldesign-21) / [Amazon.com](https://www.amazon.com/dp/B078RHDTFZ?tag=kldesign00-20) (unverified ³) |
| 10 | Emergency Stop | Button | [Amazon.se](https://www.amazon.se/dp/B0FFMTCFLK?tag=kldesign-21) · [Amazon.com](https://www.amazon.com/dp/B0FFMTCFLK?tag=kldesign00-20) |
| 11 | XT60 Connector | 2-pin 30(60)A, male, chassis mount — internal 36V power connector (PSU → driver compartment) | [Electrokit #41023549](https://www.electrokit.com/en/stromkontakt-2-pol-xt60-30a-hane-chassi) · [Amazon.com](https://www.amazon.com/dp/B0FS7B12SS?tag=kldesign00-20) (unverified ³, no Amazon.se match found) |
| 12 | XT60 Connector | 2-pin 30(60)A, female — mates with #11; mounted with M2.5×10 screws (see Bolts/Screws below) | [Electrokit #41023546](https://www.electrokit.com/en/stromkontakt-2-pol-xt60-30a-hona) · [Amazon.se](https://www.amazon.se/dp/B09SL2MNN1?tag=kldesign-21) / [Amazon.com](https://www.amazon.com/dp/B09SL2MNN1?tag=kldesign00-20) (same Amass brand as the Electrokit part — the best-matched alt. here) |
| 18 | Step-down 36 V → 24 V | Adjustable buck converter (3.2–46 V in, 3 A). **Set it to 24.0 V with no load connected before wiring anything to it** — see [Wiring](../wiring.md#36v-power-rails) | [Amazon.se](https://www.amazon.se/dp/B0DK6M63YL?tag=kldesign-21) · [Amazon.com](https://www.amazon.com/dp/B0D7ZWVSFW?tag=kldesign00-20) |
| 19 | Mains Power Cable | 3-core earthed, IEC C13 plug, wall plug for your country | [Amazon.se](https://www.amazon.se/dp/B06WWBPCN8?tag=kldesign-21) · [Amazon.com](https://www.amazon.com/dp/B072LPBVP7?tag=kldesign00-20) |
| 20 | Mains-rated wire | C14 inlet → PSU L/N/⏚ inside the casing. Flexible single-core, 0.75–1.0 mm² (18 AWG), rated ≥300 V (EU: H05V-K / H07V-K; US: UL1015 600 V). Brown (L), blue (N), green/yellow (⏚) | — |

> **C13 vs C14:** the panel-mount socket on the PSU casing is an **IEC C14 inlet** (#9); the
> cable that plugs into it has a **C13** plug — a standard computer/kettle cable (#19).
>
> **XT60 orientation:** the **female** XT60 goes on the side that is live (the PSU), the
> **male** on the side that is not, so no live pins are ever exposed.

### Cooling

| # | Component | Specs | Link |
|---|-----------|-------|------|
| 13 | Fan | 24 V, 40×40×20 mm — the same fan as the 24V build, fed from the 24 V rail (#18), not from 36 V | [Amazon.se](https://www.amazon.se/dp/B00MNJD8BE?tag=kldesign-21) · [Amazon.com](https://www.amazon.com/dp/B07B66DJYX?tag=kldesign00-20) |

### Wiring Supplies

| # | Component | Specs | Link |
|---|-----------|-------|------|
| 14 | Silicone Wire | 18 AWG, 36 V power wiring (PSU → driver) — the PSU (#7) is rated 10 A; 18 AWG silicone-insulated wire is commonly rated in that range, but this is not a substitute for checking the actual continuous current your wire run carries (the stepper itself draws up to 4 A, well under the PSU's ceiling) against a proper ampacity chart before building | [Amazon.com](https://www.amazon.com/Silicone-Electrical-Conductor-Parallel-Flexible/dp/B07FMRDP87?tag=kldesign00-20) |
| 15 | Silicone Wire | 24 AWG, flexible stranded, signal wiring | [Amazon.com](https://www.amazon.com/TUOFENG-Wire-Stranded-Flexible-Silicone-Different/dp/B07G2BWBX8?tag=kldesign00-20) |
| 16 | Dupont Connector Kit + Crimping Tool | 2.54 mm connectors, housings, and ratcheting crimper | [Amazon.com](https://www.amazon.com/Crimping-Connector-Assortment-Ratcheting-0-25-1-5mm%C2%B2/dp/B0FJ8LCZ9W?tag=kldesign00-20) |
| 17 | Ferrule Connector Kit + Crimping Tool | For power and motor wires to TMC5160 terminal block, and mains wires on clamp terminals | [Amazon.com](https://www.amazon.com/Preciva-Hexagonal-Self-adjustable-Terminals-Connectors/dp/B0D3D65VZT?tag=kldesign00-20) |
| 21 | Insulated Fork Terminals + Female Spade Connectors | Forks for the PSU screw terminals (sized to the terminal screw; flanged/locking type recommended). Fully insulated female spades for the C14 inlet tabs (sized to the tabs, typically 4.8 or 6.3 mm) | [Amazon.se](https://www.amazon.se/dp/B0H7WBF4PW?tag=kldesign-21) · [Amazon.com](https://www.amazon.com/dp/B0DMRWTW7J?tag=kldesign00-20) |

### Hardware (Fasteners & Inserts)

#### Bolts / Screws

| Qty | Size | Used For |
|-----|------|----------|
| 15 pcs | M4 x 16mm | Motor, Motor mount, Backplane upper, Backplane lower |
| 11 pcs | M5 x 40mm | Ballscrew mounts, Sled clamp |
| 4 pcs | M5 x 25mm | Sled mount |
| 1 pcs | M4 x 20mm | Display mount |
| 4 pcs | M3 x 30mm | 24V Fan |
| 2 pcs | M3 x 8mm | 36 V → 24 V step-down (#18) |
| 4 pcs | M3 x 5mm | TMC5160T |
| 1 pcs | M3 x 10mm | Driverhousing mounting to backplane|
| 2 pcs | M3 x 10mm | Driverhousinglid|
| 4 pcs | M2 x 5mm | Display |
| ? pcs | M2.5 x 10mm | Female XT60 mount (#12) — count TODO, confirm against the connector's actual mounting holes |

#### Lock Nuts

| Qty | Size | Used For |
|-----|------|----------|
| 3 pcs | M5 Lock nut | Sled clamp |
| 4 pcs | M3 Lock nut | 24V Fan |

#### Heat Inserts

| Qty | Size | Used For | Link |
|-----|------|----------|------|
| 16 pcs | M4 Heat insert | Motor, Motor mount, Backplane upper, Backplane lower, Display mount | [Amazon.se](https://www.amazon.se/dp/B09MTTC7S9?tag=kldesign-21) · [Amazon.com](https://www.amazon.com/dp/B0FCXXW62N?tag=kldesign00-20) ¹ |
| 8 pcs | M5 Heat insert | Ballscrew mount | [Amazon.se](https://www.amazon.se/dp/B07YSVXWS8?tag=kldesign-21) · [Amazon.com](https://www.amazon.com/dp/B0FCXXW62N?tag=kldesign00-20) ¹ |
| 7 pcs | M3 Heat insert | TMC5160T mount, Driverhousing to backplane, 36 V → 24 V step-down | [Amazon.se](https://www.amazon.se/dp/B08BCRZZS3?tag=kldesign-21) · [Amazon.com](https://www.amazon.com/dp/B0FCXXW62N?tag=kldesign00-20) ¹ |

> ¹ The US link is a bundle kit that includes M3, M4, and M5 inserts.
> ³ **Unverified**: found by search, not confirmed against the actual listing (photos/specs)
> to be an exact match for the Electrokit part in that row — treat as a candidate to check
> before buying, not a confirmed equivalent. The Electrokit link is the one actually sourced
> from a real order and should be treated as primary for now.

---
