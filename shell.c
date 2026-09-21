#include <stdint.h>
#include "rtc.h"

#define BOOT_INFO_ADDR 0x8800

/* ------------------------------------------------------------
 * Boot info
 *
 * 0x90000 = framebuffer
 * 0x90004 = pitch
 * 0x90008 = bpp
 * ------------------------------------------------------------ */

static volatile uint32_t framebuffer = 0;
static volatile uint32_t framebuffer_pitch = 0;
static volatile uint32_t framebuffer_bpp = 0;

extern volatile char key_buffer[128];
extern volatile int kb_head;
extern volatile int kb_tail;

static int WIDTH = 800;
static int HEIGHT = 600;


#define FS_MAX_ENTRIES 128
#define FS_DIR  2

#define TASKBAR_H 42

struct fs_entry {
    char name[32];
    unsigned int size;
    unsigned int start_sector;
    unsigned int sector_count;
    unsigned int parent;
    unsigned char type;
    unsigned char used;
    unsigned char reserved[18];
};

extern int fs_read_entry(int index, struct fs_entry *entry);

void term_print_line(const char* str, uint32_t color);

void fs_list_term(unsigned int dir_idx) {
    struct fs_entry entry;
    int found = 0;

    for (int i = 0; i < FS_MAX_ENTRIES; i++) {
        if (!fs_read_entry(i, &entry))
            break;

        if (!entry.used)
            continue;

        if (entry.parent != dir_idx)
            continue;

        found = 1;

        if (entry.type == FS_DIR) {
            term_print_line(entry.name, 0x00FFFF00); 
        } else {
            term_print_line(entry.name, 0x00FFFFFF);
        }
    }

    if (!found) {
        term_print_line("(empty directory)", 0x00808080);
    }
}


/* ============================================================
 * BACKGROUND BUFFER
 * ============================================================ */

uint32_t *desktop_buffer = (uint32_t *)0x100000;

static void __attribute__((unused)) save_desktop_buffer(void)
{
    for (int y = 0; y < HEIGHT; y++) {
        volatile uint32_t *src =
            (volatile uint32_t *)(
                framebuffer +
                ((uint32_t)y * framebuffer_pitch)
            );

        uint32_t *dst =
            &desktop_buffer[y * WIDTH];

        for (int x = 0; x < WIDTH; x++)
            dst[x] = src[x];
    }
}


static void __attribute__((unused)) restore_desktop_region(
    int x,
    int y,
    int w,
    int h
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

    if (x + w > WIDTH)
        w = WIDTH - x;

    if (y + h > HEIGHT)
        h = HEIGHT - y;

    if (w <= 0 || h <= 0)
        return;

    for (int yy = 0; yy < h; yy++) {

        volatile uint32_t *dst =
            (volatile uint32_t *)(
                framebuffer +
                ((uint32_t)(y + yy) * framebuffer_pitch) +
                ((uint32_t)x * 4)
            );

        uint32_t *src =
            &desktop_buffer[(y + yy) * WIDTH + x];

        for (int xx = 0; xx < w; xx++)
            dst[xx] = src[xx];
    }
}


/* ============================================================
 * PORTAS
 * ============================================================ */

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}


/* ============================================================
 * VBE
 * ============================================================ */

static void framebuffer_init(void)
{
    volatile uint32_t *info =
        (volatile uint32_t *)BOOT_INFO_ADDR;

    framebuffer      = info[0];
    framebuffer_pitch = info[1];
    framebuffer_bpp   = info[2];


    if (framebuffer == 0)
        return;

    if (framebuffer_pitch == 0)
        framebuffer_pitch = WIDTH * 4;

    if (framebuffer_bpp != 32)
        framebuffer_bpp = 32;
}

#define BLACK        0x00000000
#define WHITE        0x00FFFFFF

#define GRAY         0x00808080
#define LIGHT_GRAY   0x00C0C0C0
#define DARK_GRAY    0x00404040

#define BLUE         0x000080FF
#define DARK_BLUE    0x00002060

#define CYAN         0x0000FFFF
#define GREEN        0x0000FF00
#define RED          0x00FF0000
#define YELLOW       0x00FFFF00
#define PURPLE       0x00FF00FF

#define ORANGE       0x00FF8800

#define DESKTOP_TOP      0x00102030
#define DESKTOP_BOTTOM   0x001A2D42

#define TASKBAR_COLOR    0x00101018
#define TASKBAR_BORDER   0x00304050

#define WINDOW_COLOR     0x001A1A22
#define WINDOW_BORDER    0x00506070

#define TITLE_COLOR      0x00253040
#define TITLE_ACTIVE     0x000060A0


/* ============================================================
 * PIXEL
 * ============================================================ */

static void putpixel(
    int x,
    int y,
    uint32_t color
)
{
    if (x < 0 || x >= WIDTH)
        return;

    if (y < 0 || y >= HEIGHT)
        return;

    volatile uint32_t *pixel =
        (volatile uint32_t *)
        (framebuffer +
         ((uint32_t)y * framebuffer_pitch) +
         ((uint32_t)x * 4));

    *pixel = color;
}


/* ============================================================
 * RETÂNGULO
 * ============================================================ */

static void fill_rect(
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

    if (x + w > WIDTH)
        w = WIDTH - x;

    if (y + h > HEIGHT)
        h = HEIGHT - y;

    if (w <= 0 || h <= 0)
        return;

    for (int yy = 0; yy < h; yy++) {
        volatile uint32_t *dst =
            (volatile uint32_t *)(
                framebuffer +
                ((uint32_t)(y + yy) * framebuffer_pitch) +
                ((uint32_t)x * 4)
            );

        for (int xx = 0; xx < w; xx++)
            dst[xx] = color;
    }
}


static void rect(
    int x,
    int y,
    int w,
    int h,
    uint32_t color
)
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


/* ============================================================
 * LINHAS
 * ============================================================ */

static void hline(
    int x,
    int y,
    int w,
    uint32_t color
)
{
    for (int i = 0; i < w; i++)
        putpixel(x + i, y, color);
}


static void vline(
    int x,
    int y,
    int h,
    uint32_t color
)
{
    for (int i = 0; i < h; i++)
        putpixel(x, y + i, color);
}


/* ============================================================
 * FONTE 5x7
 * ============================================================ */

