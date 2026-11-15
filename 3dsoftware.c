
#include <stdint.h>
#include "graphics.h"
#include <stddef.h>

extern void *kmalloc(size_t size);
extern void kfree(void *ptr);

#define SW3D_MAX_W 440
#define SW3D_MAX_H 260
#define SW3D_ZSIZE (SW3D_MAX_W * SW3D_MAX_H)

#define SW3D_WHITE 0x00FFFFFF
#define SW3D_GRAY  0x00808080
#define SW3D_BLUE  0x000080FF
#define SW3D_CYAN  0x0000FFFF
#define SW3D_DARK  0x000B111B
#define SW3D_PANEL 0x0018202C
#define SW3D_BORDER 0x00405060

typedef struct
{
    int x;
    int y;
    int z;
} SW3DVec;

typedef struct
{
    int x;
    int y;
    int z;
} SW3DVertex;

static uint32_t *sw3d_depth = NULL;

static int sw3d_rx = 20;
static int sw3d_ry = 30;
static int sw3d_rz = 0;

static int sw3d_view_x;
static int sw3d_view_y;
static int sw3d_view_w;
static int sw3d_view_h;

static int sw3d_dragging;
static int sw3d_last_x;
static int sw3d_last_y;

static int sw3d_sin(int degrees)
{
    degrees %= 360;

    if (degrees < 0)
        degrees += 360;

    int sign = 1;

    if (degrees > 180)
    {
        degrees = 360 - degrees;
        sign = -1;
    }

    if (degrees > 90)
        degrees = 180 - degrees;

    int product = degrees * (180 - degrees);
    int numerator = 4 * product * 65536;
    int denominator = 40500 - product;

    if (denominator == 0)
        return 0;

    return sign * (numerator / denominator);
}

static int sw3d_cos(int degrees)
{
    return sw3d_sin(degrees + 90);
}

static int sw3d_rotate_component(int a, int ca, int b, int cb)
{
    return (a * ca + b * cb) / 65536;
}

int software3d_open(void)
{
    if (sw3d_depth != NULL)
        return 1;

    sw3d_depth = kmalloc(
        SW3D_ZSIZE * sizeof(uint32_t)
    );

    if (sw3d_depth == NULL)
        return 0;

    return 1;
}

void software3d_close(void)
{
    if (sw3d_depth != NULL)
    {
        kfree(sw3d_depth);
        sw3d_depth = NULL;
    }

    sw3d_dragging = 0;
}

unsigned int software3d_get_memory_kb(void)
{
    if (sw3d_depth == NULL)
        return 0;

    return (
        SW3D_ZSIZE * sizeof(uint32_t) + 1023
    ) / 1024;
}

static SW3DVec sw3d_rotate(SW3DVec v)
{
    int sx = sw3d_sin(sw3d_rx);
    int cx = sw3d_cos(sw3d_rx);

    int sy = sw3d_sin(sw3d_ry);
    int cy = sw3d_cos(sw3d_ry);

    int sz = sw3d_sin(sw3d_rz);
    int cz = sw3d_cos(sw3d_rz);

    int y = sw3d_rotate_component(v.y, cx, -v.z, sx);
    int z = sw3d_rotate_component(v.y, sx, v.z, cx);

    v.y = y;
    v.z = z;

    int x = sw3d_rotate_component(v.x, cy, v.z, sy);
    z = sw3d_rotate_component(-v.x, sy, v.z, cy);

    v.x = x;
    v.z = z;

    x = sw3d_rotate_component(v.x, cz, -v.y, sz);
    y = sw3d_rotate_component(v.x, sz, v.y, cz);

    v.x = x;
    v.y = y;

    return v;
}

static SW3DVertex sw3d_project(
    SW3DVec v,
    int width,
    int height
)
{
    SW3DVertex out;

    v.z += 5000;

    if (v.z < 500)
        v.z = 500;

    out.x = width / 2 + (v.x * 230) / v.z;
    out.y = height / 2 - (v.y * 230) / v.z;
    out.z = v.z;

    return out;
}

static int sw3d_edge(
    SW3DVertex a,
    SW3DVertex b,
    int x,
    int y
)
{
    return
        (x - a.x) * (b.y - a.y) -
        (y - a.y) * (b.x - a.x);
}

