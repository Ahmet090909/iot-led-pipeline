# IoT LED Pipeline – Verkeerslichtkruispunt (Raspberry Pi)

## Projectbeschrijving
Dit project is ontwikkeld binnen het vak **IoT Technologie** en focust op het opzetten van een **betrouwbare ontwikkel- en build-pipeline** voor embedded software op de Raspberry Pi.

Als demonstratieproject werd een **verkeerslichtkruispunt** gerealiseerd met GPIO-aangestuurde LEDs, inclusief een **night mode** die via een drukknop geactiveerd kan worden.  
De nadruk ligt niet alleen op de functionaliteit, maar vooral op **structuur, pipeline, versiebeheer en uitbreidbaarheid**.

---

## Functionaliteit
- Kruispunt met **twee richtingen (Noord–Zuid & Oost–West)**
- Elke richting:
  - Rood, geel en groen LED
- **State machine** met veilige overgangen:
  - Groen → Geel → All-Red → andere richting
- **Night mode**:
  - Beide richtingen knipperend geel
  - Toggle via drukknop
- Veilige opstart: all-red toestand

---

## Hardware
- Raspberry Pi 4
- 6 × LED (rood, geel, groen × 2 richtingen)
- 6 × weerstand (220–330 Ω)
- 1 × drukknop (night mode)
- Breadboard & jumper wires

### GPIO Mapping
| Functie | GPIO |
|------|------|
| NS Rood | GPIO 26 |
| NS Geel | GPIO 19 |
| NS Groen | GPIO 13 |
| EW Rood | GPIO 6 |
| EW Geel | GPIO 5 |
| EW Groen | GPIO 22 |
| Night mode knop | GPIO 23 |

> De drukknop is aangesloten naar GND en gebruikt een interne pull-up.

---

## Software & Libraries
- Programmeertaal: **C**
- GPIO-library: **pigpio**
- Besturingssysteem: Raspberry Pi OS (Linux)

> pigpio vereist directe toegang tot GPIO-hardware en draait daarom **op de Raspberry Pi zelf**.

---

## Build & Run
### Compileren
De code wordt gecompileerd met GCC.  
pigpio is lokaal beschikbaar als statische library.

```bash
gcc src/intersection_night.c \
  -I. -L. -lpigpio -lrt -lpthread \
  -o intersection