static const uint8_t font5x7[96][7] = {

    /* space */
    {0,0,0,0,0,0,0},

    /* ! */
    {4,4,4,4,4,0,4},

    /* " */
    {10,10,0,0,0,0,0},

    /* # */
    {10,31,10,10,31,10,0},

    /* $ */
    {4,15,20,14,5,30,4},

    /* % */
    {24,25,2,4,8,19,3},

    /* & */
    {12,18,20,8,21,18,13},

    /* ' */
    {4,4,0,0,0,0,0},

    /* ( */
    {2,4,8,8,8,4,2},

    /* ) */
    {8,4,2,2,2,4,8},

    /* * */
    {0,4,21,14,21,4,0},

    /* + */
    {0,4,4,31,4,4,0},

    /* , */
    {0,0,0,0,4,4,8},

    /* - */
    {0,0,0,31,0,0,0},

    /* . */
    {0,0,0,0,0,6,6},

    /* / */
    {1,2,4,8,16,0,0},

    /* 0 */
    {14,17,19,21,25,17,14},

    /* 1 */
    {4,12,4,4,4,4,14},

    /* 2 */
    {14,17,1,2,4,8,31},

    /* 3 */
    {30,1,1,14,1,1,30},

    /* 4 */
    {2,6,10,18,31,2,2},

    /* 5 */
    {31,16,16,30,1,1,30},

    /* 6 */
    {14,16,16,30,17,17,14},

    /* 7 */
    {31,1,2,4,8,8,8},

    /* 8 */
    {14,17,17,14,17,17,14},

    /* 9 */
    {14,17,17,15,1,1,14},

    /* : */
    {0,6,6,0,6,6,0},

    /* ; */
    {0,6,6,0,6,6,12},

    /* < */
    {2,4,8,16,8,4,2},

    /* = */
    {0,0,31,0,31,0,0},

    /* > */
    {8,4,2,1,2,4,8},

    /* ? */
    {14,17,1,2,4,0,4},

    /* @ */
    {14,17,1,13,21,21,14},

    /* A */
    {14,17,17,31,17,17,17},

    /* B */
    {30,17,17,30,17,17,30},

    /* C */
    {14,17,16,16,16,17,14},

    /* D */
    {30,17,17,17,17,17,30},

    /* E */
    {31,16,16,30,16,16,31},

    /* F */
    {31,16,16,30,16,16,16},

    /* G */
    {14,17,16,23,17,17,14},

    /* H */
    {17,17,17,31,17,17,17},

    /* I */
    {14,4,4,4,4,4,14},

    /* J */
    {7,2,2,2,2,18,12},

    /* K */
    {17,18,20,24,20,18,17},

    /* L */
    {16,16,16,16,16,16,31},

    /* M */
    {17,27,21,21,17,17,17},

    /* N */
    {17,25,21,19,17,17,17},

    /* O */
    {14,17,17,17,17,17,14},

    /* P */
    {30,17,17,30,16,16,16},

    /* Q */
    {14,17,17,17,21,18,13},

    /* R */
    {30,17,17,30,20,18,17},

    /* S */
    {15,16,16,14,1,1,30},

    /* T */
    {31,4,4,4,4,4,4},

    /* U */
    {17,17,17,17,17,17,14},

    /* V */
    {17,17,17,17,17,10,4},

    /* W */
    {17,17,17,21,21,21,10},

    /* X */
    {17,17,10,4,10,17,17},

    /* Y */
    {17,17,10,4,4,4,4},

    /* Z */
    {31,1,2,4,8,16,31},

    /* [ */
    {14,8,8,8,8,8,14},

    /* \ */
    {16,8,4,2,1,0,0},

    /* ] */
    {14,2,2,2,2,2,14},

    /* ^ */
    {4,10,17,0,0,0,0},

    /* _ */
    {0,0,0,0,0,0,31},

    /* ` */
    {8,4,2,0,0,0,0},

    /* a */
    {0,0,14,1,15,17,15},

    /* b */
    {16,16,22,25,17,17,30},

    /* c */
    {0,0,14,17,16,17,14},

    /* d */
    {1,1,13,19,17,17,15},

    /* e */
    {0,0,14,17,31,16,14},

    /* f */
    {6,9,8,28,8,8,8},

    /* g */
    {0,0,15,17,15,1,14},

    /* h */
    {16,16,22,25,17,17,17},

    /* i */
    {4,0,12,4,4,4,14},

    /* j */
    {2,0,6,2,2,18,12},

    /* k */
    {16,16,18,20,24,20,18},

    /* l */
    {12,4,4,4,4,4,14},

    /* m */
    {0,0,26,21,21,21,21},

    /* n */
    {0,0,22,25,17,17,17},

    /* o */
    {0,0,14,17,17,17,14},

    /* p */
    {0,0,30,17,30,16,16},

    /* q */
    {0,0,15,17,15,1,1},

    /* r */
    {0,0,22,25,16,16,16},

    /* s */
    {0,0,15,16,14,1,30},

    /* t */
    {8,8,28,8,8,9,6},

    /* u */
    {0,0,17,17,17,19,13},

    /* v */
    {0,0,17,17,17,10,4},

    /* w */
    {0,0,17,17,21,21,10},

    /* x */
    {0,0,17,10,4,10,17},

    /* y */
    {0,0,17,17,15,1,14},

    /* z */
    {0,0,31,2,4,8,31},

    /* { */
    {2,4,4,8,4,4,2},

    /* | */
    {4,4,4,4,4,4,4},

    /* } */
    {8,4,4,2,4,4,8},

    /* ~ */
    {8,21,2,0,0,0,0}
};


/* ============================================================
 * CARACTERE
 * ============================================================ */

static void draw_char(
    int x,
    int y,
    unsigned char c,
    uint32_t color
)
{
    if (c < 32 || c > 127)
        c = '?';

    const uint8_t *glyph = font5x7[c - 32];

    for (int row = 0; row < 7; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 5; col++) {
            if (bits & (1 << (4 - col))) {
                int px = x + col;
                int py = y + row;
                if (px >= 0 && px < 1024 && py >= 0 && py < 768) {
                    putpixel(px, py, color);
                }
            }
        }
    }
}


/* ============================================================
 * TEXTO
 * ============================================================ */

static void draw_string(
    int x,
    int y,
    const char *str,
    uint32_t color
)
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


/* ============================================================
 * TEXTO CENTRALIZADO
 * ============================================================ */

static void draw_centered(
    int x,
    int y,
    int width,
    const char *str,
    uint32_t color
)
{
    int len = 0;

    const char *p = str;

    while (*p++) {
        len++;
    }

    int text_width = len * 6;

    int text_x =
        x + ((width - text_width) / 2);

    draw_string(
        text_x,
        y,
        str,
        color
    );
}


/* ============================================================
 * MOUSE PS/2
 * ============================================================ */

static int mouse_x = 400;
static int mouse_y = 300;

static int mouse_left = 0;
static int mouse_right = 0;

static int mouse_packet_index = 0;

static int8_t mouse_packet[3];


static void mouse_wait(uint8_t type)
{
    int timeout = 100000;

    if (type == 0) {

        while (timeout--) {

            if ((inb(0x64) & 1) != 0)
                return;
        }

    } else {

        while (timeout--) {

            if ((inb(0x64) & 2) == 0)
                return;
        }
    }
}


static void mouse_write(uint8_t value)
{
    mouse_wait(1);

    outb(0x64, 0xD4);

    mouse_wait(1);

    outb(0x60, value);
}


static uint8_t mouse_read(void)
{
    mouse_wait(0);

    return inb(0x60);
}


static void mouse_init(void)
{
    mouse_wait(1);

    outb(0x64, 0xA8);

    mouse_wait(1);

    outb(0x64, 0x20);

    mouse_wait(0);

    uint8_t status = inb(0x60);

    status |= 2;

    mouse_wait(1);

    outb(0x64, 0x60);

    mouse_wait(1);

    outb(0x60, status);

    mouse_write(0xF6);

    mouse_read();

    mouse_write(0xF4);

    mouse_read();

    mouse_x = WIDTH / 2;
    mouse_y = HEIGHT / 2;
}


static void mouse_poll(void)
{
    while (inb(0x64) & 1) {
        uint8_t status = inb(0x64);

        if (!(status & 0x20)) {
            (void)inb(0x60);
            continue;
        }

        uint8_t data = inb(0x60);

        if (mouse_packet_index == 0) {
            if (!(data & 0x08))
                continue;
        }

        mouse_packet[mouse_packet_index++] = (int8_t)data;

        if (mouse_packet_index < 3)
            continue;

        mouse_packet_index = 0;

        int8_t dx = mouse_packet[1];
        int8_t dy = mouse_packet[2];

        mouse_left  = mouse_packet[0] & 1;
        mouse_right = mouse_packet[0] & 2;

        mouse_x += dx;
        mouse_y -= dy;

        if (mouse_x < 0)
            mouse_x = 0;

        if (mouse_y < 0)
            mouse_y = 0;

        if (mouse_x >= WIDTH)
            mouse_x = WIDTH - 1;

        if (mouse_y >= HEIGHT)
            mouse_y = HEIGHT - 1;
    }
}


/* ============================================================
 * CURSOR
 * ============================================================ */

#define CURSOR_W 10
#define CURSOR_H 16

static uint32_t cursor_background[CURSOR_W * CURSOR_H];

static int cursor_saved_x = 0;
static int cursor_saved_y = 0;

static void save_cursor_background(void)
{
    cursor_saved_x = mouse_x;
    cursor_saved_y = mouse_y;

    for (int y = 0; y < CURSOR_H; y++) {
        for (int x = 0; x < CURSOR_W; x++) {

            int px = cursor_saved_x + x;
            int py = cursor_saved_y + y;

            if (px >= 0 && px < WIDTH &&
                py >= 0 && py < HEIGHT) {

                volatile uint32_t *pixel =
                    (volatile uint32_t *)
                    (framebuffer +
                     py * framebuffer_pitch +
                     px * 4);

                cursor_background[y * CURSOR_W + x] = *pixel;
            } else {
                cursor_background[y * CURSOR_W + x] = 0;
            }
        }
    }
}

