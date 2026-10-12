
#include "graphics.h"
#include <stdint.h>

extern uint32_t *backbuffer;

#ifndef WIDTH
#define WIDTH 800
#endif

#ifndef HEIGHT
#define HEIGHT 600
#endif

void putpixel(int x, int y, uint32_t color)
{
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT)
        return;

    backbuffer[y * WIDTH + x] = color;
}

static uint32_t blend_color(uint32_t bg, uint32_t fg, uint8_t alpha)
{
    uint32_t inv = 255 - alpha;

    uint32_t r = ((((fg >> 16) & 255) * alpha) +
                  (((bg >> 16) & 255) * inv)) / 255;

    uint32_t g = ((((fg >> 8) & 255) * alpha) +
                  (((bg >> 8) & 255) * inv)) / 255;

    uint32_t b = (((fg & 255) * alpha) +
                  ((bg & 255) * inv)) / 255;

    return (r << 16) | (g << 8) | b;
}

void putpixel_alpha(int x, int y, uint32_t color, uint8_t alpha)
{
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT || alpha == 0)
        return;

    uint32_t *pixel = &backbuffer[y * WIDTH + x];

    if (alpha == 255) {
        *pixel = color;
        return;
    }

    *pixel = blend_color(*pixel, color, alpha);
}

void fill_rect(
    int x,
    int y,
    int w,
    int h,
    uint32_t color
)
{
    if (w <= 0 || h <= 0)
        return;

    if (x < 0) {
        w += x;
        x = 0;
    }

    if (y < 0) {
        h += y;
        y = 0;
    }

    if (x >= WIDTH || y >= HEIGHT)
        return;

    if (w > WIDTH - x)
        w = WIDTH - x;

    if (h > HEIGHT - y)
        h = HEIGHT - y;

    if (w <= 0 || h <= 0)
        return;

    for (int yy = 0; yy < h; yy++) {
        uint32_t *dst = &backbuffer[(y + yy) * WIDTH + x];

        for (int xx = 0; xx < w; xx++)
            dst[xx] = color;
    }
}

void fill_rect_alpha(
    int x,
    int y,
    int w,
    int h,
    uint32_t color,
    uint8_t alpha
)
{
    if (w <= 0 || h <= 0 || alpha == 0)
        return;

    if (alpha == 255) {
        fill_rect(x, y, w, h, color);
        return;
    }

    if (x < 0) {
        w += x;
        x = 0;
    }

    if (y < 0) {
        h += y;
        y = 0;
    }

    if (x >= WIDTH || y >= HEIGHT)
        return;

    if (w > WIDTH - x)
        w = WIDTH - x;

    if (h > HEIGHT - y)
        h = HEIGHT - y;

    if (w <= 0 || h <= 0)
        return;

    for (int yy = 0; yy < h; yy++) {
        uint32_t *dst = &backbuffer[(y + yy) * WIDTH + x];

        for (int xx = 0; xx < w; xx++)
            dst[xx] = blend_color(dst[xx], color, alpha);
    }
}

void rect(int x, int y, int w, int h, uint32_t color)
{
    if (w <= 0 || h <= 0)
        return;

    for (int xx = x; xx < x + w; xx++) {
        putpixel(xx, y, color);
        putpixel(xx, y + h - 1, color);
    }

    for (int yy = y; yy < y + h; yy++) {
        putpixel(x, yy, color);
        putpixel(x + w - 1, yy, color);
    }
}

void hline(int x, int y, int w, uint32_t color)
{
    if (y < 0 || y >= HEIGHT || w <= 0)
        return;

    if (x < 0) {
        w += x;
        x = 0;
    }

    if (x >= WIDTH)
        return;

    if (w > WIDTH - x)
        w = WIDTH - x;

    if (w <= 0)
        return;

    uint32_t *dst = &backbuffer[y * WIDTH + x];

    for (int i = 0; i < w; i++)
        dst[i] = color;
}

void vline(int x, int y, int h, uint32_t color)
{
    if (h <= 0)
        return;

    for (int i = 0; i < h; i++)
        putpixel(x, y + i, color);
}

void draw_circle(int xc, int yc, int r, uint32_t color)
{
    if (r <= 0)
        return;

    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if ((x * x + y * y) <= (r * r))
                putpixel(xc + x, yc + y, color);
        }
    }
}

