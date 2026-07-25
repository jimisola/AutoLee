# Bill of Materials — 36V

Everything needed to build the **36V** variant of AutoLee. See [Wiring](../wiring.md) for
how it all connects, and the [README](../../README.md) for the safety warning **before**
you build or operate it. Building the 24V variant instead? See
[../24V/bill-of-materials.md](../24V/bill-of-materials.md).

> ⚠️ **TODO:** this file is scaffolded from the 24V BOM. Rows marked `TODO` below carry
> voltage-dependent specs (PSU, buck converter, fan, and possibly wiring gauge) that need
> the actual 36V part numbers/links filled in — do not build from this file until they are.
> Everything else (mechanical, connectors, fasteners) is assumed identical to the 24V build;
> double-check that assumption too before relying on it.

> **Support this project:** The product links below are affiliate links. If you purchase through them, I earn a small commission at no extra cost to you — it's a simple way to help fund continued development of AutoLee. Thank you!

### Electronics

| # | Component | Specs | Link |
|---|-----------|-------|------|
| 1 | WaveShare 1.47" ESP32-C6 | Touchscreen controller & UI | [Amazon.se](https://www.amazon.se/dp/B0F8B845Y6?tag=kldesign-21) · [Amazon.com](https://www.amazon.com/dp/B0FC5SNKH4?tag=kldesign00-20) |
| 2 | TMC5160T Plus | Silent stepper driver with StallGuard2 | [Amazon.se](https://www.amazon.se/dp/B0D5HQWW1C?tag=kldesign-21) · [Amazon.com](https://www.amazon.com/dp/B0CHFK7VBL?tag=kldesign00-20) |
| 3 | Buck Converter | **TODO:** 36 V → 5 V (24V variant's converter is input-rated for 24 V only — confirm/replace) | **TODO** |

### Mechanical

| # | Component | Specs | Link |
|---|-----------|-------|------|
| 4 | NEMA 23 Stepper Motor | 2.4 Nm, 4.0 A, 57×57×82 mm, 8 mm shaft | [Amazon.se](https://www.amazon.se/dp/B091C37FJ2?tag=kldesign-21) · [Amazon.com](https://www.amazon.com/dp/B091C37FJ2?tag=kldesign00-20) |
| 5 | Shaft Coupling | Motor-to-leadscrew (8 mm to 10 mm) | [Amazon.se](https://www.amazon.se/dp/B07CLLW7Z3?tag=kldesign-21) · [Amazon.com](https://www.amazon.com/dp/B08QV1QN81?tag=kldesign00-20) |
| 6 | Ball Screw Kit SFU1605 250 mm | 250mm SFU1605 BK12/BF12 10 mm Shaft | [Amazon.de](https://www.amazon.de/dp/B08WRJRM22?tag=kldesign-21) · [Amazon.com](https://www.amazon.com/dp/B09BQSWPM4?tag=kldesign00-20) |

### Power

| # | Component | Specs | Link |
|---|-----------|-------|------|
| 7 | Power Supply | **TODO:** 36 V, ? A DC | **TODO** |
| 8 | On/Off Switch | Panel mount | [Amazon.se](https://www.amazon.se/dp/B07GDCNXKP?tag=kldesign-21) · [Amazon.com](https://www.amazon.com/dp/B078KBC5VH?tag=kldesign00-20) |
| 9 | DC Power Jack | 2.5 mm socket | [Amazon.se](https://www.amazon.se/dp/B081CM1G4M?tag=kldesign-21) · [Amazon.com](https://www.amazon.com/dp/B09W9SJ1B6?tag=kldesign00-20) |
| 10 | Emergency Stop | Button | [Amazon.se](https://www.amazon.se/dp/B0FFMTCFLK?tag=kldesign-21) · [Amazon.com](https://www.amazon.com/dp/B0FFMTCFLK?tag=kldesign00-20) |

### Cooling

| # | Component | Specs | Link |
|---|-----------|-------|------|
| 11 | Fan | **TODO:** 36 V, 40×40×20 mm (confirm the 24V fan isn't dual-rated before reusing) | **TODO** |

### Wiring Supplies

| # | Component | Specs | Link |
|---|-----------|-------|------|
| 12 | Silicone Wire | **TODO:** 18 AWG assumed OK at 36 V for the same current, but re-check against the actual 36V PSU's rated current once chosen | [Amazon.com](https://www.amazon.com/Silicone-Electrical-Conductor-Parallel-Flexible/dp/B07FMRDP87?tag=kldesign00-20) |
| 13 | Silicone Wire | 24 AWG, flexible stranded, signal wiring | [Amazon.com](https://www.amazon.com/TUOFENG-Wire-Stranded-Flexible-Silicone-Different/dp/B07G2BWBX8?tag=kldesign00-20) |
| 14 | Dupont Connector Kit + Crimping Tool | 2.54 mm connectors, housings, and ratcheting crimper | [Amazon.com](https://www.amazon.com/Crimping-Connector-Assortment-Ratcheting-0-25-1-5mm%C2%B2/dp/B0FJ8LCZ9W?tag=kldesign00-20) |
| 15 | Ferrule Connector Kit + Crimping Tool | For power and motor wires to TMC5160 terminal block | [Amazon.com](https://www.amazon.com/Preciva-Hexagonal-Self-adjustable-Terminals-Connectors/dp/B0D3D65VZT?tag=kldesign00-20) |

### Hardware (Fasteners & Inserts)

#### Bolts / Screws

| Qty | Size | Used For |
|-----|------|----------|
| 15 pcs | M4 x 16mm | Motor, Motor mount, Backplane upper, Backplane lower |
| 11 pcs | M5 x 40mm | Ballscrew mounts, Sled clamp |
| 4 pcs | M5 x 25mm | Sled mount |
| 1 pcs | M4 x 20mm | Display mount |
| 4 pcs | M3 x 30mm | 36V Fan |
| 4 pcs | M3 x 5mm | TMC5160T |
| 1 pcs | M3 x 10mm | Driverhousing mounting to backplane|
| 2 pcs | M3 x 10mm | Driverhousinglid|
| 4 pcs | M2 x 5mm | Display |

#### Lock Nuts

| Qty | Size | Used For |
|-----|------|----------|
| 3 pcs | M5 Lock nut | Sled clamp |
| 4 pcs | M3 Lock nut | 36V Fan |

#### Heat Inserts

| Qty | Size | Used For | Link |
|-----|------|----------|------|
| 16 pcs | M4 Heat insert | Motor, Motor mount, Backplane upper, Backplane lower, Display mount | [Amazon.se](https://www.amazon.se/dp/B09MTTC7S9?tag=kldesign-21) · [Amazon.com](https://www.amazon.com/dp/B0FCXXW62N?tag=kldesign00-20) ¹ |
| 8 pcs | M5 Heat insert | Ballscrew mount | [Amazon.se](https://www.amazon.se/dp/B07YSVXWS8?tag=kldesign-21) · [Amazon.com](https://www.amazon.com/dp/B0FCXXW62N?tag=kldesign00-20) ¹ |
| 5 pcs | M3 Heat insert | TMC5160T mount, Driverhousing to backplane | [Amazon.se](https://www.amazon.se/dp/B08BCRZZS3?tag=kldesign-21) · [Amazon.com](https://www.amazon.com/dp/B0FCXXW62N?tag=kldesign00-20) ¹ |

> ¹ The US link is a bundle kit that includes M3, M4, and M5 inserts.

---