static void restore_cursor_background(void)
{
    for (int y = 0; y < CURSOR_H; y++) {
        for (int x = 0; x < CURSOR_W; x++) {

            int px = cursor_saved_x + x;
            int py = cursor_saved_y + y;

            if (px >= 0 && px < WIDTH &&
                py >= 0 && py < HEIGHT) {

                volatile uint32_t *pixel =
                    (volatile uint32_t *)
                    (framebuffer +
                     py * framebuffer_pitch +
                     px * 4);

                *pixel = cursor_background[y * CURSOR_W + x];
            }
        }
    }
}

static void draw_mouse_cursor(void)
{
    static const uint16_t cursor[16] = {
        0b1000000000,
        0b1100000000,
        0b1110000000,
        0b1111000000,
        0b1111100000,
        0b1111110000,
        0b1111111000,
        0b1111111100,
        0b1111111110,
        0b1111110000,
        0b1100110000,
        0b1000011000,
        0b0000001100,
        0b0000001100,
        0b0000000000,
        0b0000000000
    };

    for (int y = 0; y < CURSOR_H; y++) {
        for (int x = 0; x < CURSOR_W; x++) {

            if (cursor[y] & (1 << (CURSOR_W - 1 - x))) {
                putpixel(
                    mouse_x + x,
                    mouse_y + y,
                    0x00FFFFFF
                );
            }
        }
    }
}


/* ============================================================
 * WALLPAPER
 * ============================================================ */

static void draw_wallpaper(void)
{

    for (int y = 0; y < HEIGHT; y++) {

        uint32_t r =
            0x10 + ((uint32_t)y * 0x0A / HEIGHT);

        uint32_t g =
            0x20 + ((uint32_t)y * 0x0D / HEIGHT);

        uint32_t b =
            0x30 + ((uint32_t)y * 0x12 / HEIGHT);

        uint32_t color =
            (r << 16) |
            (g << 8) |
            b;

        hline(
            0,
            y,
            WIDTH,
            color
        );
    }

    fill_rect(
        WIDTH - 260,
        70,
        180,
        180,
        0x001A3448
    );

    fill_rect(
        WIDTH - 220,
        110,
        180,
        180,
        0x00172C3D
    );

    fill_rect(
        WIDTH - 180,
        150,
        180,
        180,
        0x00142636
    );
}


/* ============================================================
 * ÍCONE
 * ============================================================ */

static void draw_icon(
    int x,
    int y,
    const char *label,
    uint32_t color
)
{

    fill_rect(
        x,
        y,
        48,
        48,
        0x00203040
    );

    rect(
        x,
        y,
        48,
        48,
        color
    );

    fill_rect(
        x + 10,
        y + 10,
        28,
        20,
        color
    );

    draw_centered(
        x - 20,
        y + 55,
        88,
        label,
        WHITE
    );
}

#define ICON_BMP_MAX_SIZE 4096

static unsigned char icon_bmp[ICON_BMP_MAX_SIZE];
extern unsigned int current_dir;

extern int fs_write_entry(int index, struct fs_entry *entry);
extern int fs_find(const char *name, unsigned int parent);
extern int fs_find_free_entry(void);
extern unsigned int fs_find_free_data_sector(void);

extern unsigned char fs_sector[512];
extern unsigned char fs_bitmap[512];

extern int ata_read_sector(unsigned int sector, unsigned char *buffer);
extern int ata_write_sector(unsigned int sector, unsigned char *buffer);

static int load_icon_bmp(
    const char *name
)
{
    int idx = fs_find(name, 0);

    if (idx == -1)
        return 0;

    struct fs_entry entry;

    if (!fs_read_entry(idx, &entry))
        return 0;

    if (entry.type == FS_DIR)
        return 0;

    if (entry.size == 0)
        return 0;

    if (entry.size > ICON_BMP_MAX_SIZE)
        return 0;

    unsigned int sectors =
        (entry.size + 511) / 512;

    for (unsigned int i = 0; i < sectors; i++)
    {
        if (!ata_read_sector(
                entry.start_sector + i,
                fs_sector))
        {
            return 0;
        }

        unsigned int remaining =
            entry.size - i * 512;

        unsigned int copy_size =
            remaining > 512 ? 512 : remaining;

        for (unsigned int j = 0; j < copy_size; j++)
        {
            icon_bmp[i * 512 + j] =
                fs_sector[j];
        }
    }

    return 1;
}

#define FS_SECTOR_SIZE 512
#define FS_ENTRY_SIZE 64
#define FS_MAX_ENTRIES 128

#define FS_START_SECTOR  74
#define FS_ENTRY_SECTOR  75
#define FS_BITMAP_SECTOR 91
#define FS_DATA_SECTOR   92

static unsigned char bmp_buffer[4096];

static unsigned int bmp_u16(const unsigned char *p)
{
    return (unsigned int)p[0] |
           ((unsigned int)p[1] << 8);
}

static unsigned int bmp_u32(const unsigned char *p)
{
    return (unsigned int)p[0] |
           ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) |
           ((unsigned int)p[3] << 24);
}

static void draw_bmp_icon(
    int x,
    int y,
    const char *name,
    int size
)
{
    unsigned char entry_sector[512];

    unsigned int start_sector = 0;
    int found = 0;

    for (unsigned int s = 0; s < 16 && !found; s++)
    {
        if (!ata_read_sector(
                FS_ENTRY_SECTOR + s,
                entry_sector))
            return;

        for (unsigned int e = 0; e < 8; e++)
        {
            unsigned int off = e * 64;

            int same = 1;

            for (unsigned int i = 0; i < 32; i++)
            {
                if (entry_sector[off + i] !=
                    (unsigned char)name[i])
                {
                    same = 0;
                    break;
                }

                if (name[i] == '\0')
                    break;
            }

            if (!same)
                continue;

            start_sector =
                (unsigned int)entry_sector[off + 36] |
                ((unsigned int)entry_sector[off + 37] << 8) |
                ((unsigned int)entry_sector[off + 38] << 16) |
                ((unsigned int)entry_sector[off + 39] << 24);

            found = 1;
            break;
        }
    }

    if (!found)
        return;

    /*
     * Carrega os 3 setores do BMP.
     */
    for (unsigned int i = 0; i < 3; i++)
    {
        if (!ata_read_sector(
                start_sector + i,
                fs_sector))
            return;

        for (unsigned int j = 0; j < 512; j++)
            bmp_buffer[i * 512 + j] = fs_sector[j];
    }

    /*
     * Verifica BMP.
     */
    if (bmp_buffer[0] != 'B' ||
        bmp_buffer[1] != 'M')
        return;

    unsigned int pixel_offset =
        bmp_u32(&bmp_buffer[10]);

    int width =
        (int)bmp_u32(&bmp_buffer[18]);

    int height =
        (int)bmp_u32(&bmp_buffer[22]);

    unsigned int bpp =
        bmp_u16(&bmp_buffer[28]);

    unsigned int compression =
        bmp_u32(&bmp_buffer[30]);

    unsigned int colors_used =
        bmp_u32(&bmp_buffer[46]);

    if (width != 16 ||
        height == 0 ||
        bpp != 8 ||
        compression != 0)
        return;

    unsigned int palette_count =
        colors_used;

    if (palette_count == 0)
        palette_count = 256;

    if (palette_count > 256)
        return;

    unsigned int palette_offset = 54;

    unsigned int row_size =
        ((unsigned int)width + 3) & ~3u;

    int bottom_up = 1;

    if (height < 0)
    {
        height = -height;
        bottom_up = 0;
    }

    /*
     * Calcula o tamanho de cada pixel
     * na tela.
     *
     * BMP = 16x16
     *
     * size 16 -> 1x1
     * size 32 -> 2x2
     * size 48 -> 3x3
     * size 64 -> 4x4
     */
    int scale = size / width;

    if (scale < 1)
        scale = 1;

    for (int py = 0; py < height; py++)
    {
        int bmp_y =
            bottom_up ? height - 1 - py : py;

        unsigned int row =
            pixel_offset +
            (unsigned int)bmp_y * row_size;

        for (int px = 0; px < width; px++)
        {
            unsigned int index =
                bmp_buffer[row + px];

            if (index >= palette_count)
                continue;

            unsigned int palette_pos =
                palette_offset + index * 4;

            unsigned int b =
                bmp_buffer[palette_pos];

            unsigned int g =
                bmp_buffer[palette_pos + 1];

            unsigned int r =
                bmp_buffer[palette_pos + 2];

            /*
             * Preto = transparente.
             */
            if (r == 0 &&
                g == 0 &&
                b == 0)
                continue;

            unsigned int color =
                (r << 16) |
                (g << 8) |
                b;

            fill_rect(
                x + px * scale,
                y + py * scale,
                scale,
                scale,
                color
            );
        }
    }
}

