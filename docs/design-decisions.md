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
- The flash driver assumes this clock limit. Code running above 40 MHz must lower the
  clock before calling it or use a separate routine that executes from SRAM.
- **Rejected — PIOSC.** Also 16 MHz with no crystal start-up, but only ±3% across
  voltage and temperature (Table 24-15). CAN bit timing needs the two nodes within roughly
  0.5–1.5% combined, and the bootloader must speak CAN in safe mode.

**Writes: single 32-bit words via `FMD`/`FMA`/`FMC` only.** The two writes that matter
for atomicity — committing `magic` and clearing a boot-counter bit — are single words
anyway. The 32-word write buffer (`FWBn`/`FMC2`) is not worth its 128-byte alignment
rule and extra code.

Write time (Table 24-27, `TPROG64`): 30 µs min, 50 µs nominal, **300 µs max** per program
operation. Footnote b: programming fewer than 64 bits takes the same time, so each
single-word write costs a full cycle.

| | Per word | Full 36 KB slot (~9,216 words) |
|---|---|---|
| Nominal | 50 µs | ≈ 0.46 s |
| Worst case | 300 µs | **≈ 2.8 s** |

A single write stalls the CPU for at most 300 µs, but a full-slot write exceeds the 1 s
watchdog budget, so slot writes must feed the watchdog along the way (see Watchdog).
*(Corrected 2026-09-24: an earlier draft quoted the 50 µs nominal as the worst case.)*

Write rules the driver and its callers must respect:

- A write can only clear bits. Any 1-bit in the request over a 0 in flash fails the
  **whole** write, changes nothing, and sets `INVDRIS` (p. 531). Clearing a boot-counter
  bit is written as `old & ~bit`.
- `WRKEY` is `0xA442` when `BOOTCFG.KEY` = 1 (factory default) and `0x71D5` otherwise
  (p. 583). A wrong key is ignored silently — no operation, no error flag.
- `FMA` must be 4-byte aligned for a word write; otherwise "the results of the operation
  are unpredictable" (p. 542). The alignment check is required, not defensive.

**API: `flash_write_word(address, value)`, one word per call.** Callers that write many
words (image transfer) loop, so they already know which address failed, and they own
watchdog feeding — the driver knows nothing about the watchdog.

`flash_write_word` checks, in order:

1. Alignment and range (`0x08000`–`0x3FFFC`) → `FLASH_ERR_INVALID_ADDRESS`.
2. **Skip rule:** if the word already holds `value`, return 0 without programming. This
   covers writing `0xFFFFFFFF` to an erased word, and makes retries idempotent — a
   resumed transfer can rewrite words that already landed, and re-clearing an already
   cleared boot-counter bit costs nothing.
3. **0-to-1 check in software:** if `(current & value) != value`, the request has a 1
   over a 0 in flash → `FLASH_ERR_ZERO_TO_ONE`, nothing written. This enforces the
   `old & ~bit` rule instead of only documenting it. Writing `0xFFFFFFFF` over a
   programmed word fails here — the caller asked for a value that needs an erase.
4. Program, check the `FCRIS` error bits, then verify the word reads back exactly
   `value` → `FLASH_ERR_VERIFY_FAILED` on mismatch.

`INVDRIS` stays in the hardware error mask as a backstop behind check 3. Consequence:
the hardware 0-to-1 path can no longer be exercised through the driver on the bench;
it only matters if check 3 itself is wrong.

Phase 5 note: a word interrupted mid-program by power loss can read back correctly
while only weakly programmed, and the skip rule would leave it in place. This matters
most for the single-word `magic` commit; it belongs in the fault-injection campaign.

**Error return: 0 means success.** Hardware failures return the masked `FCRIS` error
bits. Bits 31–29 are software errors shared by erase and word write:
`FLASH_ERR_INVALID_ADDRESS` (bit 31), `FLASH_ERR_VERIFY_FAILED` (bit 30), and
`FLASH_ERR_ZERO_TO_ONE` (bit 29, write only).
Stale flags, including `PRIS`, are cleared through `FCMISC` before each operation and
the hardware error bits are checked afterward. Callers needing only pass/fail can
test for non-zero; the fault log records the cause.

**Flash-controller ownership.** No interrupt handler may start a flash operation or
change `FMA`, `FMD`, or `FMC`. Foreground flash operations therefore keep control of
the address and command registers from setup through completion. The watchdog ISR
must not write the flash fault log. No watchdog-interrupt diagnostic record is
specified yet; any future record that must survive reset belongs with the software
boot-progress marker design.

