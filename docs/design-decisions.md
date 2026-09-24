# Design Decisions

Recorded as they are made, with the reasoning and the rejected alternatives.
Hardware: TM4C123GH6PM, **silicon revision 7** (die B2), EK-TM4C123GXL LaunchPad.

---

## D1 — One image, two slots: two link-time builds

**Decision.** Every release is built twice, linked for slot A (`0x0A400`) and slot B
(`0x13400`). The image header records its target load address; the bootloader
refuses an image whose header does not match the slot it is in. The host asks which
slot is inactive and sends the matching variant.

**Why.** Nothing is ever copied, so the inactive slot stays byte-intact for rollback
and there is no multi-second window where a slot is half-written by the bootloader
itself. Power loss during an update can only corrupt the slot being written, which
was already invalid.

**Rejected — staging slot + fixed execution slot (MCUboot swap).** One link address
and one binary, but the bootloader must copy staging into the execution slot. That
copy is a long window in which the execution slot is partially erased, requiring a
resumable copy state machine in metadata and a third image copy for rollback. It is
the hardest variant to make atomic, which is the opposite of this project's goal.

**Rejected — position-independent code.** One binary for any slot, but Keil's
ROPI/RWPI restrictions are severe and the failure mode is a subtle runtime crash in
a function that took an absolute address, not a link error.

**Verified.** MDK-Lite does link to a non-zero base address: a stub built at
`IROM1 = 0x8000` produced `LR_IROM1`/`ER_IROM1` base `0x00008000`, `ABSOLUTE`, with
the vector table at the region base.

---

## D2 — The bootloader owns the CAN receive path

**Decision.** The bootloader contains the flash driver, CRC-32, image validation,
`VTOR` relocation and jump, A/B rollback, the boot counter, the watchdog, **and** the
CAN driver, framing protocol and chunked receive. Safe mode can accept a new image
when both slots are bad.

**Why.** A device that recovers itself in the field without a debugger is the point
of the project. The alternative leaves a board with two dead applications requiring
SWD access to revive.

**Cost.** Roughly 8–16 KB of bare-register code on top of the rest, which is why the
bootloader region is 32 KB.

**Rejected — app-only updates.** Bootloader shrinks to ~4–6 KB and the trusted code
base is much easier to verify, but two bad slots means a dead board.

---

## D3 — Metadata lives in journaled flash pages

**Decision.** Two 1 KB pages at `0x08000` and `0x08400`. Records are appended within a
page, each carrying a sequence number and a CRC. The reader takes the highest-sequence
valid record. When a page fills, the next record is written to the other page, and only
after it verifies is the old page erased. The boot counter is a word of ones with one
bit cleared per boot attempt — no erase per boot, counts to 32.

**Why.** A torn record is caught by the CRC and the reader falls back to the previous
sequence number. Nothing is ever in a state where no valid record exists.

**Rejected — on-chip EEPROM.** Ruled out by silicon errata on revision 7:
- **MEM#07** — a watchdog, software, or MOSC-failure reset during an EEPROM operation
  corrupts data. This design has a watchdog running by construction, so the corrupting
  event is one this project deliberately causes.
- **MEM#10** — using the `EESUPP` `START` bit for recovery can corrupt EEPROM for the
  lifetime of the device.
- **MEM#02** — the `START` bit does not function at all.
- **MEM#11** — `ROM_EEPROMInit()` does not initialize the EEPROM correctly.

TI's own workaround text for the related revision-6 advisories reads *"Use the Flash
memory with application software to store data instead of the EEPROM controller."*

**Rejected — in-slot trailer only.** State travels with the image and erasing a slot
clears its state in the same operation, but the boot counter still needs a home and
"which slot is active" becomes a comparison of two trailers rather than one record.

---

## D4 — Region sizes and header placement

**Decision.** Bootloader region 32 KB; image header in its own 1 KB page at the slot
base, with the image (and therefore the vector table) at slot base + `0x400`.

**Why 32 KB.** MDK-Lite caps a project at 32 KB, so the toolchain binds before the map
does and the map can never force a re-layout. 16 KB of the 160 KB free flash is a cheap
price; unused bootloader pages stay erased and write-protected.