/* ============================================================
 * JANELAS
 * ============================================================ */

#define WINDOW_FILES 0
#define WINDOW_TERM  1
#define WINDOW_COUNT 2

typedef struct {
    int x;
    int y;
    int w;
    int h;

    const char *title;

    int open;
    int active;

    int dragging;

    int drag_offset_x;
    int drag_offset_y;

    int minimized;
    int maximized;

    int old_x;
    int old_y;
    int old_w;
    int old_h;
} Window;

static Window windows[WINDOW_COUNT] = {
    {
        90, 170,
        300, 230,
        "Files",
        0, 0, 0,
        0, 0,
        0, 0,
        0, 0, 0, 0
    },

    {
        270, 150,
        430, 260,
        "Terminal",
        0, 0, 0,
        0, 0,
        0, 0,
        0, 0, 0, 0
    }
};

static int window_z[WINDOW_COUNT] = {
    WINDOW_FILES,
    WINDOW_TERM
};

static int active_window = -1;

static void draw_window(
    int x,
    int y,
    int w,
    int h,
    const char *title,
    int active
)
{

    fill_rect(
        x + 6,
        y + 6,
        w,
        h,
        0x00000000
    );


    fill_rect(
        x,
        y,
        w,
        h,
        WINDOW_COLOR
    );


    rect(
        x,
        y,
        w,
        h,
        active
            ? TITLE_ACTIVE
            : WINDOW_BORDER
    );


    fill_rect(
        x + 1,
        y + 1,
        w - 2,
        24,
        active
            ? TITLE_ACTIVE
            : TITLE_COLOR
    );

    draw_string(
        x + 8,
        y + 9,
        title,
        WHITE
    );

    fill_rect(
        x + w - 65,
        y + 6,
        12,
        12,
        BLUE
    );

    hline(
        x + w - 62,
        y + 11,
        6,
        WHITE
    );

    fill_rect(
        x + w - 43,
        y + 6,
        12,
        12,
        BLUE
    );

    rect(
        x + w - 40,
        y + 9,
        6,
        6,
        WHITE
    );


    fill_rect(
        x + w - 21,
        y + 6,
        12,
        12,
        RED
    );

    draw_string(
        x + w - 19,
        y + 8,
        "x",
        WHITE
    );
}


/* ============================================================
 * TERMINAL
 * ============================================================ */

#define FS_FILE 1
#define FS_DIR  2

extern volatile char key_buffer[128];

extern unsigned int get_used_ram_kb(void);
extern unsigned int get_total_ram_kb(void);
extern unsigned int get_total_ssd_kb(void);

struct idt_entry {
    uint16_t base_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t flags;
    uint16_t base_high;
} __attribute__((packed));

extern struct idt_entry idt[256];

/* ============================================================
 * UTILITÁRIOS
 * ============================================================ */

static void shell_print(const char *str, uint32_t color)
{
    term_print_line(str, color);
}

static int shell_strncmp(
    const char *a,
    const char *b,
    int n
)
{
    for (int i = 0; i < n; i++)
    {
        if (a[i] != b[i])
            return 1;

        if (a[i] == '\0')
            return 0;
    }

    return 0;
}

static void shell_trim_trailing_spaces(char *str)
{
    int len = 0;

    while (str[len] != '\0')
        len++;

    while (len > 0 &&
           (str[len - 1] == ' ' ||
            str[len - 1] == '\t'))
    {
        str[len - 1] = '\0';
        len--;
    }
}

static void shell_print_number(
    int n,
    char *buffer,
    int *pos
)
{
    if (n == 0)
    {
        buffer[(*pos)++] = '0';
        return;
    }

    if (n < 0)
    {
        buffer[(*pos)++] = '-';
        n = -n;
    }

    char temp[16];
    int i = 0;

    while (n > 0 && i < 15)
    {
        temp[i++] = '0' + (n % 10);
        n /= 10;
    }

    while (i > 0)
        buffer[(*pos)++] = temp[--i];
}

static void shell_print_two_numbers(
    const char *prefix,
    int a,
    const char *middle,
    int b,
    int pct
)
{
    char line[96];
    int p = 0;

    while (*prefix && p < 90)
        line[p++] = *prefix++;

    shell_print_number(a, line, &p);

    while (*middle && p < 90)
        line[p++] = *middle++;

    shell_print_number(b, line, &p);

    if (pct >= 0)
    {
        const char *s = " (";

        while (*s && p < 90)
            line[p++] = *s++;

        shell_print_number(pct, line, &p);

        s = "% used)";

        while (*s && p < 90)
            line[p++] = *s++;
    }

    line[p] = '\0';

    shell_print(line, WHITE);
}


/* ============================================================
 * INPUT DO TERMINAL
 * ============================================================ */

static char term_getchar(void);

static void shell_read_line(
    char *buffer,
    int max
)
{
    int len = 0;

    while (1)
    {
        char c = term_getchar();

        if (!c)
            continue;

        if (c == '\n')
        {
            buffer[len] = '\0';
            return;
        }

        if (c == '\b')
        {
            if (len > 0)
            {
                len--;
                buffer[len] = '\0';
            }

            continue;
        }

        if (c >= 32 &&
            c <= 126 &&
            len < max - 1)
        {
            buffer[len++] = c;
            buffer[len] = '\0';
        }
    }
}


/* ============================================================
 * LIST
 * ============================================================ */

static void shell_cmd_list(unsigned int parent)
{
    struct fs_entry entry;
    int found = 0;

    for (int i = 0; i < FS_MAX_ENTRIES; i++)
    {
        if (!fs_read_entry(i, &entry))
            break;

        if (!entry.used)
            continue;

        if (entry.parent != parent)
            continue;

        found = 1;

        char line[64];
        int p = 0;

        if (entry.type == FS_DIR)
        {
            line[p++] = '[';
            line[p++] = 'D';
            line[p++] = 'I';
            line[p++] = 'R';
            line[p++] = ']';
            line[p++] = ' ';
            line[p++] = ' ';
        }
        else
        {
            line[p++] = '[';
            line[p++] = 'F';
            line[p++] = 'I';
            line[p++] = 'L';
            line[p++] = 'E';
            line[p++] = ']';
            line[p++] = ' ';
        }

        for (int j = 0;
             j < 32 && entry.name[j];
             j++)
        {
            if (p < 62)
                line[p++] = entry.name[j];
        }

        if (entry.type == FS_DIR &&
            p < 63)
        {
            line[p++] = '/';
        }

        line[p] = '\0';

        shell_print(
            line,
            entry.type == FS_DIR
                ? 0x00FFFF00
                : 0x00FFFFFF
        );
    }

    if (!found)
        shell_print("(empty directory)", GRAY);
}


/* ============================================================
 * WHICHDIR
 * ============================================================ */

static void shell_cmd_whichdir(void)
{
    if (current_dir == 0)
    {
        shell_print("/", CYAN);
        return;
    }

    struct fs_entry entry;

    if (fs_read_entry(current_dir, &entry))
    {
        char path[64];
        int p = 0;

        path[p++] = '/';

        for (int i = 0;
             i < 32 && entry.name[i];
             i++)
        {
            if (p < 62)
                path[p++] = entry.name[i];
        }

        path[p] = '\0';

        shell_print(path, CYAN);
    }
    else
    {
        shell_print("/unknown", CYAN);
    }
}


/* ============================================================
 * MKDIR
 * ============================================================ */