| Bit | Meaning | Response |
|---|---|---|
| `VOLTRIS` | Pump voltage out of spec, operation aborted (brownout) | Retry, log |
| `INVDRIS` | Tried to program a 0 back to 1 — bug in the software 0-to-1 check | No retry |
| `ARIS` | Program/erase on a protected block — software bug | No retry |
| `ERRIS` (bit 11) | Erase verify failed — possible wear | Treat target as bad |
| `PROGRIS` (bit 13) | Program verify failed — possible wear | Treat target as bad |

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
  would keep a hung system alive. It must not write flash; the second time-out resets
  the device.
- Margin: a worst-case page erase stalls the CPU for up to 500 ms (Table 24-27), which is
  2× inside the 1 s budget. Multi-page erases feed between pages (~18 s for a full slot).
- Slot writes feed at least once per 1 KB page written (256 words, ≤ 77 ms at the
  300 µs worst case). A full slot takes up to ~2.8 s, so writing it without feeding
  would reset the device.
- Safe-mode CAN reception feeds inside its RX loop.

### D6 addendum — build configuration and bench procedure (2026-09-24)

Found while bench-testing `flash_erase_page`.

**Floating Point Hardware must be "Not Used" for the bootloader.**

- `vendor/startup.s` leaves the FPU disabled: the `CPACR` write in `Reset_Handler` is
  commented out. TI's original file enables it there.
- With *Floating Point Hardware: Single Precision*, armclang builds hard-float
  (`-mfloat-abi=hard -mfpu=fpv4-sp-d16`), and the C library's `__rt_lib_init` calls
  `_fp_init`, which executes `VMRS` **before `main`**. With CP10/CP11 disabled that is a
  no-coprocessor UsageFault, escalated to HardFault. Observed: fault LED on power-up,
  `main` never reached.
- With *Not Used* (`-mfloat-abi=soft -mfpu=none`, linker `--fpu=SoftVFP`), the image
  contains no floating-point instructions and boots normally.
- Handoff consequence: the bootloader never enables the FPU, so the app's own startup
  owns `CPACR`. A hard-float app must enable the FPU in its `Reset_Handler` before
  `__main`.
- The project setting once reverted to Single Precision without notice. Before flashing,
  confirm the build used `-mfloat-abi=soft` (in `Objects/*.dep`) or that the disassembly
  has no `V*` instructions.

**Faults must be visible.** The vendor `HardFault_Handler` is `B .`, which makes a fault
look like "nothing happened" — that is what hid the FPU fault for a day. Bench test
programs define their own `HardFault_Handler` that lights a distinct LED color. The
Phase 2 bootloader fault handler must be visible or recorded, then reset.

**Bench procedure: LM Flash can restart the CPU without a power-on reset.**

- During a program or upload the CPU is reset and held (LEDs go dark as GPIO returns to
  reset state). What happens when the command finishes differs by command:

  | After | Observed | Count |
  |---|---|---|
  | Upload (`-u`) | CPU released; image runs from reset | 3 of 3 |
  | Program (`-v file`) | CPU stays held; LEDs dark until the next command or power-cycle | 2 of 3 |
  | Program onto a previously blank device | Image ran and hit HardFault (below) | 1 of 3 |

- Do not rely on either behavior. A test image that modifies flash may run after any LM
  Flash command, and a before/after upload only describes what ran since the previous
  command. Record the LED state after each command.
- Test images should be self-contained: set up their own starting state, and detect a
  start that was not a power-on reset (the write test checks `FMPPE1` bit 24 and
  stops with a distinct color if it is already cleared).
- *(Corrected 2026-09-24: the first version of this addendum said the CPU is released
  after every command; the write-test run showed programs leave it held.)*
- This is not a power-on reset: uncommitted `FMPPEn` bits survive it. A run started
  this way after a run that cleared a protection bit will see `ARIS` on that block.
- Once, right after programming a device whose flash had been blank, the first run hit
  HardFault; the next power-on boot was normal. Only boots from a power-cycle are
  treated as representative.

**Not yet measured: page-erase time on this part.** No scope on hand, and Keil's logic
analyzer needs SWO trace, which the on-board ICDI does not provide. The watchdog margin
above uses the datasheet worst case (500 ms), so the measurement confirms rather than
decides anything.

