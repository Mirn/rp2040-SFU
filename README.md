# RP2040-SFU — "Safe Firmware Update" system bootloader for dual-slot flash layouts (like OTA)

**RP2040-SFU** is a compact and robust safe-update bootloader for the Raspberry Pi PICO (RP2040 chip).
It provides:

- **A/B firmware slots** (dual-image fail-safe updates)
- **High-speed UART transport** (921600 baud, DMA-driven, large buffers)
- **Fast, compact differential encoding** of two firmware variants
- **Clean and deterministic jump** into application firmware
- **CRC32 validation** for data integrity
- **Minimal footprint bootloader (~64 KB)**
- **Host-side tools** (Rust encoder + C decoder test tool)
- **No readback command: bootloader only supports full erase + write (no flash read/export).**
- **MIT license**

> ⚠️ **Important:** This design targets **4 MB external flash** (like W25Q32JVZP, **NOT W25Q16!!!**). 
> Standard Raspberry Pi Pico boards with **2 MB flash will NOT work** without editing the flash layout and memory map.

---

# Host uploading tool
(windows)
https://github.com/Mirn/Boot_F4_fast_uart/tree/master 

(Linux Wine + CP2102n reset adaptation ) 
https://github.com/Mirn/rp2040-SFU/tree/main/Linux_port
TODO: fix this crap, rewrite to native Linux


## Overview

RP2040-SFU implements a **fail-safe firmware update** mechanism based on *dual flash slots* (A/B). 
The bootloader occupies a fixed 64 KB region (0x10000000–0x1000FFFF), and both application images reside above it.

Updating works by:

- Sending host-encoded differential blocks via UART (BIN2Page format encodes **two variants**)
- Bootloader reconstructs **one** variant into the inactive slot from this stream;
  the other physical slot remains as a fallback
- CRC32 is calculated and only valid images are marked as usable
- On next boot, the bootloader selects the most recent valid slot and jumps into it

There is **no position-independent code**: 
each firmware image **must be linked to its correct slot base address**.

**Why this exists**
This project was created because existing RP2040 bootloaders are either USB-based, slow, fragile, or unsuitable for factory/field updates.
rp2040-SFU is a UART-first, DMA-driven, production-oriented bootloader designed for high-speed, reliable firmware updates on custom RP2040 hardware with external flash.

**Not a beginner-friendly project.**
This bootloader is intended for engineers who understand RP2040 flash layout, linker scripts, and low-level firmware update flows.

## Architecture

### Bootloader Responsibilities

The bootloader performs:

1. **High-speed UART RX**
   - UART0 @ 921600 baud
   - DMA ring buffer (32 KB)
   - Large secondary software buffer (128 KB)
   - Overflow/error statistics
   - Protocol speed-optimized for modern USB-UART bridges like CP2102n FT232 and etc (continuous upstream with no pauses caused by USB buffering)

2. **Packet receiving layer**
   - Frames incoming UART bytes
   - Calls `sfu_command_parser(code, body, size)`

3. **SFU Commands**
   - `CMD_BEGIN_UPDATE` (erase all)
   - `CMD_WRITE_BLOCK` (BIN2Page block or RAW data of firmware, automatically detect!)
   - `CMD_FINISH_UPDATE`

4. **Flash writing**
   - Decodes BIN2Page blocks into either the A-variant or the B-variant
     (selected via the "shift" flag)
   - Writes pages **only into the inactive slot**
   - The other slot remains untouched and serves as fallback

5. **Integrity checks**
   - CRC32 for each slot
   - two different CRC32 polynomials, one for UART protocol, another one for slot content signing!
   - Slot metadata update

6. **Application launch**
   - Full hardware quiesce:
     - Reset peripherals
     - Disable IRQs
     - Clean XIP cache
     - Set VTOR to selected slot's vector table
     - Jump to the app's reset handler

---

## Structure
````
Host PC
  |
  |  UART (921600 8n1, DMA RX)
  v
[Packet Layer] -> [Command Parser]
                      |
           +----------------------+
           |   inactive slot      |
           | (A or B, selected    |
           |   at CMD_BEGIN)      |
           +----------+-----------+
                      |
                  Decode pages
                      |
           Write to inactive slot
                      |
                CRC + Metadata
                      |
               Variant Selector
                      |
                     Jump
````

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

…which must have identical length, and produces a single compact stream.
On the device side, this stream can be decoded into either of the two variants
(depending on the target slot), while the other physical slot remains untouched.

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

### Usage example inside cmakelist.txt:
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

syntax:

````
bin2page_encoder.exe inputfileA.bin inputfileB.bin outputfile.page2bin
````

This stream contains enough information to reconstruct **either** of the two images
block-by-block (depending on a "shift" flag in the decoder) without storing them fully in RAM.

