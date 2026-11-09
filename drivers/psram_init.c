#include "psram_init.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/xip_ctrl.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "hardware/sync.h"

// PSRAM max frequency from build config (default 133 MHz)
#ifndef PSRAM_MAX_FREQ_MHZ
#define PSRAM_MAX_FREQ_MHZ 133
#endif

// Runtime PSRAM limit (frank-idtech1.ini can change it before psram_init)
int psram_max_freq_mhz = PSRAM_MAX_FREQ_MHZ;

/* One direct-mode transfer with CS1 held for its whole length. In quad
 * mode only the command byte is sent (to leave QPI mode). From murm386. */
static void __no_inline_not_in_flash_func(psram_direct_xfer)(const uint8_t *tx, int n, bool quad) {
    hw_set_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_ASSERT_CS1N_BITS);
    for (int i = 0; i < n; i++) {
        uint32_t w = tx[i];
        if (quad)
            w |= QMI_DIRECT_TX_OE_BITS |
                 QMI_DIRECT_TX_IWIDTH_VALUE_Q << QMI_DIRECT_TX_IWIDTH_LSB;
        qmi_hw->direct_tx = w;
        while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS);
        (void)qmi_hw->direct_rx;
    }
    hw_clear_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_ASSERT_CS1N_BITS);
}

/* Bring the part to a known state before init: leave QPI mode (it survives
 * a warm reset of the RP2350, the PSRAM keeps its power), then a software
 * reset. As murm386 (and the Adafruit/Sparkfun RP2350 PSRAM setup). */
static void __no_inline_not_in_flash_func(psram_reset)(uint cs_pin) {
    gpio_set_function(cs_pin, GPIO_FUNC_XIP_CS1);
    // Flash is unreadable while direct mode is on: nothing may run from it
    const uint32_t irq = save_and_disable_interrupts();
    qmi_hw->direct_csr = 30 << QMI_DIRECT_CSR_CLKDIV_LSB | QMI_DIRECT_CSR_EN_BITS;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS);
    // byte by byte: an initialiser would be read from .rodata in flash
    uint8_t b[1];
    b[0] = 0xF5;
    psram_direct_xfer(b, 1, true);   // exit QPI
    b[0] = 0x66;
    psram_direct_xfer(b, 1, false);  // reset enable
    b[0] = 0x99;
    psram_direct_xfer(b, 1, false);  // reset
    for (volatile int i = 0; i < 2000; i++);  // tRST is 50 ns
    qmi_hw->direct_csr = 0;
    restore_interrupts(irq);
}

