#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>

// Initialize the framebuffer
void fb_init(void);

// Clear the entire screen with a color
void fb_clear(uint32_t color);

// Draw a single pixel
void fb_draw_pixel(uint32_t x, uint32_t y, uint32_t color);

// Draw a filled rectangle
void fb_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);

// Draw a single character
void fb_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg_color, uint32_t bg_color);

// Draw a string
void fb_draw_string(uint32_t x, uint32_t y, const char *str, uint32_t fg_color, uint32_t bg_color);

#endif