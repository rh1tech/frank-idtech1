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
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <stdio.h>

#include "board_config.h"

// Game selection from doomgeneric.c dispatcher
extern int active_game;

// Flash timing configuration for overclocking
// Must be called BEFORE changing system clock
#ifndef FLASH_MAX_FREQ_MHZ
#define FLASH_MAX_FREQ_MHZ 66
#endif

void __no_inline_not_in_flash_func(set_flash_timings)(int cpu_mhz) {
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

/*
 * Make sure the flash chip has its Quad Enable bit set (from murm386).
 *
 * Winbond parts ship with QE=1. Some boards -- Waveshare RP2350-PiZero (Z0)
 * batches with Puya flash among them -- ship with QE=0, which leaves the
 * flash's IO2/IO3 as WP#/HOLD# rather than data lines. With PSRAM on the
 * same QSPI bus such a board locks up or fails as soon as it is overclocked.
 *
 * QE is non-volatile, so this writes the status register at most once in
 * the chip's life: every later boot reads QE=1 and returns. Only vendors
 * whose QE is bit 1 of SR2, read with 0x35 and written with 0x31, are
 * touched -- on e.g. Macronix 0x35 means "enter QPI" instead. The flash is
 * unreadable while it rewrites a status register, so the whole sequence
 * runs from RAM with interrupts off, before the overclock.
 */
static int __no_inline_not_in_flash_func(flash_ensure_qe)(void) {
    uint8_t tx[4], rx[4];

    tx[0] = 0x9F; tx[1] = 0; tx[2] = 0; tx[3] = 0;
    flash_do_cmd(tx, rx, 4);
    switch (rx[1]) {
    case 0xEF:  // Winbond
    case 0xC8:  // GigaDevice
    case 0x85:  // Puya
    case 0x68:  // Boya
    case 0x5E:  // Zbit
        break;
    case 0x20:  // XMC, but Micron uses 20h too and has no SR2 QE bit
        if (rx[2] != 0x40 && rx[2] != 0x70) return -1;
        break;
    default:
        return -1;
    }

    tx[0] = 0x35; tx[1] = 0;
    flash_do_cmd(tx, rx, 2);
    const uint8_t sr2 = rx[1];
    if (sr2 & 0x02) return 0;

    tx[0] = 0x06;                       // Write Enable
    flash_do_cmd(tx, rx, 1);
    tx[0] = 0x31; tx[1] = sr2 | 0x02;   // Write SR2, keep the other bits
    flash_do_cmd(tx, rx, 2);

    for (int i = 0; i < 100000; i++) {  // WIP; tW is 15 ms at worst
        tx[0] = 0x05; tx[1] = 0;
        flash_do_cmd(tx, rx, 2);
        if (!(rx[1] & 0x01)) break;
    }

    tx[0] = 0x35; tx[1] = 0;
    flash_do_cmd(tx, rx, 2);
    return (rx[1] & 0x02) ? 1 : -2;
}

int main() {
    {
        const uint32_t irq = save_and_disable_interrupts();
        flash_ensure_qe();
        restore_interrupts(irq);
    }

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