static void shell_cmd_mkdir(const char *name)
{
    if (fs_find(name, current_dir) != -1)
    {
        shell_print(
            "Error: Folder already exists.",
            RED
        );
        return;
    }

    int idx = fs_find_free_entry();

    if (idx == -1)
    {
        shell_print(
            "Error: NyteFS full (no available entries)",
            RED
        );
        return;
    }

    struct fs_entry entry;

    for (int i = 0; i < 32; i++)
        entry.name[i] = 0;

    int i = 0;

    while (name[i] && i < 31)
    {
        entry.name[i] = name[i];
        i++;
    }

    entry.name[i] = '\0';
    entry.size = 0;
    entry.start_sector = 0;
    entry.sector_count = 0;
    entry.parent = current_dir;
    entry.type = FS_DIR;
    entry.used = 1;

    if (fs_write_entry(idx, &entry))
    {
        shell_print(
            "Diretoria criada com sucesso.",
            GREEN
        );
    }
    else
    {
        shell_print(
            "Erro ao gravar no disco.",
            RED
        );
    }
}


/* ============================================================
 * CAT
 * ============================================================ */

static void shell_cmd_cat(const char *name)
{
    int idx = fs_find(name, current_dir);

    if (idx == -1)
    {
        shell_print("File not found.", RED);
        return;
    }

    struct fs_entry entry;

    fs_read_entry(idx, &entry);

    if (entry.type == FS_DIR)
    {
        shell_print(
            "Error: Target is a directory.",
            RED
        );
        return;
    }

    if (entry.size == 0 ||
        entry.start_sector == 0)
    {
        shell_print("[Empty file]", GRAY);
        return;
    }

    if (!ata_read_sector(
            entry.start_sector,
            fs_sector))
    {
        shell_print(
            "Error reading file.",
            RED
        );
        return;
    }

    char line[65];
    int p = 0;

    for (unsigned int i = 0;
         i < entry.size && i < 512;
         i++)
    {
        char c = fs_sector[i];

        if (c == '\n' || p >= 63)
        {
            line[p] = '\0';
            shell_print(line, WHITE);
            p = 0;

            if (c == '\n')
                continue;
        }

        line[p++] = c;
    }

    if (p > 0)
    {
        line[p] = '\0';
        shell_print(line, WHITE);
    }
}


/* ============================================================
 * REM
 * ============================================================ */

static void shell_cmd_rem(const char *name)
{
    int idx = fs_find(name, current_dir);

    if (idx == -1)
    {
        shell_print(
            "File or folder not found.",
            RED
        );
        return;
    }

    struct fs_entry entry;

    fs_read_entry(idx, &entry);

    entry.used = 0;

    if (fs_write_entry(idx, &entry))
        shell_print("Done.", GREEN);
}


/* ============================================================
 * WRITE
 * ============================================================ */

static void shell_cmd_write(const char *name)
{
    int idx = fs_find(name, current_dir);
    struct fs_entry entry;

    if (idx == -1)
    {
        idx = fs_find_free_entry();

        if (idx == -1)
        {
            shell_print(
                "Error: No space for more entries.",
                RED
            );
            return;
        }

        for (int i = 0; i < 32; i++)
            entry.name[i] = 0;

        int i = 0;

        while (name[i] && i < 31)
        {
            entry.name[i] = name[i];
            i++;
        }

        entry.name[i] = '\0';
        entry.parent = current_dir;
        entry.type = FS_FILE;
        entry.used = 1;
        entry.start_sector =
            fs_find_free_data_sector();
        entry.sector_count = 1;
    }
    else
    {
        fs_read_entry(idx, &entry);

        if (entry.type == FS_DIR)
        {
            shell_print(
                "Error: Target is a directory.",
                RED
            );
            return;
        }

        if (entry.start_sector == 0)
        {
            entry.start_sector =
                fs_find_free_data_sector();
        }
    }

    if (entry.start_sector == (unsigned int)-1)
    {
        shell_print(
            "Error: No free data sector.",
            RED
        );
        return;
    }

    shell_print(
        "Type the file content and press ENTER:",
        WHITE
    );

    char buffer[512];

    shell_read_line(
        buffer,
        sizeof(buffer)
    );

    for (int i = 0; i < 512; i++)
        fs_sector[i] =
            (i < 511) ? buffer[i] : 0;

    int len = 0;

    while (buffer[len] &&
           len < 511)
        len++;

    entry.size = len;

    if (ata_write_sector(
            entry.start_sector,
            fs_sector) &&
        fs_write_entry(idx, &entry))
    {
        shell_print(
            "Ficheiro guardado com sucesso!",
            GREEN
        );
    }
    else
    {
        shell_print(
            "Erro ao guardar ficheiro.",
            RED
        );
    }
}


/* ============================================================
 * EDIT.
 * ============================================================ */

static void shell_cmd_edit(const char *name)
{
    int idx = fs_find(name, current_dir);

    struct fs_entry entry;

    int is_new = 0;

    if (idx == -1)
    {
        idx = fs_find_free_entry();

        if (idx == -1)
        {
            shell_print(
                "Error: No space for more entries.",
                RED
            );
            return;
        }

        is_new = 1;

        for (int i = 0; i < 32; i++)
            entry.name[i] = 0;

        int i = 0;

        while (name[i] && i < 31)
        {
            entry.name[i] = name[i];
            i++;
        }

        entry.name[i] = '\0';
        entry.parent = current_dir;
        entry.type = FS_FILE;
        entry.used = 1;
        entry.start_sector =
            fs_find_free_data_sector();
        entry.sector_count = 1;
        entry.size = 0;
    }
    else
    {
        fs_read_entry(idx, &entry);

        if (entry.type == FS_DIR)
        {
            shell_print(
                "Error: Cannot edit a directory.",
                RED
            );
            return;
        }

        if (entry.start_sector == 0)
        {
            entry.start_sector =
                fs_find_free_data_sector();
        }
    }

    if (entry.start_sector == (unsigned int)-1)
    {
        shell_print(
            "Error: No free data sector.",
            RED
        );
        return;
    }

    shell_print(
        "=== NyteOS Editor ===",
        CYAN
    );

    shell_print(
        "ESC = save and exit",
        GRAY
    );

    char buffer[512];

    for (int i = 0; i < 512; i++)
        buffer[i] = 0;

    int len = 0;

    if (!is_new &&
        entry.size > 0 &&
        entry.size < 512)
    {
        if (ata_read_sector(
                entry.start_sector,
                fs_sector))
        {
            for (unsigned int i = 0;
                 i < entry.size;
                 i++)
            {
                buffer[i] = fs_sector[i];
            }

            len = entry.size;
        }
    }

    while (1)
    {
        char c = term_getchar();

        if (!c)
            continue;

        if (c == 27)
            break;

        if (c == '\b')
        {
            if (len > 0)
                len--;

            continue;
        }

        if (len < 511)
            buffer[len++] = c;
    }

    for (int i = 0; i < 512; i++)
        fs_sector[i] =
            (i < len) ? buffer[i] : 0;

    entry.size = len;

    if (ata_write_sector(
            entry.start_sector,
            fs_sector) &&
        fs_write_entry(idx, &entry))
    {
        shell_print(
            "File saved succesfully.",
            GREEN
        );
    }
    else
    {
        shell_print(
            "Error when saving to disk.",
            RED
        );
    }
}


/* ============================================================
 * STATUS
 * ============================================================ */

static void shell_cmd_status(void)
{

    int used_sectors = 0;
    unsigned char sector_buffer[512];

    for (unsigned int s = 1; s < FS_START_SECTOR; s++)
    {
        if (ata_read_sector(s, sector_buffer))
        {
            int has_data = 0;

            for (int i = 0; i < 512; i++)
            {
                if (sector_buffer[i] != 0)
                {
                    has_data = 1;
                    break;
                }
            }

            if (has_data)
                used_sectors++;
        }
    }

    used_sectors += 2;

    if (ata_read_sector(FS_BITMAP_SECTOR, fs_bitmap))
    {
        for (int i = 0; i < 512; i++)
        {
            if (fs_bitmap[i] != 0)
                used_sectors++;
        }
    }

    int total_bytes_ssd = used_sectors * 512;
    int used_ssd_kb = (total_bytes_ssd + 1023) / 1024;

    unsigned int total_ssd_kb = get_total_ssd_kb();

    if (used_ssd_kb > (int)total_ssd_kb)
        used_ssd_kb = total_ssd_kb;

    int ssd_pct =
        (total_ssd_kb > 0)
        ? (used_ssd_kb * 100) / total_ssd_kb
        : 0;

    unsigned int used_ram_kb =
        get_used_ram_kb();

    unsigned int total_ram_kb = get_total_ram_kb();

    if (total_ram_kb == 0)
        total_ram_kb = 640;

    if (used_ram_kb > total_ram_kb)
        used_ram_kb = total_ram_kb;

    int ram_pct =
        (total_ram_kb > 0)
        ? (used_ram_kb * 100) / total_ram_kb
        : 0;


    term_print_line(
        "=== NyteOS Monitor ===",
        WHITE
    );

    shell_print_two_numbers(
        "SSD: ",
        used_ssd_kb,
        " KB / ",
        total_ssd_kb,
        ssd_pct
    );

    shell_print_two_numbers(
        "RAM: ",
        used_ram_kb,
        " KB / ",
        total_ram_kb,
        ram_pct
    );
}