In this RP2040 SFU implementation the bootloader only reconstructs **one** image per update:
it always writes to the **inactive** slot, while the other slot remains untouched and serves as a fallback.
The host decides which of the two variants ("A" or "B") should be written into the target slot.

The encoder does not know or encode flash offsets — it operates purely on file indices.

### Update semantics and failover

- At boot, the SFU scans both slots (A and B), checks their CRC32 and timestamps,   and selects the **newest valid** image to run.
- When `CMD_ERASE` is received, the bootloader **switches to the other slot** and erases only   that slot's region. The currently running slot is never the erase/write target.
- During `CMD_WRITE`, the encoded BIN2Page stream is decoded into pages and written only into  the inactive slot. Another slot is left unchanged and remains a fallback.
- On `CMD_START`, the bootloader verifies the full-body CRC of the new image, updates  CRC + timestamp metadata for that slot, and only then allows the jump into the new firmware.

In other words: **one slot is updated, one slot is preserved**, and the bootloader always picks
the newest valid one on the next boot.

### Page2bin Format

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
 - Performs simple magic-word based framing (start signature + header + payload + CRC)
 - Delivers complete frames to sfu_command_parser()
 - Handles timeouts (PACKET_TIMEOUT_mS)
 - Provides consistent transport even over unstable USB-UART adapters

## Bootloading process finalization

Once CMD_FINISH_UPDATE is received:
 - Bootloader computes CRC32 for the updated slot 
 - Existing metadata of the untouched slot is preserved 
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


# Safety model

This SFU is designed to be robust against corrupted transport, malformed update images, and partial or failed update sessions.  
Below is an overview of the safety model and the main invariants the code enforces.

### High-level guarantees

- The bootloader **never writes outside the selected firmware slot**.
- The currently active slot is **never erased or overwritten** during an update.
- A new firmware image is considered **bootable only if**:
  - Its flash region passes a full CRC32 check, and
  - The vector table (SP + reset handler) passes strict sanity checks.
- If an update fails at any stage, the system **falls back to the last valid image** (A/B scheme).
- Malformed or noisy UART traffic can cause the update session to abort, but **cannot brick the device** (as long as there is at least one valid slot).

---

### Transport & packet layer

**UART + DMA ring buffer**

- Incoming bytes are stored in a DMA ring, then copied into a larger software ring buffer.
- Buffer overflows are detected and counted (`rx_overfulls`); excess data is dropped instead of overwriting memory.

**Packet framing**

- Each packet starts with a 32-bit signature (`PACKET_SIGN_RX`), searched using a sliding 4-byte window.  
  Any noise in the stream will eventually be resynchronized.
- Header checks:
  - `packet_code` and `packet_code_n` must be bitwise complements.
  - `packet_size` must be ≤ `PACKET_MAX_SIZE` and aligned to 4 bytes.
- Body/CRC reception:
  - All payload bytes go into `packet_buf` until the expected size is reached.
  - If `packet_buf` would overflow, the parser aborts the packet and resets its state.
  - CRC32 is computed over the entire header+body (excluding the last 4-byte CRC field) and must match the CRC transmitted by the host.

**Timeouts**

- A global timeout detects “stalled” sessions: if no complete packet is received for `PACKET_TIMEOUT_mS`, the current packet is discarded and `sfu_command_timeout()` is called.
- Timeouts **never** forward a partially received packet to the SFU layer.

Result: corrupted or incomplete packets are dropped; at worst the update session fails and must be restarted.

---

### SFU command layer

The SFU layer sits on top of the packet parser and implements the actual update protocol.

**INFO**

- `SFU_CMD_INFO` is read-only: it inspects both slots (A/B), reports IDs, flash layout, and current selection.  
  It never modifies flash.

**ERASE**

- The host announces the firmware size as a 32-bit value.
- If `firmware_size == 0` or `firmware_size > (MAIN_END - MAIN_START_FROM)`, SFU immediately returns `SFU_CMD_WRERROR` and does not touch flash.
- On valid `firmware_size > 0`:
  - `main_selector` is flipped to the **inactive** slot.
  - The signature/metadata block of the target slot is erased.
  - The main region of the slot is erased sector-by-sector until it fully covers the firmware range.
  - The erased region is verified to contain only `0xFFFFFFFF`.
  - Internal state is initialized:
    - `write_addr` / `write_addr_real` set to slot base.
    - `fw_fullbody_crc32` reset.
    - `main_update_started = true`.
    - BIN2Page decoder is reset.

Only after a successful ERASE the slot becomes eligible for WRITE.

**WRITE**

- `SFU_CMD_WRITE` packets must:
  - Have a 4-byte address prefix (`body_addr`).
  - Provide data that starts exactly at `write_addr`. Any out-of-order or duplicate writes are ignored.