**App-side flash writes: a RAM-resident write routine (2026-09-28).**

**Decision.** The app's boot-confirm write — and any future app-side flash write —
runs from a small routine copied into SRAM at app startup and executed with
interrupts disabled, not by dropping the app's clock to ≤40 MHz around each write.

**Why.** This is the errata's own workaround for MEM#14: fetching from SRAM instead
of the flash array being written removes the mis-fetch risk at its source, rather
than working around it by staying under a clock ceiling. It is also reusable — one
routine covers boot-confirm and any later app-side write (the Phase 5 fault log, at
minimum) instead of special-casing this one event.

**Rejected — drop the app's clock to ≤40 MHz around each write.** Would let the app
reuse a normal flash-write routine executing from flash, but the app's clock would
need to change and restore around every write, not just once. Once CAN is in the
picture (Phase 3/4), a clock change mid-runtime desyncs CAN bit timing (which derives
from system clock) right around the transition — a risk this project does not need to
accept for a routine that can just as well run from SRAM instead. It would also
compound with the watchdog open item below, which would then have to stay correct
around every write instead of only around the app's own clock switch at startup.

**Consequence: compaction stays bootloader-only.** The app's write never compacts —
if its append fails because the current page is full, it just retries on the next
boot. This is only safe because the bootloader guarantees room before every jump; see
D8's headroom check.

**Watchdog budget across the app's clock: calibrate for the chip's max clock
(2026-09-28).**

**Decision.** As one of the last steps before jumping (alongside the D9 marker
write), the bootloader reprograms `WDTLOAD` for the TM4C123's maximum system clock —
80 MHz — instead of the 16 MHz it used for its own execution, then locks the
watchdog via `WDTLOCK`. The app never touches WDT registers and may run at any clock
up to 80 MHz.

| | Bootloader (own execution) | App (post-jump) |
|---|---|---|
| Clock assumed | 16 MHz (MOSC, PLL off, above) | 80 MHz (chip maximum) |
| `WDTLOAD` | `0x007A1200` (8,000,000) | `0x02625A00` (40,000,000) |
| Interrupt / reset | 0.5 s / 1.0 s | 0.5 s / 1.0 s **at 80 MHz** |

**Why calibrating for the maximum clock is always safe, not just at 80 MHz.**
`WDTLOAD` ticks take longer in real time at a slower clock, never shorter. A value
sized for the fastest clock the app could ever run at is automatically safe — looser,
not tighter — at any slower clock the app actually chooses. At 16 MHz the same
`0x02625A00` gives roughly 2.5 s / 5.0 s instead of 0.5 s / 1.0 s: hangs are caught
more slowly, but the watchdog can never fire early. 80 MHz is this part's hard system
clock ceiling, so "the app must not exceed 80 MHz" is not a new restriction — it is
the silicon's own limit.

**Why lock the WDT instead of leaving it open for the app to manage.** A locked WDT
cannot be disabled or stretched by a bug anywhere in the app — the one property a
watchdog exists to guarantee. Locking is a one-word write (`WDTLOCK`) done once,
right before jump, alongside the `WDTLOAD` recalibration.

**Rejected — app reloads `WDTLOAD` itself after its own clock switch.** Gets an
exactly-tight budget matched to whatever clock the app actually picks, but costs two
things this project doesn't need to accept: a window between "clock changed" and
"`WDTLOAD` reloaded" where the timeout is silently wrong, and WDT registers that must
stay unlocked for the app's entire runtime — exactly the surface a stray pointer
write could use to disable the one thing meant to catch it.

**Rejected — lock the WDT and require the app to stay at 16 MHz.** Same
tamper-resistance as the decision above, but caps the app's clock forever for no
reason connected to the watchdog itself — MEM#14 and CAN bit-timing accuracy are the
only real clock constraints this project has, and both are already handled elsewhere
(the app-side RAM-resident write routine above; CAN clock accuracy is a Phase 3
decision).

**Note for implementation.** Writing `WDTLOAD` is expected to reload the running
down-counter immediately, which would make this recalibration double as the final
feed before jump with no separate feed call needed — confirm this against the
datasheet's WDT register description before relying on it.

---

## D7 — Image header format

**Decision.** A fixed 32-byte header at the start of each slot's header page (`0x0A000`
for slot A, `0x13000` for slot B). All fields are 32-bit words, little-endian. The rest
of the header page stays erased (`0xFF`).

