#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "hardware/structs/sysinfo.h"
#include "hardware/vreg.h"

/*
 * Board Configuration Variants:
 * 
 * BOARD_M1 - M1 GPIO layout
 * BOARD_M2 - M2 GPIO layout
 * BOARD_Z0 - Waveshare RP2350-PiZero
 * BOARD_TW - UKTailwind Pico Computer 3
 * 
 * PSRAM pin is auto-detected based on chip package:
 *   RP2350B: GPIO47 (for both M1 and M2)
 *   RP2350A: GPIO19 (M1) or GPIO8 (M2)
 * 
 * M1 GPIO Layout:
 *   HDMI: CLKN=6, CLKP=7, D0N=8, D0P=9, D1N=10, D1P=11, D2N=12, D2P=13
 *   SD:   CLK=2, CMD=3, DAT0=4, DAT3=5
 *   PS/2: CLK=0, DATA=1
 *   I2S:  DATA=26, CLK=27, LRCK=28
 * 
 * M2 GPIO Layout:
 *   HDMI: CLKN=12, CLKP=13, D0N=14, D0P=15, D1N=16, D1P=17, D2N=18, D2P=19
 *   SD:   CLK=6, CMD=7, DAT0=4, DAT3=5
 *   PS/2: CLK=2, DATA=3
 *   I2S:  DATA=9, CLK=10, LRCK=11
 *
 * Z0 (Waveshare RP2350-PiZero, an RP2350B):
 *   HDMI: GPIO32..39 on pio0 (GPIO base 16): data lanes 2/1/0 on 32..37,
 *         clock on 38/39, no pair inversion
 *   SD:   SPI1, CLK=30, MOSI=31, MISO=40, CS=43
 *   PS/2: keyboard CLK=2, DATA=3 (pio1); mouse CLK=0, DATA=1
 *   NES:  CLK=4, DATA=6, LATCH=7
 *   PSRAM CS: GPIO47
 *
 * TW (UKTailwind Pico Computer 3, an RP2350B):
 *   HDMI: as M2 (CLKN=12 .. D2P=19, pio1)
 *   SD:   SPI1, CLK=30, MOSI=31, MISO=28, CS=33
 *   I2S:  on-board PCM5102A, BCK=10, LRCK=11, DIN=22
 *   USB:  native USB through the on-board CH334U hub (4 USB-A ports)
 *   Console: UART1 TX=8, RX=9 (the on-board CH340C)
 *   No PS/2 or pad port: PS/2 keyboard CLK=2, DATA=3, mouse CLK=0, DATA=1
 *   and the NES pad CLK=4, DATA=6, LATCH=7 on the EXTERNAL I/O header
 *   PSRAM CS: GPIO47
 *
 * CPU/PSRAM Speed (set via CMake -DCPU_SPEED=xxx -DPSRAM_SPEED=xxx):
 *   252 MHz - no overclock (default for stable operation)
 *   378 MHz - medium overclock
 *   504 MHz - high overclock
 */

// Default to M1 if no config specified
#if !defined(BOARD_M1) && !defined(BOARD_M2) && !defined(BOARD_Z0) && !defined(BOARD_TW)
#define BOARD_M1
#endif

//=============================================================================
// CPU/PSRAM Speed Defaults (can be overridden via CMake)
//=============================================================================
#ifndef CPU_CLOCK_MHZ
#define CPU_CLOCK_MHZ 252
#endif

#ifndef CPU_VOLTAGE
#define CPU_VOLTAGE VREG_VOLTAGE_1_50
#endif

#ifndef PSRAM_MAX_FREQ_MHZ
#define PSRAM_MAX_FREQ_MHZ 133
#endif

//=============================================================================
// PSRAM Pin Auto-Detection
//=============================================================================

// PSRAM pin for RP2350A variants
#ifdef BOARD_M1
#define PSRAM_PIN_RP2350A 19
#elif defined(BOARD_Z0) || defined(BOARD_TW)
#define PSRAM_PIN_RP2350A 47
#else
#define PSRAM_PIN_RP2350A 8
#endif

// PSRAM pin for RP2350B (always GPIO47)
#define PSRAM_PIN_RP2350B 47

// Runtime function to get PSRAM pin based on chip package
static inline uint get_psram_pin(void) {
    // Check if RP2350A (bit 0 set) or RP2350B (bit 0 clear)
    uint32_t package_sel = *((io_ro_32*)(SYSINFO_BASE + SYSINFO_PACKAGE_SEL_OFFSET));
    if (package_sel & 1) {
        // RP2350A - use board-specific pin
        return PSRAM_PIN_RP2350A;
    } else {
        // RP2350B - always GPIO47
        return PSRAM_PIN_RP2350B;
    }
}

//=============================================================================
// M1 Layout Configuration
//=============================================================================
#ifdef BOARD_M1