/* ============================================================
 * GETCHAR
 * ============================================================ */

static inline char term_getchar(void)
{
    if (kb_head == kb_tail)
        return 0;

    char c = key_buffer[kb_tail];

    kb_tail++;

    if (kb_tail >= 128)
        kb_tail = 0;

    return c;
}


/* ============================================================
 * TERMINAL BUFFER
 * ============================================================ */

#define TERM_MAX_LINES 18
#define TERM_LINE_LEN  64

static char term_lines[TERM_MAX_LINES][TERM_LINE_LEN];
static uint32_t term_colors[TERM_MAX_LINES];
static int term_line_count = 0;

static char term_cmd_buffer[128];
static int term_cmd_len = 0;


/* ============================================================
 * TERM PRINT
 * ============================================================ */

void term_print_line(
    const char *str,
    uint32_t color
)
{
    if (term_line_count >= TERM_MAX_LINES)
    {
        for (int i = 1;
             i < TERM_MAX_LINES;
             i++)
        {
            for (int j = 0;
                 j < TERM_LINE_LEN;
                 j++)
            {
                term_lines[i - 1][j] =
                    term_lines[i][j];
            }

            term_colors[i - 1] =
                term_colors[i];
        }

        term_line_count =
            TERM_MAX_LINES - 1;
    }

    int i = 0;

    while (str[i] &&
           i < TERM_LINE_LEN - 1)
    {
        term_lines[term_line_count][i] =
            str[i];

        i++;
    }

    term_lines[term_line_count][i] =
        '\0';

    term_colors[term_line_count] =
        color;

    term_line_count++;
}


/* ============================================================
 * STRCMP
 * ============================================================ */

static int term_strcmp(
    const char *a,
    const char *b
)
{
    while (*a && *b)
    {
        if (*a != *b)
            return 1;

        a++;
        b++;
    }

    return *a != *b;
}


/* ============================================================
 * EXECUTE COMMAND
 * ============================================================ */

static void term_execute_command(
    char *command
)
{
    shell_trim_trailing_spaces(command);

    if (term_strcmp(command, "help") == 0)
    {
        shell_print("Commands:", WHITE);
        shell_print("  help", WHITE);
        shell_print("  clear", WHITE);
        shell_print("  about", WHITE);
        shell_print("  version", WHITE);
        shell_print("  list", WHITE);
        shell_print("  dir <directory>", WHITE);
        shell_print("  whichdir", WHITE);
        shell_print("  echo <text>", WHITE);
        shell_print("  mkdir <folder-name>", WHITE);
        shell_print("  edit <file-name>", WHITE);
        shell_print("  write <file-name>", WHITE);
        shell_print("  cat <file-name>", WHITE);
        shell_print("  rem <file-or-folder-name>", WHITE);
        shell_print("  status", WHITE);
        shell_print("  reboot", WHITE);
    }

    else if (term_strcmp(command, "clear") == 0)
    {
        term_line_count = 0;

        for (int i = 0;
             i < TERM_MAX_LINES;
             i++)
        {
            term_lines[i][0] = '\0';
            term_colors[i] = WHITE;
        }
    }

    else if (term_strcmp(command, "about") == 0)
    {
        shell_print("NyteOS v0.2", WHITE);
        shell_print(
            "32-bit Operating System.",
            WHITE
        );
        shell_print(
            "Filesystem: NyteFS",
            WHITE
        );
    }

    else if (term_strcmp(command, "version") == 0)
    {
        shell_print(
            "NyteOS v0.2",
            WHITE
        );
    }

    else if (term_strcmp(command, "whichdir") == 0)
    {
        shell_cmd_whichdir();
    }

    else if (term_strcmp(command, "list") == 0)
    {
        shell_cmd_list(current_dir);
    }

    else if (shell_strncmp(command, "list ", 5) == 0)
    {
        char *target = command + 5;

        while (*target == ' ')
            target++;

        if (*target == '/' &&
            *(target + 1) != '\0')
        {
            target++;
        }

        if (*target == '\0' ||
            term_strcmp(target, "/") == 0)
        {
            shell_cmd_list(0);
        }
        else
        {
            char clean_target[32];
            int i = 0;

            while (target[i] != '\0' &&
                   target[i] != '/' &&
                   i < 31)
            {
                clean_target[i] =
                    target[i];

                i++;
            }

            clean_target[i] = '\0';

            int idx =
                fs_find(
                    clean_target,
                    current_dir
                );

            if (idx != -1)
            {
                struct fs_entry entry;

                fs_read_entry(
                    idx,
                    &entry
                );

                if (entry.type == FS_DIR)
                {
                    shell_cmd_list(idx);
                }
                else
                {
                    shell_print(
                        "Error: Target is a file, not a directory.",
                        RED
                    );
                }
            }
            else
            {
                shell_print(
                    "Directory not found.",
                    RED
                );
            }
        }
    }

    else if (term_strcmp(command, "status") == 0)
    {
        shell_cmd_status();
    }

    else if (term_strcmp(command, "dir") == 0)
    {
        shell_print(
            "Use: dir <folder>",
            WHITE
        );
    }

    else if (shell_strncmp(command, "dir ", 4) == 0)
    {
        char *target = command + 4;

        while (*target == ' ')
            target++;

        if (*target == '\0')
        {
            shell_print(
                "Use: dir <folder>",
                WHITE
            );
        }
        else if (term_strcmp(target, "/") == 0)
        {
            current_dir = 0;

            shell_print(
                "Switched to /",
                GREEN
            );
        }
        else
        {
            int idx =
                fs_find(
                    target,
                    current_dir
                );

            if (idx != -1)
            {
                struct fs_entry entry;

                fs_read_entry(
                    idx,
                    &entry
                );

                if (entry.type == FS_DIR)
                {
                    current_dir = idx;

                    shell_print(
                        "Changing directory to: ",
                        WHITE
                    );

                    shell_print(
                        target,
                        WHITE
                    );
                }
                else
                {
                    shell_print(
                        "Error: Target is a file, not a directory.",
                        RED
                    );
                }
            }
            else
            {
                shell_print(
                    "Directory not found.",
                    RED
                );
            }
        }
    }

    else if (term_strcmp(command, "mkdir") == 0)
    {
        shell_print(
            "Use: mkdir <folder-name>",
            WHITE
        );
    }

    else if (shell_strncmp(command, "mkdir ", 6) == 0)
    {
        char *target = command + 6;

        while (*target == ' ')
            target++;

        if (*target == '\0')
        {
            shell_print(
                "Use: mkdir <folder-name>",
                WHITE
            );
        }
        else
        {
            shell_cmd_mkdir(target);
        }
    }

    else if (term_strcmp(command, "cat") == 0)
    {
        shell_print(
            "Use: cat <file-name>",
            WHITE
        );
    }

    else if (shell_strncmp(command, "cat ", 4) == 0)
    {
        char *target = command + 4;

        while (*target == ' ')
            target++;

        if (*target == '\0')
        {
            shell_print(
                "Use: cat <file-name>",
                WHITE
            );
        }
        else
        {
            shell_cmd_cat(target);
        }
    }

    else if (term_strcmp(command, "rem") == 0)
    {
        shell_print(
            "Use: rem <file-or-folder-name>",
            WHITE
        );
    }

    else if (shell_strncmp(command, "rem ", 4) == 0)
    {
        char *target = command + 4;

        while (*target == ' ')
            target++;

        if (*target == '\0')
        {
            shell_print(
                "Use: rem <file-or-folder-name>",
                WHITE
            );
        }
        else
        {
            shell_cmd_rem(target);
        }
    }

    else if (term_strcmp(command, "write") == 0)
    {
        shell_print(
            "Use: write <file-name>",
            WHITE
        );
    }

    else if (shell_strncmp(command, "write ", 6) == 0)
    {
        char *target = command + 6;

        while (*target == ' ')
            target++;

        if (*target == '\0')
        {
            shell_print(
                "Use: write <file-name>",
                WHITE
            );
        }
        else
        {
            shell_cmd_write(target);
        }
    }

    else if (term_strcmp(command, "edit") == 0)
    {
        shell_print(
            "Use: edit <file-name>",
            WHITE
        );
    }

    else if (shell_strncmp(command, "edit ", 5) == 0)
    {
        char *target = command + 5;

        while (*target == ' ')
            target++;

        if (*target == '\0')
        {
            shell_print(
                "Use: edit <file-name>",
                WHITE
            );
        }
        else
        {
            shell_cmd_edit(target);
        }
    }

    else if (term_strcmp(command, "echo") == 0)
    {
        shell_print("", WHITE);
    }

    else if (shell_strncmp(command, "echo ", 5) == 0)
    {
        shell_print(
            command + 5,
            WHITE
        );
    }

    else if (term_strcmp(command, "reboot") == 0)
    {
        while (inb(0x64) & 2)
            ;

        outb(0x64, 0xFE);

        while (1)
            __asm__ volatile("hlt");
    }

    else if (command[0] != '\0')
{
    shell_print(
        "command not found",
        RED
    );
}
}