- Data is fed into the BIN2Page decoder in fixed-size blocks (page-sized buffers padded with 0xFF).  
  The decoder calls `sfu_real_writer()` for each fully decoded output block.

**START**

- `SFU_CMD_START` triggers:
  - `bin2page_finish()` to flush any remaining buffered data.
  - Final CRC32 check against the expected value supplied by the host.
- Only if CRC matches and **no decoder/write errors** occurred:
  - Slot metadata (CRC + timestamp) is written.
  - On the next boot (or immediately after), the bootloader may select this slot as the “latest valid” firmware.

If CRC or BIN2Page decoding fails, the slot is left **unsigned** and will not be selected as valid.

---

### Flash write constraints

Flash writes are funneled through `sfu_real_writer()`:

- Writes are allowed **only** if `main_update_started == true` (i.e. after a valid ERASE).
- When the page being written overlaps the vector table (first words at `MAIN_RUN_FROM`), the function:
  - Extracts the prospective initial SP and reset PC from the page.
  - Validates them via `check_run_context()` (stack in valid RAM range, PC in code region, PC Thumb bit set, alignment, etc.).
  - If validation fails, no flash write is performed and the update session is marked as failed.
- The target address is always checked:
  - `write_addr_real + FLASH_PAGE_SIZE` must not exceed `MAIN_END`.
  - Writes must be aligned to the configured page size.

Result: even with a malformed BIN2Page stream or buggy host encoder, flash writes cannot escape the slot boundaries or create a blatantly invalid vector table.

---

### BIN2Page decoder safety

The BIN2Page decoder on the MCU side is defensive by design:

- Input and output block sizes are fixed (`BIN2PAGE_INPUT_BSIZE == BIN2PAGE_OUTPUT_BSIZE == 256`).
- All index arithmetic (padding, address table, data patches) is range-checked:
  - If any index or derived offset would go outside the 256-byte block, the block is treated as invalid.
- On structural errors:
  - `decode_errors` is incremented.
  - The decoder stops applying patches and instead emits the original 256-byte block unchanged.
- `bin2page_finish()` flushes the remaining data and also reports whether any structural errors were seen.

The SFU layer treats any non-zero `decode_errors` as a **hard failure** for the update.

---

### Slot selection & boot

On startup, the bootloader:

1. **Scans both slots (A and B)**:
   - Reads their metadata (CRC, timestamp) and recomputes CRC32 over the full slot region.
   - Only slots with matching CRC are considered valid.
2. **Selects the latest valid slot**:
   - If both are valid, the one with the newer timestamp wins.
   - If only one is valid, that one is used.
   - If neither is valid, the bootloader stays in SFU mode and waits for an update.
3. **Performs a final context check** before jumping:
   - Validates initial SP and reset PC again (same checks as in `sfu_real_writer`).
   - If the context is invalid, the slot is treated as broken and not booted.

Only after all these checks pass does the bootloader quiesce the system and jump to the application.

---

### Failure modes

In practice, the following failure modes are possible and handled gracefully:

- **Garbage or noisy UART input**  
  → Packets fail header/size/CRC checks or time out; SFU stays in bootloader mode.

- **Update interrupted mid-transfer**  
  → Partially written slot fails CRC or BIN2Page checks; no valid metadata is written; the slot is not selected on boot.

- **Encoder/format bugs (host side)**  
  → BIN2Page decoder errors and/or CRC mismatch; update is rejected; existing valid slot remains bootable.

- **Corrupted flash after manufacturing or field issues**  
  → Slot fails CRC or context checks; bootloader falls back to the other slot or stays in SFU mode.

In all cases, the SFU either:
- boots a previously valid image, or  
- stays in a safe “bootloader only” mode, waiting for a new update.

# based on SFU bootloader for STM32
<details>
<summary><strong>Background: Why this SFU originally started on STM32F4</strong></summary>

### Why built-in UART bootloaders were not enough

Originally, this SFU was implemented for STM32F4 MCUs.
At that time, the built-in UART bootloader had several practical limitations:

- Assumed very conservative, RS-232-style UART speeds  
- Poor fit for modern USB-UART bridges (CP2102, FTDI, etc.)
- Extremely slow firmware upload for large images
- No slot-based update, rollback, or metadata handling

These limitations made the default bootloader unsuitable for
production flashing and frequent firmware updates.

A detailed analysis (in Russian) can be found in this Habr article:  
https://habr.com/ru/articles/305800/

English (Google Translate):  
https://translate.google.com/translate?sl=ru&tl=en&u=https://habr.com/ru/articles/305800/

</details>

# **This system will not work on 2 MB flash without modifying the memory layout and linker scripts.**
