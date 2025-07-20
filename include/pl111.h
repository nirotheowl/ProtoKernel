#ifndef PL111_H
#define PL111_H

#include <stdint.h>

void pl111_init(void);
void pl111_clear(uint32_t color);
void pl111_draw_pixel(uint32_t x, uint32_t y, uint32_t color);
void pl111_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
void pl111_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg_color, uint32_t bg_color);
void pl111_draw_string(uint32_t x, uint32_t y, const char *str, uint32_t fg_color, uint32_t bg_color);

#endif