**Why a separate header page.** The header is its own erase unit, so the full image can
be written and verified before the validating word is written into a page that was not
touched during the transfer. It also puts the vector table on a naturally 1 KB-aligned
address for `VTOR`.

---

## Constraints that shaped all of the above

- **MEM#14** (revs 6 and 7) — flash program/erase while executing from flash above
  40 MHz can mis-fetch. The program/erase routine runs from SRAM with interrupts
  disabled, or the clock drops to 40 MHz for the duration.
- **SYSCTL#21** (revs 6 and 7, no workaround) — `RESC` may not log the reset cause.
  Boot logic uses a software boot-progress marker, not `RESC`.
- **MEM#05** (revs 6 and 7, no workaround) — power loss during a non-volatile register
  commit can brick the device. `FMPPE`/`BOOTCFG` are never committed; runtime
  (volatile) protection writes only.
- **Erase time scales with wear** — 8–15 ms fresh, up to 500 ms at 100k cycles. The
  watchdog is serviced inline between pages, not from an ISR.
- **WDT0 only** — WDT#01/#02/#03 all affect Watchdog Timer 1.

---

## D5 — CRC-32 variant, implementation, and scope

**Variant.** CRC-32/ISO-HDLC — the zlib / PNG / Ethernet one.

| Parameter | Value |
|---|---|
| Polynomial | `0x04C11DB7` (`0xEDB88320` in reflected form) |
| Init | `0xFFFFFFFF` |
| RefIn / RefOut | true / true |
| XorOut | `0xFFFFFFFF` |

**Check value:** CRC-32 of the ASCII bytes `123456789` (9 bytes, no terminator) is
**`0xCBF43926`**. Both the host packager and the target implementation must produce
this before either is trusted.

**Why this variant.** Python's `zlib.crc32()` implements it exactly, so the host side is
free and only the target implementation has to be written and verified.

**Implementation: nibble table.** 16 entries, 64 bytes of RO data, two lookups per byte.
Roughly an order of magnitude faster than bitwise for 64 bytes of flash — the 256-entry
byte table's extra speed is not worth 1 KB out of a 32 KB bootloader for an operation
that runs once or twice per reset.

**Rejected — ROM CRC.** The TM4C123 ROM contains a CRC implementation and the datasheet
names flash validation as an intended use (§8.2.2.4, p. 528). It costs zero flash, but
the variant and API are documented only in the ROM User's Guide (SPMU367), which we have
not read, and MEM#11 shows rev-7 ROM APIs are not automatically trustworthy.

**Scope: the header CRC does not cover the magic field.**

The image commit sequence is: write image -> write header (all fields except magic) ->
verify -> write magic as a **single 32-bit word**, which is the act that makes the image
valid. If the header CRC covered the magic, the magic could not be written last without
invalidating the header. Therefore:

- `magic` is the standalone validity flag, at header offset 0.
- The header CRC covers the header from the field **after** `magic` through the end of
  the header.
- The image CRC covers the image payload only, not the header.

**API: one-shot `crc32_compute(data, length)`, no streaming interface.** Every CRC the
bootloader computes is over contiguous memory: the image and header CRCs are computed
over flash, and any per-chunk check in the CAN framing is over a single RX buffer. The
post-update image CRC is deliberately computed by reading the slot back from flash, not
accumulated over the incoming CAN stream — a stream CRC would still match after a failed
word program or a torn erase, while a read-back CRC verifies what was actually programmed.

**Constraint on the header layout.** The header CRC must also exclude its own field, so
`header_crc` sits either immediately after `magic` or as the last header field. Either
way the covered range stays contiguous and one call covers it.

**Revisit if:**
- CRC over a full 36 KB slot takes longer than the watchdog budget allows (estimate
  ~11 ms at 50 MHz, ~35 ms at 16 MHz; to be measured on target), forcing a feed mid-CRC; or
- Phase 3 needs a CRC accumulated across chunks before they reach flash.

Migration path: split into `crc32_init` / `crc32_update` / `crc32_final` (the loop already
carries `crc` as its only state) and keep `crc32_compute` as a wrapper, so existing
callers and the host test are unchanged.

---

## D6 — Flash driver, bootloader clock, and watchdog timing

