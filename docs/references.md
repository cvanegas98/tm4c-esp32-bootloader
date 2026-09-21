# Vendor References

The PDFs are gitignored (9 MB). Keep local copies in the repo root; this file records
what they are and where the relevant material lives.

## Documents

| Document | Literature no. | Revision |
|---|---|---|
| TM4C123GH6PM Datasheet | **SPMS376E** | June 12, 2014 |
| Tiva C Series TM4C123x Silicon Errata | **SPMZ849F** | April 2016, covers silicon revs 6 and 7 |

In the datasheet, **printed page numbers match PDF page numbers exactly** — you can
jump straight to a page number in any reader.

## Datasheet page index

| Topic | Section | Page |
|---|---|---|
| `DID0` — read silicon revision (`MAJOR`/`MINOR`) | Reg 1 | 238 |
| `DID1` — part no., temperature range | Reg 2 | 240 |
| Flash overview: 1 KB erase, 2 KB protect pairs | 8.2.3 | 528 |
| Flash protection policy (`FMPPEn`/`FMPREn`) | 8.2.3.2–8.2.3.5 | 528–530 |
| **Flash programming rules + SRAM note** | 8.2.3.7 | **531** |
| Word program and page erase sequences | 8.2.3.8 | 531 |
| 32-word write buffer (`FWBn`, `FMC2`) | 8.2.3.9 | 532 |
| Non-volatile register commit (never do this) | 8.2.3.10 | 532–534 |
| EEPROM: error during programming | 8.2.4.1 | 537–538 |
| EEPROM: soft reset handling | 8.2.4.1 | 538 |
| EEPROM: mandatory init sequence | 8.2.4.2 | 539 |
| `RMCTL` — reset boot sequence, SP from 0x0, PC from 0x4 | Reg 29 | 577 |
| `GPIOCR` — commit register (PF0/PD7 are locked) | Reg 20 | 685 |
| CAN0 signal tables | 23.3 / 23.5 | 1344, 1353 |
| Reset characteristics (EEPROM repair = 6400 ms) | Table 24-11 | 1370 |
| **Flash + EEPROM characteristics** | 24.12 | **1384** |

### Key numbers (Table 24-27, p. 1384)

| Parameter | Value |
|---|---|
| Endurance | 100,000 program/erase cycles |
| Program, 64-bit aligned (`TPROG64`) | 30 µs min / 50 nom / 300 µs max |
| Page erase, <1k cycles | 8–15 ms |
| Page erase, 10k cycles | 15–40 ms |
| Page erase, 100k cycles | **75–500 ms** |
| Retention | 20 years ≤85 °C, 11 years at 105 °C |

Programming fewer than 64 bits takes the same time as 64 (footnote b), so the `FWBn`
buffered path is worth it for bulk image writes.

## Errata advisories that apply to silicon revision 7

| Advisory | Page | Effect |
|---|---|---|
| **MEM#14** | 52 | Flash program/erase while executing from flash >40 MHz can mis-fetch. Run from SRAM or drop to 40 MHz; interrupts disabled either way. |
| **MEM#05** | 47 | Power loss during a non-volatile register commit can brick the device. **No workaround.** |
| **MEM#07** | 48 | Watchdog / software / MOSC-failure reset during an EEPROM op corrupts data. |
| **MEM#10** | 50 | `EESUPP` `START` can corrupt EEPROM for the device lifetime. |
| **MEM#02** | 44 | `EESUPP` `START` does not function. |
| **MEM#11** | 51 | `ROM_EEPROMInit()` does not initialize correctly; use the flash version, TivaWare 2.1+. |
| **MEM#19** | 53 | PC0–3, PD7, PF0 cannot be ROM boot pins. |
| **SYSCTL#21** | 70 | `RESC` may not log the reset cause. **No workaround.** |
| **SYSCTL#16** | — | Non-monotonic VDDA rise 2.0–2.6 V can leave the LDO unstarted; only a power cycle recovers. Matters for Phase 5 fault injection. |
| **SYSCTL#03** | 61 | MOSC loss undetected after successful start. TI's workaround is WDT1 — which has three advisories of its own. |
| **WDT#01/02/03** | 76–78 | All affect Watchdog Timer 1. **Use WDT0.** |

Revision-6-only advisories **MEM#03** and **MEM#04** (silent EEPROM corruption, and a
device that a reset will not recover) do **not** apply to this board.

Note: MEM#03's workaround text references a `RESBEHAVCTL` register. **That register does
not exist on the TM4C123** — it is a TM4C129 register in a shared errata document.

## Gotcha

`USECRL` does **not** exist on the TM4C123. It is an LM3S/Stellaris-era register; flash
timing is handled internally on this part.