static void draw_terminal(void)
{
    Window *win = &windows[WINDOW_TERM];
    if (!win->open) return;

    draw_window(win->x, win->y, win->w, win->h, win->title, win->active);

    fill_rect(win->x + 2, win->y + 26, win->w - 4, win->h - 28, 0x000F0F14);

    int start_y = win->y + 32;
    for (int i = 0; i < term_line_count; i++) {
        draw_string(win->x + 10, start_y + (i * 12), term_lines[i], term_colors[i]);
    }

    int current_y = start_y + (term_line_count * 12);
    if (current_y < win->y + win->h - 20) {
        char prompt[128];
        int p = 0;
        const char *base = "nyte:/";
        
        while (*base)
            prompt[p++] = *base++;

        if (current_dir != 0)
        {
            struct fs_entry entry;
            if (fs_read_entry(current_dir, &entry))
            {
                int i = 0;
                while (i < 32 && entry.name[i] != '\0')
                {
                    prompt[p++] = entry.name[i];
                    i++;
                }
            }
            else
            {
                prompt[p++] = '?';
            }
        }

        prompt[p++] = '>';
        prompt[p++] = ' ';
        prompt[p] = '\0';

        draw_string(win->x + 10, current_y, prompt, WHITE);
        
        int cmd_x = win->x + 10 + (p * 6);
        draw_string(cmd_x, current_y, term_cmd_buffer, WHITE);

        int cursor_x = cmd_x + (term_cmd_len * 6);
        fill_rect(cursor_x, current_y, 6, 9, WHITE);
    }
}


/* ============================================================
 * WINDOW MANAGER
 * ============================================================ */

static int point_in_window(
    Window *win,
    int x,
    int y
)
{
    return
        win->open &&
        x >= win->x &&
        x < win->x + win->w &&
        y >= win->y &&
        y < win->y + win->h;
}


static int point_in_titlebar(
    Window *win,
    int x,
    int y
)
{
    return
        point_in_window(win, x, y) &&
        y >= win->y &&
        y < win->y + 25;
}


static int point_in_minimize_button(Window *win, int x, int y)
{
    return (
        x >= win->x + win->w - 64 &&
        x <  win->x + win->w - 43 &&
        y >= win->y &&
        y <  win->y + 24
    );
}

static int point_in_maximize_button(Window *win, int x, int y)
{
    return (
        x >= win->x + win->w - 43 &&
        x <  win->x + win->w - 22 &&
        y >= win->y &&
        y <  win->y + 24
    );
}


static int point_in_close_button(
    Window *win,
    int x,
    int y
)
{
    if (!point_in_window(win, x, y))
        return 0;

    return
        x >= win->x + win->w - 21 &&
        x <  win->x + win->w - 9 &&
        y >= win->y + 6 &&
        y <  win->y + 18;
}


static void window_raise(int index)
{
    if (index < 0 || index >= WINDOW_COUNT)
        return;

    int pos = -1;

    for (int i = 0; i < WINDOW_COUNT; i++)
    {
        if (window_z[i] == index)
        {
            pos = i;
            break;
        }
    }

    if (pos < 0)
        return;

    for (int i = pos; i < WINDOW_COUNT - 1; i++)
        window_z[i] = window_z[i + 1];

    window_z[WINDOW_COUNT - 1] = index;

    active_window = index;

    for (int i = 0; i < WINDOW_COUNT; i++)
        windows[i].active = 0;

    windows[index].active = 1;
}


static void window_open(int index)
{
    if (index < 0 || index >= WINDOW_COUNT)
        return;

    windows[index].open = 1;
    windows[index].dragging = 0;

    window_raise(index);
}


static void window_close(int index)
{
    if (index < 0 || index >= WINDOW_COUNT)
        return;

    windows[index].open = 0;
    windows[index].active = 0;
    windows[index].dragging = 0;

    if (active_window == index)
        active_window = -1;
}


static int window_at(int x, int y)
{
    for (int z = WINDOW_COUNT - 1; z >= 0; z--) {

        int i = window_z[z];

        if (!windows[i].open)
            continue;

        if (windows[i].minimized)
            continue;

        if (point_in_window(
                &windows[i],
                x,
                y))
            return i;
    }

    return -1;
}

static void window_minimize(int index);
static void window_toggle_maximize(int index);
static int taskbar_window_at(int x, int y);

static void window_manager_mouse_down(void)
{

    int task = taskbar_window_at(mouse_x, mouse_y);

if (task >= 0) {
    windows[task].minimized = 0;
    window_raise(task);
    return;
}

    int index = window_at(mouse_x, mouse_y);

    if (index >= 0) {

        Window *win = &windows[index];

        window_raise(index);

        if (point_in_close_button(
                win,
                mouse_x,
                mouse_y)) {

            window_close(index);
            return;
        }

        if (point_in_minimize_button(
        win,
        mouse_x,
        mouse_y)) {

    window_minimize(index);
    return;
}

if (point_in_maximize_button(
        win,
        mouse_x,
        mouse_y)) {

    window_toggle_maximize(index);
    return;
}

        if (point_in_titlebar(
                win,
                mouse_x,
                mouse_y)) {

            win->dragging = 1;

            windows[index].active = 1;

            win->drag_offset_x =
                mouse_x - win->x;

            win->drag_offset_y =
                mouse_y - win->y;
        }

        return;
    }

    if (
        mouse_x >= 30 &&
        mouse_x < 78 &&
        mouse_y >= 40 &&
        mouse_y < 88
    ) {
        window_open(WINDOW_FILES);
        return;
    }

    if (
        mouse_x >= 30 &&
        mouse_x < 78 &&
        mouse_y >= 130 &&
        mouse_y < 178
    ) {
        window_open(WINDOW_TERM);
        return;
    }
}

static void draw_taskbar(void);
static void desktop_draw(void);

static void window_minimize(int index)
{
    if (index < 0 || index >= WINDOW_COUNT)
        return;

    windows[index].minimized = 1;
    windows[index].active = 0;

    if (active_window == index)
        active_window = -1;

    desktop_draw();
}


static void window_toggle_maximize(int index)
{
    if (index < 0 || index >= WINDOW_COUNT)
        return;

    Window *win = &windows[index];

    if (!win->maximized)
    {
        win->old_x = win->x;
        win->old_y = win->y;
        win->old_w = win->w;
        win->old_h = win->h;

        win->x = 0;
        win->y = 0;
        win->w = WIDTH;
        win->h = HEIGHT - 42;

        win->maximized = 1;
    }
    else
    {
        win->x = win->old_x;
        win->y = win->old_y;
        win->w = win->old_w;
        win->h = win->old_h;

        win->maximized = 0;
    }
}