void fill_circle(int xc, int yc, int r, uint32_t color)
{
    if (r <= 0)
        return;

    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if ((x * x + y * y) <= (r * r))
                putpixel(xc + x, yc + y, color);
        }
    }
}

const uint8_t font5x7[96][7] = {
    {0,0,0,0,0,0,0},
    {4,4,4,4,4,0,4},
    {10,10,0,0,0,0,0},
    {10,31,10,10,31,10,0},
    {4,15,20,14,5,30,4},
    {24,25,2,4,8,19,3},
    {12,18,20,8,21,18,13},
    {4,4,0,0,0,0,0},
    {2,4,8,8,8,4,2},
    {8,4,2,2,2,4,8},
    {0,4,21,14,21,4,0},
    {0,4,4,31,4,4,0},
    {0,0,0,0,4,4,8},
    {0,0,0,31,0,0,0},
    {0,0,0,0,0,6,6},
    {1,2,4,8,16,0,0},
    {14,17,19,21,25,17,14},
    {4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2},
    {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14},
    {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14},
    {14,17,17,15,1,1,14},
    {0,6,6,0,6,6,0},
    {0,6,6,0,6,6,12},
    {2,4,8,16,8,4,2},
    {0,0,31,0,31,0,0},
    {8,4,2,1,2,4,8},
    {14,17,1,2,4,0,4},
    {14,17,1,13,21,21,14},
    {14,17,17,31,17,17,17},
    {30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14},
    {30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31},
    {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,14},
    {17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14},
    {7,2,2,2,2,18,12},
    {17,18,20,24,20,18,17},
    {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17},
    {17,25,21,19,17,17,17},
    {14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13},
    {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30},
    {31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14},
    {17,17,17,17,17,10,4},
    {17,17,17,21,21,21,10},
    {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4},
    {31,1,2,4,8,16,31},
    {14,8,8,8,8,8,14},
    {16,8,4,2,1,0,0},
    {14,2,2,2,2,2,14},
    {4,10,17,0,0,0,0},
    {0,0,0,0,0,0,31},
    {8,4,2,0,0,0,0},
    {0,0,14,1,15,17,15},
    {16,16,22,25,17,17,30},
    {0,0,14,17,16,17,14},
    {1,1,13,19,17,17,15},
    {0,0,14,17,31,16,14},
    {6,9,8,28,8,8,8},
    {0,0,15,17,15,1,14},
    {16,16,22,25,17,17,17},
    {4,0,12,4,4,4,14},
    {2,0,6,2,2,18,12},
    {16,16,18,20,24,20,18},
    {12,4,4,4,4,4,14},
    {0,0,26,21,21,21,21},
    {0,0,22,25,17,17,17},
    {0,0,14,17,17,17,14},
    {0,0,30,17,30,16,16},
    {0,0,15,17,15,1,1},
    {0,0,22,25,16,16,16},
    {0,0,15,16,14,1,30},
    {8,8,28,8,8,9,6},
    {0,0,17,17,17,19,13},
    {0,0,17,17,17,10,4},
    {0,0,17,17,21,21,10},
    {0,0,17,10,4,10,17},
    {0,0,17,17,15,1,14},
    {0,0,31,2,4,8,31},
    {2,4,4,8,4,4,2},
    {4,4,4,4,4,4,4},
    {8,4,4,2,4,4,8},
    {8,21,2,0,0,0,0}
};

void draw_char(int x, int y, unsigned char c, uint32_t color)
{
    if (c < 32 || c > 127)
        c = '?';

    const uint8_t *glyph = font5x7[c - 32];

    for (int row = 0; row < 7; row++) {
        uint8_t bits = glyph[row];

        for (int col = 0; col < 5; col++) {
            if (bits & (1 << (4 - col)))
                putpixel(x + col, y + row, color);
        }
    }
}

void draw_string(int x, int y, const char *str, uint32_t color)
{
    while (*str) {
        if (*str == '\n') {
            y += 10;
            x = 0;
        } else {
            draw_char(x, y, *str, color);
            x += 6;
        }

        str++;
    }
}

void draw_centered(int x, int y, int width, const char *str, uint32_t color)
{
    int len = 0;
    const char *p = str;

    while (*p++)
        len++;

    int text_width = len * 6;
    int text_x = x + ((width - text_width) / 2);

    draw_string(text_x, y, str, color);
}