void __no_inline_not_in_flash_func(psram_init)(uint cs_pin) {
    psram_reset(cs_pin);

    const int clock_hz = clock_get_hz(clk_sys); 

    gpio_set_function(cs_pin, GPIO_FUNC_XIP_CS1);

    qmi_hw->direct_csr = 10 << QMI_DIRECT_CSR_CLKDIV_LSB | 
                        QMI_DIRECT_CSR_EN_BITS | 
                        QMI_DIRECT_CSR_AUTO_CS1N_BITS;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS);

    const uint CMD_QPI_EN = 0x35; 
    qmi_hw->direct_tx = QMI_DIRECT_TX_NOPUSH_BITS | CMD_QPI_EN;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS);


    const int max_psram_freq = psram_max_freq_mhz * 1000000; 
    
    int divisor = (clock_hz + max_psram_freq - 1) / max_psram_freq;
    if (divisor == 1 && clock_hz > 100000000) {
        divisor = 2;
    }
    
    int rxdelay = divisor;
    if (clock_hz / divisor > 100000000) {
        rxdelay += 1; 
    }

    const int clock_period_fs = 1000000000000000ll / clock_hz;
    
    const int max_select_val = (125 * 1000000) / clock_period_fs;

    const int min_deselect = (18 * 1000000 + (clock_period_fs - 1)) / clock_period_fs - (divisor + 1) / 2;

    qmi_hw->m[1].timing = 
        1 << QMI_M1_TIMING_COOLDOWN_LSB | 
        QMI_M1_TIMING_PAGEBREAK_VALUE_1024 << QMI_M1_TIMING_PAGEBREAK_LSB | 
        max_select_val << QMI_M1_TIMING_MAX_SELECT_LSB | 
        min_deselect << QMI_M1_TIMING_MIN_DESELECT_LSB | 
        rxdelay << QMI_M1_TIMING_RXDELAY_LSB | 
        divisor << QMI_M1_TIMING_CLKDIV_LSB;

    qmi_hw->m[1].rfmt =
        QMI_M0_RFMT_PREFIX_WIDTH_VALUE_Q << QMI_M0_RFMT_PREFIX_WIDTH_LSB | 
        QMI_M0_RFMT_ADDR_WIDTH_VALUE_Q << QMI_M0_RFMT_ADDR_WIDTH_LSB | 
        QMI_M0_RFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M0_RFMT_SUFFIX_WIDTH_LSB | 
        QMI_M0_RFMT_DUMMY_WIDTH_VALUE_Q << QMI_M0_RFMT_DUMMY_WIDTH_LSB | 
        QMI_M0_RFMT_DATA_WIDTH_VALUE_Q << QMI_M0_RFMT_DATA_WIDTH_LSB | 
        QMI_M0_RFMT_PREFIX_LEN_VALUE_8 << QMI_M0_RFMT_PREFIX_LEN_LSB | 
        6 << QMI_M0_RFMT_DUMMY_LEN_LSB;
    
    qmi_hw->m[1].rcmd = 0xEB; 

    qmi_hw->m[1].wfmt =
        QMI_M0_WFMT_PREFIX_WIDTH_VALUE_Q << QMI_M0_WFMT_PREFIX_WIDTH_LSB |
        QMI_M0_WFMT_ADDR_WIDTH_VALUE_Q << QMI_M0_WFMT_ADDR_WIDTH_LSB |
        QMI_M0_WFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M0_WFMT_SUFFIX_WIDTH_LSB |
        QMI_M0_WFMT_DUMMY_WIDTH_VALUE_Q << QMI_M0_WFMT_DUMMY_WIDTH_LSB |
        QMI_M0_WFMT_DATA_WIDTH_VALUE_Q << QMI_M0_WFMT_DATA_WIDTH_LSB |
        QMI_M0_WFMT_PREFIX_LEN_VALUE_8 << QMI_M0_WFMT_PREFIX_LEN_LSB;
    
    qmi_hw->m[1].wcmd = 0x38; 

    qmi_hw->direct_csr = 0;
    
    hw_set_bits(&xip_ctrl_hw->ctrl, XIP_CTRL_WRITABLE_M1_BITS);
}

// Back to SPI mode before a reset. A PSRAM left in QPI mode corrupts the
// boot ROM's flash access after a warm reset (the chip resets, the PSRAM
// does not): flash IDs read as garbage and writes fail to verify.
void __no_inline_not_in_flash_func(psram_exit_qpi)(void) {
    // nothing may run from flash or PSRAM while the QMI is in direct mode
    (void)save_and_disable_interrupts();
    qmi_hw->direct_csr = 10 << QMI_DIRECT_CSR_CLKDIV_LSB |
                         QMI_DIRECT_CSR_EN_BITS |
                         QMI_DIRECT_CSR_AUTO_CS1N_BITS;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS);
    const uint CMD_QPI_EXIT = 0xF5;
    qmi_hw->direct_tx = QMI_DIRECT_TX_NOPUSH_BITS |
                        QMI_DIRECT_TX_IWIDTH_VALUE_Q << QMI_DIRECT_TX_IWIDTH_LSB |
                        CMD_QPI_EXIT;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS);
    qmi_hw->direct_csr = 0;
}
