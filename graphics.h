#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <stdint.h>

typedef enum {
    FONT_DEFAULT = 0,
    FONT_57,
    FONT_57B,
    FONT_79
} FontType;

extern const uint8_t font5x7[96][7];

void putpixel(int x, int y, uint32_t color);
void putpixel_alpha(int x, int y, uint32_t color, uint8_t alpha);

void fill_rect(int x, int y, int w, int h, uint32_t color);
void fill_rect_alpha(int x, int y, int w, int h, uint32_t color, uint8_t alpha);

void rect(int x, int y, int w, int h, uint32_t color);

void hline(int x, int y, int w, uint32_t color);
void vline(int x, int y, int h, uint32_t color);

void draw_circle(int xc, int yc, int r, uint32_t color);
void fill_circle(int xc, int yc, int r, uint32_t color);

void draw_char(int x, int y, unsigned char c, uint32_t color);
void draw_string(int x, int y, const char *str, uint32_t color, FontType type);
void draw_centered(int x, int y, int width, const char *str, uint32_t color, FontType type);

#endif