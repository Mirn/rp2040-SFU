# RP2040-SFU — "Safe Firmware Update" system bootloader for dual-slot flash layouts (like OTA)

**RP2040-SFU** is a compact and robust safe-update bootloader for the Raspberry Pi RP2040.  
It provides:

- **A/B firmware slots** (dual-image fail-safe updates)
- **High-speed UART transport** (921600 baud, DMA-driven, large buffers)
- **Fast, compact differential encoding** of two firmware variants
- **Clean and deterministic jump** into application firmware
- **CRC32 validation** for data integrity
- **Minimal footprint bootloader (~64 KB)**
- **Host-side tools** (Rust encoder + C decoder test tool)
- **No read command**, only erase all and update inside 
- **MIT license**

> ⚠️ **Important:** This design targets **4 MB external flash** (like W25Q32JVZP, **NOT W25Q16!!!**).  
> Standard Raspberry Pi Pico boards with **2 MB flash will NOT work** without editing the flash layout and memory map.

---

# Host uploading tool
(windows)
https://github.com/Mirn/Boot_F4_fast_uart/tree/master 

(Linux Wine + CP2102n reset adaptation ) 
https://github.com/Mirn/rp2040-SFU/tree/main/Linux_port
TODO: fix this crap, rewrite to naitive Linux


## Overview

RP2040-SFU implements a **fail-safe firmware update** mechanism based on *dual flash slots* (A/B).  
The bootloader occupies a fixed 64 KB region (0x10000000–0x1000FFFF), and both application images reside above it.

Updating works by:

- Sending host-encoded differential blocks via UART
- Bootloader reconstructs **both** slot images on-the-fly
- CRC32 is calculated and only valid images are marked as usable
- On next boot, the bootloader selects the most recent valid slot and jumps into it

There is **no position-independent code**:  
each firmware image **must be linked to its correct slot base address**.

## Architecture

### Bootloader Responsibilities

The bootloader performs:

1. **High-speed UART RX**
   - UART0 @ 921600 baud
   - DMA ring buffer (32 KB)
   - Large secondary software buffer (128 KB)
   - Overflow/error statistics
   - Protocol speed-optimized for modern USB-uart bridges like CP2102n FT232 and etc (data upstreaming without any pauses by USB reasons)

2. **Packet receiving layer**
   - Frames incoming UART bytes
   - Calls `sfu_command_parser(code, body, size)`

3. **SFU Commands**
   - `CMD_BEGIN_UPDATE` (erase all)
   - `CMD_WRITE_BLOCK` (BIN2Page block or RAW data of firmware, automaticly detect!)
   - `CMD_FINISH_UPDATE`

4. **Flash writing**
   - Reconstruct blockA (slot A) and blockB (slot B)
   - Store them at `FLASH_BASE_A + offset` and `FLASH_BASE_B + offset`

5. **Integrity checks**
   - CRC32 for each slot
   - two different CRC32 polindrome, one for uart protocol, another one for slot content signing!
   - Slot metadata update

6. **Application launch**
   - Full hardware quiesce:
     - Reset peripherals
     - Disable IRQs
     - Clean XIP cache
     - Set VTOR to selected slot's vector table
     - Jump to the app's reset handler

---

## A/B Slot Layout

Example for 4 MB flash:

````
0x10000000  ┌──────────────────────────┐
            │   SFU bootloader (64 KB) │
0x10010000  ├──────────────────────────┤  <-- SLOT A base, Firmware A
            │       Firmware A         │
            │          ...             │
            │  Metadata A (at last 4k) │
0x10200000  ├──────────────────────────┤
            │  non used by SFU (64 KB) │  <-- These 64k can be used, for example, as an EEPROM emulator.
            ├──────────────────────────┤  <-- SLOT B base, Firmware B
0x10210000  │       Firmware B         │
            │          ...             │
            │  Metadata B (at last 4k) │
0x10400000  └──────────────────────────┘

````

**Both images must be built for fixed addresses, since RP2040 does not support PIC/PIE firmware for XIP execution.**

## BIN2Page Encoder (Rust, optional)
### Purpose

The encoder takes two full binary images:

````
input_page_A.bin
input_page_B.bin
````

…which must have identical length, and produces a single compact stream:

