#ifndef RAMFB_H
#define RAMFB_H

#include <stdint.h>

// Conversion functions between endian numbers since Qemu fw_cfg uses big endian
#define BE16(x) __builtin_bswap16(x)
#define BE64(x) __builtin_bswap64(x)
#define BE32(x) __builtin_bswap32(x)

// Pixel format types
#define FORMAT_RGB888 875710290     // 24 bits, red=8, green=8, blue=8
#define FORMAT_XRGB8888 875713112   // 32 bits, alpha=8, red=8, green=8, blue=8
#define FORMAT_RGB565 909199186     // 16 bits, red=5, green=6, blue=5

// ramfb configuration struct
struct QemuRamFBCfg {
    uint64_t address;   // The address of the framebuffer
    uint32_t fourcc;    // The four character code representing the pixel format
    uint32_t flags;     // Flags for framebuffer configuration
    uint32_t width;     // The width of the framebuffer in pixels
    uint32_t height;    // The height of the framebuffer in pixels
    uint32_t stride;    // The stride (number of bytes per row) of the framebuffer
} __attribute__((packed));

// Initialize ramfb configuration
void qemu_ramfb_make_cfg(struct QemuRamFBCfg* cfg, void* fb_address, uint32_t fb_width, uint32_t fb_height);

// Configure the QEMU ramfb
void qemu_ramfb_configure(struct QemuRamFBCfg* cfg);

#endif