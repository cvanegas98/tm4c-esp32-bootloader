# Flash Memory Map — TM4C123GH6PM

256 KB flash at 0x00000000, single bank, **1 KB erase pages**, **32-bit word programming**,
**2 KB protection blocks** (`FMPPEn` / `FMPREn`).

## Layout

| Region | Base | End | Size | Notes |
|---|---|---|---|---|
| Bootloader | `0x00000` | `0x07FFF` | 32 KB | Immutable root. No backup, not field-updatable. |
| Metadata journal | `0x08000` | `0x087FF` | 2 KB | Two 1 KB pages, ping-pong. |
| Fault log | `0x08800` | `0x09FFF` | 6 KB | Six 1 KB pages. Phase 5 campaign data. |
| Slot A | `0x0A000` | `0x12FFF` | 36 KB | 1 KB header page + 35 KB image. |
| Slot B | `0x13000` | `0x1BFFF` | 36 KB | 1 KB header page + 35 KB image. |
| Free | `0x1C000` | `0x3FFFF` | 144 KB | Reserved: golden recovery image, growth. |

## Slot internals

| | Slot A | Slot B |
|---|---|---|
| Header page | `0x0A000` – `0x0A3FF` | `0x13000` – `0x133FF` |
| Image base (link address) | **`0x0A400`** | **`0x13400`** |
| Image size | 35 KB (`0x8C00`) | 35 KB (`0x8C00`) |

The application's vector table sits at the image base, so `VTOR` is set to
`0x0A400` or `0x13400`. Both are 1 KB aligned.

## Why these boundaries

- **Every region starts on a 1 KB boundary.** 1 KB is the erase granularity, so a
  misaligned boundary means erasing a neighbour.
- **The bootloader ends on a 2 KB boundary.** `FMPPEn` protection is 2 KB granular;
  32 KB is exactly 16 protection blocks, so runtime write-protection covers the
  bootloader and nothing else.
- **Image bases are 1 KB aligned for `VTOR`.** The TM4C123 vector table has 155
  entries = 620 bytes, which rounds up to a 1 KB alignment requirement.
- **Header gets its own erase page.** It is a separate erase unit from the image, so
  the image can be written and verified in full before the validating word is
  written into a page that was never touched during the transfer.
- **Bootloader region is 32 KB because the toolchain caps at 32 KB.** MDK-Lite binds
  before the map does, so the map can never be the reason for a re-layout.

## Atomicity rules

1. The last write that makes an image or a metadata record valid is **a single
   32-bit word**. Everything before it leaves the device booting the old image.
2. A slot being written is invalid for the whole window; the other slot must be
   known-good. This is why A/B works and a single slot cannot.
3. Flash writes only clear bits (1 -> 0). Only an erase sets them back to 1.
   No limit exists on writes-per-word between erases on this part — verified in
   both the datasheet and the errata — so the boot counter clears one bit per
   attempt with no erase.