static void sw3d_triangle(
    SW3DVertex a,
    SW3DVertex b,
    SW3DVertex c,
    uint32_t color
)
{
    int area = sw3d_edge(a, b, c.x, c.y);

    if (area == 0)
        return;

    if (area < 0)
    {
        SW3DVertex temp = b;
        b = c;
        c = temp;
        area = -area;
    }

    int min_x = a.x;
    int max_x = a.x;
    int min_y = a.y;
    int max_y = a.y;

    if (b.x < min_x) min_x = b.x;
    if (c.x < min_x) min_x = c.x;
    if (b.x > max_x) max_x = b.x;
    if (c.x > max_x) max_x = c.x;

    if (b.y < min_y) min_y = b.y;
    if (c.y < min_y) min_y = c.y;
    if (b.y > max_y) max_y = b.y;
    if (c.y > max_y) max_y = c.y;

    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;

    if (max_x >= sw3d_view_w)
        max_x = sw3d_view_w - 1;

    if (max_y >= sw3d_view_h)
        max_y = sw3d_view_h - 1;

    for (int y = min_y; y <= max_y; y++)
    {
        for (int x = min_x; x <= max_x; x++)
        {
            int w0 = sw3d_edge(b, c, x, y);
            int w1 = sw3d_edge(c, a, x, y);
            int w2 = sw3d_edge(a, b, x, y);

            if (w0 < 0 || w1 < 0 || w2 < 0)
                continue;

            int z =
                (w0 * a.z +
                 w1 * b.z +
                 w2 * c.z) / area;

            int index = y * sw3d_view_w + x;

            if ((uint32_t)z < sw3d_depth[index])
            {
                sw3d_depth[index] = (uint32_t)z;

                putpixel(
                    sw3d_view_x + x,
                    sw3d_view_y + y,
                    color
                );
            }
        }
    }
}

static void sw3d_quad(
    SW3DVertex *v,
    int a,
    int b,
    int c,
    int d,
    uint32_t color
)
{
    sw3d_triangle(v[a], v[b], v[c], color);
    sw3d_triangle(v[a], v[c], v[d], color);
}

static void sw3d_render(void)
{   
    if (sw3d_depth == NULL)
        return;

    static const SW3DVec cube[8] =
    {
        {-1000, -1000, -1000},
        { 1000, -1000, -1000},
        { 1000,  1000, -1000},
        {-1000,  1000, -1000},

        {-1000, -1000,  1000},
        { 1000, -1000,  1000},
        { 1000,  1000,  1000},
        {-1000,  1000,  1000}
    };

    SW3DVertex v[8];

    for (int i = 0; i < 8; i++)
    {
        SW3DVec p = sw3d_rotate(cube[i]);

        v[i] = sw3d_project(
            p,
            sw3d_view_w,
            sw3d_view_h
        );
    }

    for (int i = 0; i < sw3d_view_w * sw3d_view_h; i++)
        sw3d_depth[i] = 0xFFFFFFFF;

    for (int y = 0; y < sw3d_view_h; y++)
    {
        for (int x = 0; x < sw3d_view_w; x++)
        {
            putpixel(
                sw3d_view_x + x,
                sw3d_view_y + y,
                SW3D_DARK
            );
        }
    }

    sw3d_quad(v, 0, 1, 2, 3, 0x00E74C3C);
    sw3d_quad(v, 4, 7, 6, 5, 0x003498DB);
    sw3d_quad(v, 0, 4, 5, 1, 0x002ECC71);
    sw3d_quad(v, 3, 2, 6, 7, 0x00F1C40F);
    sw3d_quad(v, 0, 3, 7, 4, 0x009B59B6);
    sw3d_quad(v, 1, 5, 6, 2, 0x00E67E22);
}

static void sw3d_button(int x, int y, int w, const char *label)
{
    fill_rect(x, y, w, 22, SW3D_PANEL);
    rect(x, y, w, 22, SW3D_BORDER);
    draw_string(x + 7, y + 7, label, SW3D_WHITE, FONT_DEFAULT);
}

