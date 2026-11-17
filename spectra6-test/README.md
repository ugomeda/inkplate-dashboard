# EE02 T133A01 bring-up starter for ESP-IDF 6.1

This is a proposed first-pass port of the Seeed T133A01 initialization, SPI data transfer, and refresh sequence to **native ESP-IDF**.

**Not hardware-tested.** Validate GPIO assignments, power sequencing, and controller selection before using it beyond a test panel. Use USB Serial/JTAG output (UART0 GPIO43/44 are used by display signals).

To integrate into an existing firmware repo, copy `components/spectra6/` to its `components/` directory and replace the test main with `main/main.cpp`. Keep your original Inkplate10 main separately.

Configure ESP-IDF target `esp32s3`, octal PSRAM at 80 MHz, and USB Serial/JTAG console. Then `idf.py build flash monitor`.

Pin mapping basis:
https://github.com/philippwaller/esphome-epaper-spectra6-133/blob/main/packages/boards/seeed_studio/xiao_ee02.yaml

Panel command sequence and dual-chip logic adapted from Seeed_GFX. Review and preserve applicable upstream notices when incorporating it into your distributed firmware:
https://github.com/Seeed-Studio/Seeed_GFX/blob/master/TFT_Drivers/T133A01_Defines.h

The first test streams six hardware colors without allocating a full framebuffer. Once this test is verified, implement a 960,000-byte packed framebuffer in PSRAM, and decode indexed PNG into it. Add onboard font/icon rendering for error screens using the same framebuffer.
