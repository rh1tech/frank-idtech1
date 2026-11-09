/*
 * HDMI output: frank-quake2's PIO driver (drivers/hdmi_pio) behind the API
 * the firmware uses (drivers/video_hdmi.c). 640x480@60 from a 320x240
 * 8-bit framebuffer, HDMI audio packets, the line IRQ on core 1.
 */
#pragma once
#ifndef HDMI_H_
#define HDMI_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum g_out {
    g_out_VGA, g_out_HDMI
} g_out;

// after the final sys clock (a multiple of 252 MHz): starts the output, its
// line IRQ on core 1
void graphics_init(g_out g_out);
void graphics_set_res(int w, int h);             // 320x240 only
void graphics_set_buffer(uint8_t *buffer);       // shown at once
void graphics_set_shift(int x, int y);           // picture shift, pixels

// Frame-synchronised updates, applied together at the next vertical blank:
// stage palette entries, commit them, then request the buffer to show.
void graphics_stage_palette(uint8_t i, uint32_t color888);
void graphics_commit_palette(void);              // shown with the next requested buffer
void graphics_request_buffer(uint8_t *buffer);   // show this buffer from the next frame
bool graphics_buffer_pending(void);              // a requested flip has not happened yet
uint32_t graphics_frame_count(void);
bool graphics_wait_vsync(void);                  // false on timeout (no scanout)

#endif
