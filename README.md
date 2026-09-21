# Fault-Tolerant CAN Bootloader — TM4C123 + ESP32

A bare-metal A/B firmware update system for the TI TM4C123GH6PM, updated over CAN 2.0B
by an ESP32 host running FreeRTOS. Designed so that **power loss at any instant leaves
the device bootable.**

## Hardware

| | |
|---|---|
| Target | TI TM4C123GXL LaunchPad — TM4C123GH6PM, Cortex-M4F, 256 KB flash, 32 KB SRAM, **silicon rev 7** |
| Host | ESP32-DevKitC, ESP-IDF / FreeRTOS |
| Link | CAN 2.0B, two SN65HVD230 transceivers |
| Debug | Onboard Stellaris ICDI over SWD |

Target firmware is bare-metal register-level C — no TivaWare, no HAL.

## Design

The bootloader is the immutable root at `0x00000`. Two application slots hold
independently valid images; a journaled metadata region decides which one boots.

- **A/B slots with two link-time builds** — each release is built twice, for slot A and
  slot B. Nothing is ever copied, so the inactive slot stays intact for rollback.
- **Journaled flash metadata** — sequence-numbered, CRC'd records in ping-pong pages.
  Boot counter clears one bit per attempt, so there is no erase per boot.
- **Bootloader-resident CAN** — safe mode can accept a new image when both slots are
  bad, so a device is always recoverable in the field without a debugger.
- **Single-word commit** — the last write that validates an image or a metadata record
  is one 32-bit word. Everything before it leaves the old image booting.

See [`docs/memory-map.md`](docs/memory-map.md) for the layout and
[`docs/design-decisions.md`](docs/design-decisions.md) for the reasoning and the
rejected alternatives.

## Repository layout

```
bootloader/      bootloader firmware (src/, inc/)
app/             test application, built twice (slot A and slot B variants)
tools/           host-side Python: image packager (header + CRC-32), test tooling
docs/            memory map, design decisions, vendor reference index
```

## Toolchain

Keil MDK-Lite 5.43 (32 KB code limit per project — binds both the bootloader and the
app). arm-none-eabi GCC 14.3 is also installed as a fallback.

**Flashing does not use Keil.** MDK 5.43 ships with a Stellaris ICDI driver
(`lmidk-agdi.dll`) dated 2019; its flash *programming* path fails at every address,
though mass erase and memory reads work. Program the board with **TI LM Flash
Programmer** instead:

1. Post-build step produces a raw binary:
   ```
   fromelf.exe --bin --output ".\Objects\<name>.bin" ".\Objects\<name>.axf"
   ```
   (`fromelf` lives in `…\Keil_v5\ARM\ARMCLANG\bin\`)
2. LM Flash Programmer → Configuration: TM4C123G LaunchPad, interface **ICDI**
3. Program tab → select the `.bin`, set **Program Address Offset** to the region base
   (a `.bin` carries no addresses), tick Verify

Debugging in uVision still works — only programming is broken in that driver.

## Status

**Phase 1 — flash driver, CRC-32, memory map.** Memory map and all architectural
decisions are settled; implementation next.

| Phase | Scope |
|---|---|
| 1 | Flash driver, CRC-32, memory map |
| 2 | Bootloader: validation, VTOR relocation, jump, A/B rollback, boot counter, watchdog, safe mode |
| 3 | CAN transport: bit timing, framing, chunked transfer with ACK/NAK |
| 4 | ESP32 FreeRTOS host: tasks, queue, mutex, TWAI driver |
| 5 | Fault injection campaign and documentation |
