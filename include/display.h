#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

#define RAMFB_FOURCC_R8G8B8A8   0x34325241  // 'A', 'R', '2', '4' = ARGB32
#define RAMFB_FOURCC_R8G8B8     0x00325852  // 'X', 'R', '2', '4' = RGB24

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    uint32_t *framebuffer;
} display_info_t;

void display_init(void);
void display_clear(uint32_t color);
void display_draw_pixel(uint32_t x, uint32_t y, uint32_t color);
void display_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
void display_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg_color, uint32_t bg_color);
void display_draw_string(uint32_t x, uint32_t y, const char *str, uint32_t fg_color, uint32_t bg_color);

extern display_info_t display_info;

#endif