**Clock.** The bootloader runs from the 16 MHz crystal (MOSC) with the PLL off.

- At or below 40 MHz, so MEM#14 does not apply and the program/erase routine can execute
  from flash — no SRAM copy. Fetches simply stall until the operation completes (p. 531).
- **Rejected — PIOSC.** Also 16 MHz with no crystal start-up, but only ±3% across
  voltage and temperature (Table 24-15). CAN bit timing needs the two nodes within roughly
  0.5–1.5% combined, and the bootloader must speak CAN in safe mode.

**Writes: single 32-bit words via `FMD`/`FMA`/`FMC` only.** The two writes that matter
for atomicity — committing `magic` and clearing a boot-counter bit — are single words
anyway. A full 36 KB slot is ~9,216 words, about 0.5 s worst case at 50 µs per 64 bits
(Table 24-27), so the 32-word write buffer (`FWBn`/`FMC2`) is not worth its 128-byte
alignment rule and extra code.

Write rules the driver and its callers must respect:

- A write can only clear bits. Any 1-bit in the request over a 0 in flash fails the
  **whole** write, changes nothing, and sets `INVDRIS` (p. 531). Clearing a boot-counter
  bit is written as `old & ~bit`.
- `WRKEY` is `0xA442` when `BOOTCFG.KEY` = 1 (factory default) and `0x71D5` otherwise
  (p. 583). A wrong key is ignored silently — no operation, no error flag.

**Error return: the masked `FCRIS` error bits; 0 means success.** Stale flags are cleared
through `FCMISC` before each operation and checked after it. Callers that only need
pass/fail test for non-zero; the fault log records the cause.

| Bit | Meaning | Response |
|---|---|---|
| `VOLTRIS` | Pump voltage out of spec, operation aborted (brownout) | Retry, log |
| `INVDRIS` | Tried to program a 0 back to 1 — software bug | No retry |
| `ARIS` | Program/erase on a protected block — software bug | No retry |
| `ERRIS` | Program/erase verify failed — possible wear | Treat target as bad |

**Self-protection: uncommitted `FMPPE0` bits 0–15, set at every boot.** Each bit covers a
2 KB block, so bits 0–15 are exactly the 32 KB bootloader region; bit 16 (`0x08000`, the
metadata journal) stays writable.

- `FMPPEn` is RW0 — bits only go 1 -> 0 — and only a power-on reset restores them; watchdog,
  software and pin resets do not (p. 579). That is what makes it stick for the app's
  whole run, and also why **only the bootloader region** is ever protected this way:
  a protected slot or journal would stay unwritable after a watchdog reset.
- Never committed (MEM#05, see constraints above).
- Consequence: **the bootloader cannot update itself in the field.** Not a goal.
- On the bench, if LM Flash cannot reprogram the bootloader region, power-cycle the board.

**Watchdog: WDT0, 1 s from last feed to reset.**

- WDT0 resets on its **second** time-out (p. 774), so `WDTLOAD` holds 0.5 s of ticks —
  8,000,000 at 16 MHz. First time-out raises the interrupt at 0.5 s; reset at 1.0 s.
- The WDT interrupt handler must **not** clear the interrupt — that counts as a feed and
  would keep a hung system alive. It may write a fault-log entry, then let the second
  time-out reset.
- Margin: a worst-case page erase stalls the CPU for up to 500 ms (Table 24-27), which is
  2× inside the 1 s budget. Multi-page erases feed between pages (~18 s for a full slot).
  Safe-mode CAN reception feeds inside its RX loop.

**Open items.**

1. **App-side flash writes vs. MEM#14.** The app marks boot-OK in the metadata journal,
   and it will likely run from the PLL above 40 MHz. Options: drop the clock to ≤40 MHz
   around each write, or run the app's program/erase routine from SRAM with interrupts
   disabled. *Undecided.*
2. **Watchdog timeout scales with the app's clock.** WDT0 counts system-clock ticks, so
   a load set at 16 MHz gives 0.2 s to reset at 80 MHz. Options: the app reloads
   `WDTLOAD` after its clock switch (bootloader leaves the WDT registers unlocked), or
   the bootloader locks the WDT and the app must stay at 16 MHz. *Undecided.*
