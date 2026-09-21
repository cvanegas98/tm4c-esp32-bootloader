# Vendor files

Supplied by Texas Instruments, not written for this project.

| File | Origin |
|---|---|
| `startup.s` | TI Stellaris/Tiva startup for Keil MDK. Vector table (155 entries, `AREA RESET`), `Reset_Handler` branching to `__main`. No `ENTRY` directive — the C library supplies the image entry point. |
| `tm4c123gh6pm.h` | TI register definition header for the TM4C123GH6PM. |