| Offset | Field | Contents |
|---|---|---|
| 0 | `magic` | `0xB007C0DE`. Written **last, by the bootloader**, as the validity commit |
| 4 | `header_crc` | CRC-32 (D5) of header bytes 8 to `header_size` − 1 |
| 8 | `header_version` | `1` |
| 12 | `header_size` | `32` |
| 16 | `load_address` | `0x0A400` (slot A) or `0x13400` (slot B) — D1 |
| 20 | `image_size` | Bytes; a multiple of 4; 1 to `0x8C00` |
| 24 | `image_crc` | CRC-32 (D5) of `image_size` bytes from `load_address` |
| 28 | `fw_version` | Semantic version packed as `major << 16 \| minor << 8 \| patch` |

**Why `header_crc` at offset 4.** D5 requires the CRC to skip `magic` and its own field,
which leaves two contiguous choices: directly after `magic`, or last. At offset 4 its
position never moves when a later header version grows at the end, and the bootloader
can locate it before knowing which version it is reading.

**Why all 32-bit words.** Flash is programmed one word at a time (D6), fields never
straddle a write, and the host packs the header with a single `struct` format.

**Why semver packed into one word.** Human-readable in logs, and still compares
correctly as a plain unsigned number: major 0–65535, minor and patch 0–255.

**Padding.** The packager pads the image to a multiple of 4 bytes with `0xFF`. A padded
word of `0xFFFFFFFF` hits the skip rule in `flash_write_word` (D6), so padding costs no
program cycles. `image_size` and `image_crc` include the padding.

**Who writes `magic`.** The host packager fills in every field, including `magic`, so
the file is self-describing. On the target, the bootloader writes every header word
except `magic`, verifies both CRCs by reading them back from flash, and only then writes
`magic` itself. Whatever `magic` value the host sent is ignored.

**Entry point comes from the image's vector table, not the header.** Word 0 of the image
is the initial stack pointer, word 1 the reset handler. Storing them in the header too
would add a copy that can disagree. The bootloader sanity-checks them instead:

- Initial SP inside SRAM: `0x20000000` < SP ≤ `0x20008000`, 8-byte aligned.
- Reset handler has bit 0 set (Thumb), and `(reset & ~1)` lies inside
  `[load_address, load_address + image_size)`.

**Validation order at boot** (cheapest first; any failure means the slot is not
bootable):

1. `magic` == `0xB007C0DE`.
2. `header_size` from 32 to 1024 and a multiple of 4, then `header_crc` over bytes 8 to
   `header_size` − 1.
3. `header_version` ≥ 1; `load_address` equals this slot's image base; `image_size`
   non-zero, a multiple of 4, and ≤ `0x8C00`.
4. `image_crc` over the image.
5. Vector-table sanity checks above.

**Good/bad status lives only in the metadata journal (D3).** `magic` means exactly one
thing: the image and header were completely written and verified. Whether the image has
booted successfully, is pending its first boot, or has been marked bad is recorded only
in the journal. Rejected: clearing `magic` to `0x00000000` to mark an image bad — it
works without an erase, but gives two places that can disagree.

**Forward compatibility: later header versions may only append.** The bootloader can
never be updated (D6), so today's bootloader must be able to boot images packaged
with a future header version. It accepts any `header_size` from 32 to 1024 (one header
page) that is a multiple of 4, verifies `header_crc` over the whole header, and reads
only the version 1 fields. The rule this puts on every future version: **fields at
offsets 0–31 never move or change meaning**, and new fields go after offset 32.
Rejected — accept only version 1 with `header_size` 32: any format change would need a
new bootloader, which devices in the field can never receive.

---

## D8 — Metadata journal record format and compaction

**Decision.** One fixed 24-byte record layout is shared by three record kinds, all
appended into the D3 journal: `ACTIVE_SLOT` (which slot the bootloader should try)
and `STATUS_A` / `STATUS_B` (per-slot health). All fields are 32-bit words,
little-endian.

| Offset | Field | Contents |
|---|---|---|
| 0 | `seq` | Monotonic counter, shared across all three record kinds |
| 4 | `type` | `0 = ACTIVE_SLOT`, `1 = STATUS_A`, `2 = STATUS_B` |
| 8 | `value` | `ACTIVE_SLOT`: which slot (0=A, 1=B). `STATUS_*`: status enum |
| 12 | `image_crc` | `STATUS_*` only — the D7 `image_crc` of the image this record describes. Reserved for `ACTIVE_SLOT` |
| 16 | `record_crc` | CRC-32 (D5) of bytes 0–15 |
| 20 | `boot_counter` | **Outside `record_crc`.** `STATUS_*` only; unused for `ACTIVE_SLOT` |