// HDMI Pins
#define HDMI_PIN_CLKN 6
#define HDMI_PIN_CLKP 7
#define HDMI_PIN_D0N  8
#define HDMI_PIN_D0P  9
#define HDMI_PIN_D1N  10
#define HDMI_PIN_D1P  11
#define HDMI_PIN_D2N  12
#define HDMI_PIN_D2P  13

#define HDMI_BASE_PIN HDMI_PIN_CLKN

// SD Card Pins
#define SDCARD_PIN_CLK    2
#define SDCARD_PIN_CMD    3
#define SDCARD_PIN_D0     4
#define SDCARD_PIN_D3     5

// PS/2 Keyboard Pins
#define PS2_PIN_CLK  0
#define PS2_PIN_DATA 1

// PS/2 Mouse Pins
#define PS2_MOUSE_CLK  14
#define PS2_MOUSE_DATA 15

// I2S Audio Pins
#define I2S_DATA_PIN       26
#define I2S_CLOCK_PIN_BASE 27

#endif // BOARD_M1

//=============================================================================
// M2 Layout Configuration
//=============================================================================
#ifdef BOARD_M2

// HDMI Pins
#define HDMI_PIN_CLKN 12
#define HDMI_PIN_CLKP 13
#define HDMI_PIN_D0N  14
#define HDMI_PIN_D0P  15
#define HDMI_PIN_D1N  16
#define HDMI_PIN_D1P  17
#define HDMI_PIN_D2N  18
#define HDMI_PIN_D2P  19

#define HDMI_BASE_PIN HDMI_PIN_CLKN

// SD Card Pins
#define SDCARD_PIN_CLK    6
#define SDCARD_PIN_CMD    7
#define SDCARD_PIN_D0     4
#define SDCARD_PIN_D3     5

// PS/2 Keyboard Pins
#define PS2_PIN_CLK  2
#define PS2_PIN_DATA 3

// PS/2 Mouse Pins
#define PS2_MOUSE_CLK  0
#define PS2_MOUSE_DATA 1

// I2S Audio Pins
#define I2S_DATA_PIN       9
#define I2S_CLOCK_PIN_BASE 10

#endif // BOARD_M2

//=============================================================================
// Z0 Layout Configuration: Waveshare RP2350-PiZero (murm386 "Z2")
//=============================================================================
#ifdef BOARD_Z0

// HDMI on GPIO32..39 from pio0 (its GPIO window at 16): data lanes first,
// the clock on 38/39. Set for the driver in CMakeLists.txt (drivers/hdmi_pio).
#define HDMI_BASE_PIN 32

// SD Card Pins (SPI1)
#define SDCARD_PIN_CLK    30
#define SDCARD_PIN_CMD    31
#define SDCARD_PIN_D0     40
#define SDCARD_PIN_D3     43

// PS/2 Keyboard Pins, on pio2 with the NES pad and I2S: pio0 is the video's,
// pio1 the PIO-USB host's (it takes a whole PIO)
#define PS2_PIN_CLK  2
#define PS2_PIN_DATA 3
#define PS2KBD_PIO   pio2

// PS/2 Mouse Pins
#define PS2_MOUSE_CLK  0
#define PS2_MOUSE_DATA 1

// I2S Audio Pins (a PCM5102A module: DIN 10, BCK 11, LRCK 12)
#define I2S_DATA_PIN       10
#define I2S_CLOCK_PIN_BASE 11

#endif // BOARD_Z0

//=============================================================================
// TW Layout Configuration: UKTailwind Pico Computer 3
//=============================================================================
#ifdef BOARD_TW

// HDMI Pins (as M2)
#define HDMI_PIN_CLKN 12
#define HDMI_PIN_CLKP 13
#define HDMI_PIN_D0N  14
#define HDMI_PIN_D0P  15
#define HDMI_PIN_D1N  16
#define HDMI_PIN_D1P  17
#define HDMI_PIN_D2N  18
#define HDMI_PIN_D2P  19

#define HDMI_BASE_PIN HDMI_PIN_CLKN

// SD Card Pins (SPI1)
#define SDCARD_PIN_CLK    30
#define SDCARD_PIN_CMD    31
#define SDCARD_PIN_D0     28
#define SDCARD_PIN_D3     33

// PS/2 Keyboard Pins (EXTERNAL I/O header)
#define PS2_PIN_CLK  2
#define PS2_PIN_DATA 3

// PS/2 Mouse Pins (EXTERNAL I/O header)
#define PS2_MOUSE_CLK  0
#define PS2_MOUSE_DATA 1

// I2S Audio Pins (the on-board PCM5102A: BCK 10, LRCK 11, DIN 22)
#define I2S_DATA_PIN       22
#define I2S_CLOCK_PIN_BASE 10

#endif // BOARD_TW

#endif // BOARD_CONFIG_H