static int taskbar_window_at(int x, int y)
{
    int bar_y = HEIGHT - TASKBAR_H;

    if (y < bar_y)
        return -1;

    int task_x = 104;

    for (int i = 0; i < WINDOW_COUNT; i++) {

        if (!windows[i].open)
            continue;

        if (
            x >= task_x &&
            x < task_x + 140
        ) {
            return i;
        }

        task_x += 146;
    }

    return -1;
}


static void window_manager_mouse_up(void)
{
    for (int i = 0; i < WINDOW_COUNT; i++)
        windows[i].dragging = 0;
}


static void window_manager_drag(void)
{
    if (active_window < 0)
        return;

    Window *win = &windows[active_window];

    if (!win->dragging)
        return;

    win->x =
        mouse_x - win->drag_offset_x;

    win->y =
        mouse_y - win->drag_offset_y;

    if (win->x < 0)
        win->x = 0;

    if (win->y < 0)
        win->y = 0;

    if (win->x + win->w > WIDTH)
        win->x = WIDTH - win->w;

    if (win->y + win->h > HEIGHT - 42)
        win->y = HEIGHT - 42 - win->h;
}


/* ============================================================
 * FILE MANAGER
 * ============================================================ */

static void draw_files(void)
{
    Window *win = &windows[WINDOW_FILES];

    if (!win->open)
        return;

    draw_window(
        win->x,
        win->y,
        win->w,
        win->h,
        win->title,
        win->active
    );

    draw_string(
        win->x + 22,
        win->y + 94,
        "user",
        WHITE
    );

    draw_bmp_icon(
        win->x + 10,
        win->y + 46,
        "files.bmp",
        48
    );

    draw_string(
        win->x + 20,
        win->y + 135,
        "NyteFS",
        CYAN
    );

    draw_string(
        win->x + 20,
        win->y + 155,
        "1 folder(s)",
        GRAY
    );
}


/* ============================================================
 * RELÓGIO RTC
 * ============================================================ */

static int clock_last_minute = -1;

static void draw_clock(void)
{
    rtc_time_t now;

    rtc_get_time(&now);

    char clock[6];

    clock[0] = '0' + (now.hour / 10);
    clock[1] = '0' + (now.hour % 10);
    clock[2] = ':';
    clock[3] = '0' + (now.minute / 10);
    clock[4] = '0' + (now.minute % 10);
    clock[5] = '\0';

    draw_string(
        WIDTH - 54,
        HEIGHT - 25,
        clock,
        WHITE
    );
}


/* ============================================================
 * TASKBAR
 * ============================================================ */

static void draw_taskbar(void)
{
    int bar_height = 42;
    int y = HEIGHT - bar_height;

    fill_rect(
        0,
        y,
        WIDTH,
        bar_height,
        TASKBAR_COLOR
    );

    hline(
        0,
        y,
        WIDTH,
        TASKBAR_BORDER
    );

    draw_bmp_icon(
        8,
        y + 7,
        "nyteos.bmp",
        32
    );

    vline(
        50,
        y + 7,
        28,
        TASKBAR_BORDER
    );

    int task_x = 60;

    for (int i = 0; i < WINDOW_COUNT; i++)
    {
        if (!windows[i].open)
            continue;

        fill_rect(
            task_x,
            y + 7,
            140,
            28,
            windows[i].active
                ? DARK_BLUE
                : TASKBAR_COLOR
        );

        rect(
            task_x,
            y + 7,
            140,
            28,
            TASKBAR_BORDER
        );

        draw_string(
            task_x + 10,
            y + 17,
            windows[i].title,
            WHITE
        );

        task_x += 146;
    }

    draw_clock();
}


/* ============================================================
 * DESKTOP
 * ============================================================ */

static void desktop_draw(void)
{
    draw_wallpaper();

    draw_bmp_icon(
        30,
        40,
        "files.bmp",
        48
    );

    draw_centered(
        10,
        95,
        88,
        "FILES",
        WHITE
    );

    draw_bmp_icon(
        30,
        130,
        "term.bmp",
        48
    );

    draw_centered(
        10,
        185,
        88,
        "TERM",
        WHITE
    );

for (int z = 0; z < WINDOW_COUNT; z++)
{
    int i = window_z[z];

    if (!windows[i].open)
        continue;

    if (windows[i].minimized)
        continue;

    if (i == WINDOW_FILES)
        draw_files();

    if (i == WINDOW_TERM)
        draw_terminal();
}

    draw_taskbar();
}


/* ============================================================
 * ATUALIZAÇÃO
 * ============================================================ */

static void __attribute__((unused)) desktop_update(void)
{
    mouse_poll();
}


/* ============================================================
 * SHELL UI
 * ============================================================ */

static void shell_process_key(char key)
{
    if (key == '\n')
    {
        term_cmd_buffer[0] = '\0';
        term_cmd_len = 0;
        return;
    }

    if (key == '\b')
    {
        if (term_cmd_len > 0)
        {
            term_cmd_len--;
            term_cmd_buffer[term_cmd_len] = '\0';
        }
        return;
    }

    if (term_cmd_len < sizeof(term_cmd_buffer) - 1)
    {
        term_cmd_buffer[term_cmd_len++] = key;
        term_cmd_buffer[term_cmd_len] = '\0';
    }
}

extern volatile char pending_key;
extern volatile int key_pending;

void shell_ui(void)
{
    framebuffer_init();

    if (framebuffer == 0) {
        while (1)
            __asm__ volatile ("hlt");
    }

    mouse_init();

    __asm__ volatile ("sti");

    windows[WINDOW_FILES].open = 0;
    windows[WINDOW_TERM].open = 0;

    windows[WINDOW_FILES].active = 0;
    windows[WINDOW_TERM].active = 0;

    active_window = -1;

    desktop_draw();

    save_cursor_background();
    draw_mouse_cursor();

    int old_x = mouse_x;
    int old_y = mouse_y;

    int old_left = 0;

    while (1) {

if (key_pending)
{
    char key = pending_key;
    key_pending = 0;

    shell_process_key(key);
}
        mouse_poll();

        rtc_time_t rtc_now;

        rtc_get_time(&rtc_now);

        if (rtc_now.minute != clock_last_minute) {

            clock_last_minute = rtc_now.minute;

            fill_rect(
                WIDTH - 60,
                HEIGHT - 35,
                60,
                35,
                TASKBAR_COLOR
            );

            draw_clock();
        }

        if (mouse_left && !old_left) {

            int current_x = mouse_x;
            int current_y = mouse_y;

            mouse_x = old_x;
            mouse_y = old_y;
            restore_cursor_background();

            mouse_x = current_x;
            mouse_y = current_y;

            window_manager_mouse_down();

            desktop_draw();

            save_cursor_background();
            draw_mouse_cursor();

            old_x = mouse_x;
            old_y = mouse_y;
            old_left = mouse_left;

            continue;
        }

        if (mouse_left) {

            int before_x = windows[
                active_window >= 0
                    ? active_window
                    : 0
            ].x;

            int before_y = windows[
                active_window >= 0
                    ? active_window
                    : 0
            ].y;

            window_manager_drag();

            int moved_window = 0;

            if (active_window >= 0) {

                Window *win =
                    &windows[active_window];

                if (
                    win->x != before_x ||
                    win->y != before_y
                ) {
                    moved_window = 1;
                }
            }

            if (moved_window) {

                int current_x = mouse_x;
                int current_y = mouse_y;

                mouse_x = old_x;
                mouse_y = old_y;

                restore_cursor_background();

                mouse_x = current_x;
                mouse_y = current_y;

                desktop_draw();

                save_cursor_background();
                draw_mouse_cursor();

                old_x = mouse_x;
                old_y = mouse_y;

                continue;
            }
        }

        if (!mouse_left && old_left) {
            window_manager_mouse_up();
        }

        if (
            mouse_x != old_x ||
            mouse_y != old_y
        ) {

            int current_x = mouse_x;
            int current_y = mouse_y;

            mouse_x = old_x;
            mouse_y = old_y;

            restore_cursor_background();

            mouse_x = current_x;
            mouse_y = current_y;

            save_cursor_background();
            draw_mouse_cursor();

            old_x = mouse_x;
            old_y = mouse_y;
        }

        old_left = mouse_left;
    }
}