24 bytes/record, ~42 records per 1 KB page (D3) before a compaction swap.

**Appending a record: `record_crc` last, always.** Offsets 0–12 (`seq`/`type`/
`value`/`image_crc`) can be written in any order — none of them alone commits the
record. `boot_counter` (offset 20) is also written before the commit when it needs a
non-default value (a compaction copy carries forward an already-partially-cleared
counter); a fresh record needs no explicit write there, since its correct starting
value, `0xFFFFFFFF`, is already what an erased page reads as, and the D6 skip rule
turns an explicit write of that value into a free no-op anyway. `record_crc` at
offset 16 is written **last** — the same single-word commit pattern as `magic` in D7.

**Why one shared layout instead of three record shapes.** Keeps the scanner and the
compaction routine a single code path instead of three parsers. Costs a few unused
bytes in the `ACTIVE_SLOT` record.

**Why active-slot is its own record, not derived from the two statuses.** An
explicit pointer means rollback is "append one small record" — it never touches
either slot's `STATUS` record. Deriving "active" from, say, "highest `fw_version`
among `GOOD` slots" would mean a rollback decision could be silently re-derived
differently after a future compaction, and switching away from a failed slot would
require rewriting (or preserving) its diagnostic state for no reason.

**Why `STATUS` binds to `image_crc`.** Without it, a `STATUS_A = GOOD` record does
not say which image it validated. Concrete failure: slot A is `GOOD`, someone
reflashes it with a new image, and the journal write that should reset status to
`PENDING` is interrupted by power loss between the image write and the journal
append. The bootloader would see `STATUS_A = GOOD` and boot an unconfirmed image
without ever running rollback logic. Binding the record to `image_crc` lets the
bootloader detect the mismatch against the D7 header and treat it as `PENDING`.

**Boot counter: 3 attempts, threshold fixed in bootloader code.** Not stored in the
journal — matches D6 (the bootloader can never be updated, so nothing is gained by
making the threshold field-tunable). `boot_counter` starts at `0xFFFFFFFF`. On every
boot where the active slot's status is `PENDING`, the bootloader clears one more bit
before jumping. Once 3 bits are clear with no `GOOD` confirmation, the bootloader
appends `STATUS = BAD` and flips `ACTIVE_SLOT`. Once status reaches `GOOD` the
counter is never touched again. `boot_counter` sits outside `record_crc` specifically
so clearing a bit never invalidates the record it belongs to.

