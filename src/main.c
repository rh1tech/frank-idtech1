/*
 * frank-idtech1 - id Tech 1 games for RP2350
 * Supports: Doom, Heretic, Hexen, Strife
 * Main entry point with overclocking support
 */
#include "doomgeneric.h"
#include "pico/stdlib.h"
#include "hardware/vreg.h"
#include "hardware/clocks.h"
#include "hardware/structs/qmi.h"
#include <stdio.h>

#include "board_config.h"

// Game selection from doomgeneric.c dispatcher
extern int active_game;

// Flash timing configuration for overclocking
// Must be called BEFORE changing system clock
#define FLASH_MAX_FREQ_MHZ 88

static void __no_inline_not_in_flash_func(set_flash_timings)(int cpu_mhz) {
    const int clock_hz = cpu_mhz * 1000000;
    const int max_flash_freq = FLASH_MAX_FREQ_MHZ * 1000000;

    int divisor = (clock_hz + max_flash_freq - (max_flash_freq >> 4) - 1) / max_flash_freq;
    if (divisor == 1 && clock_hz >= 166000000) {
        divisor = 2;
    }

    int rxdelay = divisor;
    if (clock_hz / divisor > 100000000 && clock_hz >= 166000000) {
        rxdelay += 1;
    }

    qmi_hw->m[0].timing = 0x60007000 |
                        rxdelay << QMI_M0_TIMING_RXDELAY_LSB |
                        divisor << QMI_M0_TIMING_CLKDIV_LSB;
}

int main() {
    // Overclock support: For speeds > 252 MHz, increase voltage first
#if CPU_CLOCK_MHZ > 252
    vreg_disable_voltage_limit();
    vreg_set_voltage(CPU_VOLTAGE);
    set_flash_timings(CPU_CLOCK_MHZ);  // Set flash timings BEFORE clock change
    sleep_ms(100);  // Wait for voltage and timings to stabilize
#endif

    // Set system clock
    // 640x480@60Hz pixel clock is ~25.2MHz, PIO DVI needs 10x = ~252MHz
    // 378 MHz / 15 = 25.2 MHz (also works for HDMI)
    // 504 MHz / 20 = 25.2 MHz (also works for HDMI)
    if (!set_sys_clock_khz(CPU_CLOCK_MHZ * 1000, false)) {
        // Fallback to safe clock if requested speed fails
        set_sys_clock_khz(252 * 1000, true);
    }

    stdio_init_all();

    // Brief startup delay for USB serial connection
    for (int i = 0; i < 3; i++) {
        sleep_ms(500);
    }

    extern void check_crash_report(void);
    check_crash_report();

    printf("frank-idtech1 - Multi-game for RP2350\n");
    printf("System Clock: %lu MHz\n", clock_get_hz(clk_sys) / 1000000);
    printf("Starting game engine...\n");

    // Game name will be determined by WAD selection in DG_StartScreen
    const char *game_name = "idtech1";
    char *argv[] = {(char *)game_name, NULL};
    doomgeneric_Create(1, argv);

    while (1) {
        doomgeneric_Tick();
    }

    return 0;
}