````
update.page2bin
````
## usage example 
### 1. Prepare linker scripts

You must build two firmware images, each linked to its own flash base.

Example:
````
FLASH_BASE_A = 0x10010000
FLASH_BASE_B = 0x10210000
````

Edit your memmap.ld and save as memmap_0x10000_shifted.ld for SLOT A:

````
/* SLOT A */
FLASH (rx) : ORIGIN = 0x10010000, LENGTH = 0x001F0000
````

and duplicate it for SLOT B as memmap_0x210000_shifted.ld for SLOT B:

````
/* SLOT B */
FLASH (rx) : ORIGIN = 0x10210000, LENGTH = 0x001F0000
````

Ensure both images fit their regions.

### 2. Build firmware A & B (see next step "usage example inside cmakelist.txt")

Convert to raw binaries if needed:

````
arm-none-eabi-objcopy -O binary tester_a.elf tester_a.bin
arm-none-eabi-objcopy -O binary tester_b.elf tester_b.bin
````

**Both binaries must have the same size.**

### usage example inside cmakelist.txt:
````
function(make_variant VAR_NAME LD_FILE)
    add_executable(${VAR_NAME}
    ...
    pico_set_linker_script(${VAR_NAME}  ${LD_FILE})
    ...
    pico_add_extra_outputs(${VAR_NAME})
endfunction()

make_variant(payspot_tester_a ${CMAKE_CURRENT_LIST_DIR}/memmap_0x10000_shifted.ld)
make_variant(payspot_tester_b ${CMAKE_CURRENT_LIST_DIR}/memmap_0x210000_shifted.ld)

add_custom_target(postprocess ALL
    COMMAND ../bin2page_encoder/target/release/bin2page_encoder.exe ${CMAKE_CURRENT_BINARY_DIR}/tester_a.bin ${CMAKE_CURRENT_BINARY_DIR}/tester_b.bin ${CMAKE_CURRENT_BINARY_DIR}/tester.page2bin
    DEPENDS tester_a tester_b
    COMMENT "Running post-processing step (bin2page_encoder)"
)
````

sytax:

````
bin2page_encoder.exe inputfileA.bin inputfileB.bin outputfile.page2bin
````

This stream contains enough information for the bootloader to reconstruct both images block-by-block without storing them fully in RAM.

The encoder does not know or encode flash offsets — it operates purely on file indices.

### page2bin Format

It begins with a header "BIN2Page" used for format detection. N bytes for extra info (now 0 bytes). 256-byte blocks follow.
Each generated 256-byte block contains:

 - A header byte: (MSB = 1 (full-format marker), Lower bits = (addr_count + padding))
 - padding bytes of 0xFF
 - A list of byte offsets that differ between A and B
 - A list of substituted bytes for B
 - A raw sequence of A bytes (data[])

## UART & Packet Layer

The UART subsystem (usart_mini) provides:
 - 921600 baud
 - DMA RX ring buffer (32 KB)
 - Secondary expansion buffer (128 KB)
 - Real-time overflow/error statistics
 - Automatic draining to packet parser
 - The packet layer:
 - Performs SLIP-like framing
 - Delivers complete frames to sfu_command_parser()
 - Handles timeouts (PACKET_TIMEOUT_MS)
 - Provides consistent transport even over unstable USB-UART adapters

## Bootloading process finalization

Once CMD_FINISH_UPDATE is received:
 - Bootloader computes CRC32 for both slots
 - Marks valid slot(s)
 - On next reboot selects the newest valid variant
 - Clears hardware state
 - Jumps into firmware

### Flash Size Notes (4 MB Required)

This implementation expects:
 - Bootloader: 64 KB
 - Two firmware slots of substantial size
 - Metadata region for CRC and versioning

The default layout targets 4 MB flash chips (e.g., W25Q32JV).
If your board only has 2 MB (standard Pico):
 - You must manually edit linkers and flash layout
 - Reduce slot sizes
 - Possibly remove dual-image logic

# Integration Notes

 - Firmware is not position-independent
 - Each slot requires its own linked binary
 - Encoder is address-agnostic
 - Bootloader controls actual flash placement
 - UART transport can be replaced with other media

# **The system will not work on 2 MB without modification.**