This makes D6 open item 1 (the app's own boot-OK flash write under MEM#14) a hard
requirement: the app must be able to append its `STATUS = GOOD` record before a
third unconfirmed reboot happens, or a healthy image gets rolled back anyway.

**Page identification: a generation word.** Refines D3's "the reader takes the
highest-sequence valid record" for the two-page case. Each 1 KB page's first word is
`page_gen` (u32); erased (`0xFFFFFFFF`) means "not active." The current page is
whichever of the two has a valid `page_gen` and the higher value. Comparing `seq`
*across* pages during a compaction is not safe — the old (full) page can hold
higher-numbered records than a freshly-populated new page — so `page_gen`, not `seq`,
decides which page is authoritative.

**Compaction**, triggered when the current page has no room for the next append:

1. Read the latest valid record of each of the 3 types from the current (full) page.
2. Blank-check the other page (all 256 words `0xFFFFFFFF`) and erase it only if the
   check fails. It is normally already erased, but a power cut between step 4 and
   step 5 of an earlier compaction leaves it holding a lower `page_gen` and stale
   records, so compaction never assumes it is blank.
3. Write the 3 records to the other page, **byte-identical, including
   `boot_counter`.** Resetting the counter on compaction would silently grant a
   flaky image extra retry attempts, breaking the 3-attempt guarantee above.
4. Write the new page's `page_gen` **last** — the single-word atomic handoff. A
   crash before this write leaves the old (full) page still authoritative; the whole
   procedure just replays from step 1 on the next boot.
5. Erase the old page. **Eager**, immediately after the handoff, so the non-current
   page is normally already blank when the next compaction needs it and step 2 skips
   its erase. This is the normal case, not an invariant: a power cut between step 4
   and this erase leaves a stale non-current page. That is harmless — its `page_gen`
   is lower, so it is never selected — and step 2 of the next compaction cleans it up.

**Watchdog.** Steps 2 and 5 can each erase a page (up to 500 ms, D6), about 1 s
together — the whole watchdog budget. D6 requires a feed between multi-page erases;
the journal module knows nothing about the watchdog, so the feed point between the
two erases is marked in `journal_compact` and wired in during Phase 2.

**Rejected — lazy erase of the old page.** Defer the erase until the page is next
needed as a compaction target. The erase count is the same either way, but lazy
leaves a page with a valid, lower `page_gen` and stale records sitting in flash for
the whole life of the current page instead of only during the power-cut window
above. If the current page's `page_gen` word were ever lost, the selector would fall
back to that stale page and resurrect old state; eager keeps that exposure as short
as possible.

**Proactive headroom check: the bootloader guarantees room before every jump.**
Compaction is bootloader-only (D6) — the app's own flash writes never compact, they
just retry on the next boot if an append fails. Before jumping to the app on every
boot, the bootloader checks whether the current page has room for at least one more
record — bare minimum, exactly 1 — and compacts first if not. This check runs
**before** this boot's `boot_counter` bit-clear above, so if compaction was needed,
the bit-clear and the rest of this boot land on the fresh page.

**Why bare minimum, not headroom.** Without this check, the app's retry-next-boot has
no bound, and a healthy image could exhaust its 3 boot attempts purely because the
journal was full, not because anything was wrong with it. Room for exactly 1 record
is the simplest rule that still bounds the app's retry to at most one extra boot.
Revisit if a boot is ever found needing more than one append in a single session.

**Normal-boot scan (no compaction).** Find the current page via `page_gen`, then walk
its records front-to-back, latching the highest-`seq` CRC-valid record per type. Stop
at the first record whose `record_crc` fails to verify against bytes 0–15 — not
literally the first all-`0xFF` slot. Because `record_crc` is always the last word
written, a torn append leaves some words written and `record_crc` still
`0xFFFFFFFF`, which fails to verify against the partial content the same way a
genuinely blank slot does; one check catches both.

**Parked — first-boot / manufacturing provisioning.** A blank chip has both pages at
`page_gen = 0xFFFFFFFF`; nothing exists yet for the normal scan to find. Something
must write the bootstrap records (`page_gen = 1`, an initial `ACTIVE_SLOT`, and
initial `STATUS_A`/`STATUS_B`) before normal boot logic applies. Revisit once the
bootloader's boot/validate/jump logic exists, since provisioning is really "what the
bootloader does when the journal is empty," a case that logic has to handle anyway.

---

## D9 — SRAM no-init boot-progress marker

**Decision.** A single `u32` word, `boot_marker`, at a fixed address at the
**bottom** of SRAM (`0x20000000`), excluded from both the bootloader's and the app's
zero-init (`.bss`)/copy-init (`.data`) startup so its value survives any reset that
does not remove power. No CRC — the sentinel value is self-validating, the same
reasoning as `magic` in D7.

**Mechanism.**

- On every bootloader entry (any reset), read `boot_marker` first, then immediately
  clear it to a neutral value before doing anything else.
- If the value just read was the sentinel, the previous reset happened **during app
  execution** — the bootloader had already reached the jump last time.
- Any other value (garbage, `0xFFFFFFFF`, leftover from a brown-out) means the
  reset's origin is unknown or possibly cold; treat it conservatively.
- Right before jumping to the app, write the sentinel back.

**Why this exists.** Errata SYSCTL#21: `RESC` may not reliably log the reset cause,
so the hardware cannot be trusted to say why a reset happened. This is an
independent, software-controlled signal instead.

**Why the bottom of SRAM, not the top.** The top of SRAM is where the stack lives —
the initial SP is typically at or near `0x20008000`, and D7's vector-table check
already validates `SP ≤ 0x20008000` (hardware-verified, D6 bench tests). Reserving a
word at the top would lower that ceiling to `0x20007FFC` for both builds and require
revisiting already-verified logic. Reserving a word at the bottom only requires each
build's linker to start `.data`/`.bss` placement 4 bytes higher — no existing
validated logic changes.

**Scope kept minimal.** Boot-progress marker only — no crash diagnostics and no
app-to-bootloader request channel. Both were considered and set aside; revisit only
if a concrete need for either surfaces.

**Open.** The sentinel value itself is not yet chosen.