/*
 * Desenha o conteúdo interno da janela.
 * A decoração da janela continua sendo responsabilidade do shell.c.
 */
void software3d_draw_content(int x, int y, int w, int h)
{   
    if (sw3d_depth == NULL)
        return;

    int button_y = y + 36;
    int button_x = x + 12;

    sw3d_button(button_x, button_y, 38, "X-");
    sw3d_button(button_x + 44, button_y, 38, "X+");
    sw3d_button(button_x + 88, button_y, 38, "Y-");
    sw3d_button(button_x + 132, button_y, 38, "Y+");
    sw3d_button(button_x + 176, button_y, 38, "Z-");
    sw3d_button(button_x + 220, button_y, 38, "Z+");
    sw3d_button(button_x + 264, button_y, 58, "RESET");

    sw3d_view_x = x + 12;
    sw3d_view_y = y + 68;

    sw3d_view_w = w - 24;
    sw3d_view_h = h - 104;

    if (sw3d_view_w > SW3D_MAX_W)
        sw3d_view_w = SW3D_MAX_W;

    if (sw3d_view_h > SW3D_MAX_H)
        sw3d_view_h = SW3D_MAX_H;

    if (sw3d_view_w <= 0 || sw3d_view_h <= 0)
        return;

    fill_rect(
        sw3d_view_x - 1,
        sw3d_view_y - 1,
        sw3d_view_w + 2,
        sw3d_view_h + 2,
        SW3D_BORDER
    );

    sw3d_render();

    draw_string(
        x + w - 90,
        y + h - 22,
        "LMB to rotate",
        SW3D_GRAY,
        FONT_DEFAULT
    );
}

void software3d_mouse_down(
    int win_x,
    int win_y,
    int mouse_x,
    int mouse_y,
    int win_w,
    int win_h
)
{
    int bx = win_x + 12;
    int by = win_y + 36;

    if (mouse_y >= by && mouse_y < by + 22)
    {
        if (mouse_x >= bx && mouse_x < bx + 38)
            sw3d_rx += 10;
        else if (mouse_x >= bx + 44 && mouse_x < bx + 82)
            sw3d_rx -= 10;
        else if (mouse_x >= bx + 88 && mouse_x < bx + 126)
            sw3d_ry += 10;
        else if (mouse_x >= bx + 132 && mouse_x < bx + 170)
            sw3d_ry -= 10;
        else if (mouse_x >= bx + 176 && mouse_x < bx + 214)
            sw3d_rz += 10;
        else if (mouse_x >= bx + 220 && mouse_x < bx + 258)
            sw3d_rz -= 10;
        else if (mouse_x >= bx + 264 && mouse_x < bx + 322)
        {
            sw3d_rx = 20;
            sw3d_ry = 30;
            sw3d_rz = 0;
        }

        return;
    }

    int vx = win_x + 12;
    int vy = win_y + 68;
    int vw = win_w - 24;
    int vh = win_h - 104;

    if (vw > SW3D_MAX_W) vw = SW3D_MAX_W;
    if (vh > SW3D_MAX_H) vh = SW3D_MAX_H;

    if (mouse_x >= vx && mouse_x < vx + vw &&
        mouse_y >= vy && mouse_y < vy + vh)
    {
        sw3d_dragging = 1;
        sw3d_last_x = mouse_x;
        sw3d_last_y = mouse_y;
    }
}

int software3d_mouse_move(int mouse_x, int mouse_y, int left_pressed)
{
    if (!sw3d_dragging || !left_pressed)
        return 0;

    int dx = mouse_x - sw3d_last_x;
    int dy = mouse_y - sw3d_last_y;

    sw3d_last_x = mouse_x;
    sw3d_last_y = mouse_y;

    sw3d_ry -= dx;
    sw3d_rx -= dy;

    if (sw3d_rx > 360) sw3d_rx -= 360;
    if (sw3d_rx < 0) sw3d_rx += 360;

    if (sw3d_ry > 360) sw3d_ry -= 360;
    if (sw3d_ry < 0) sw3d_ry += 360;

    return dx != 0 || dy != 0;
}

void software3d_mouse_up(void)
{
    sw3d_dragging = 0;
}