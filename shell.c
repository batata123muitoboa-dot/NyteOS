#include <stdint.h>
#include "rtc.h"
#include "speaker.h"
#include "graphics.h"

#define BOOT_INFO_ADDR 0x8800

/* ------------------------------------------------------------
 * Boot info
 *
 * 0x90000 = framebuffer
 * 0x90004 = pitch
 * 0x90008 = bpp
 * ------------------------------------------------------------ */

uint32_t *backbuffer = (uint32_t *)0x300000;

static volatile uint32_t framebuffer = 0;
static volatile uint32_t framebuffer_pitch = 0;
static volatile uint32_t framebuffer_bpp = 0;

extern volatile char key_buffer[128];
extern volatile int kb_head;
extern volatile int kb_tail;

extern int keyboard_pop(char *key);

extern int ata_read_sector(unsigned int lba, unsigned char *buffer);

extern void cmd_write(const char *name);
extern void cmd_edit(const char *name);

extern void software3d_draw_content(int x, int y, int w, int h);

extern void software3d_mouse_down(
    int win_x, int win_y,
    int mouse_x, int mouse_y,
    int win_w, int win_h
);

extern int software3d_mouse_move(
    int mouse_x, int mouse_y, int left_pressed
);

extern void software3d_mouse_up(void);

extern int software3d_open(void);
extern void software3d_close(void);

extern unsigned int software3d_get_memory_kb(void);

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

static int files_creating = 0;
static char files_new_name[32];
static int files_new_name_len = 0;

extern int fs_find(const char *name, int parent);
extern int fs_find_free_entry(void);
extern int fs_find_free_data_sector(void);
extern int fs_write_entry(int index, struct fs_entry *entry);

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

static void swap_buffers(void) {
    for (int y = 0; y < HEIGHT; y++) {
        volatile uint32_t *dst = (volatile uint32_t *)(framebuffer + ((uint32_t)y * framebuffer_pitch));
        uint32_t *src = &backbuffer[y * WIDTH];
        for (int x = 0; x < WIDTH; x++) {
            dst[x] = src[x];
        }
    }
}

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

#define TASKBAR_COLOR    0x002A2927
#define TASKBAR_BORDER   0x00454542

#define WINDOW_COLOR     0x001A1A22
#define WINDOW_BORDER    0x00506070

#define TITLE_COLOR      0x00253040
#define TITLE_ACTIVE     0x000060A0

/* ============================================================
 * EXCEPTION HANDLER
 * ============================================================ */

void nyteos_bsod(uint32_t vector, uint32_t error)
{
    (void)vector;
    (void)error;

    asm volatile("cli");

    framebuffer_init();

    if (framebuffer == 0)
        while (1) { asm volatile("hlt"); }

    fill_rect(0, 0, 800, 600, DARK_BLUE);

    const char *msg = "NyteOS has crashed and need to restart.";

    int len = 0;
    while (msg[len])
        len++;

    int x = (800 - len * 7) / 2;

    draw_string(x, 270, msg, WHITE, FONT_DEFAULT);

    draw_string(
        300,
        310,
        "System halted due to hardware exception.",
        WHITE,
        FONT_DEFAULT
    );

    while (1) {
        asm volatile("hlt");
    }
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

    mouse_write(0xF3); mouse_write(200); mouse_read();
    mouse_write(0xF3); mouse_write(100); mouse_read();
    mouse_write(0xF3); mouse_write(80);  mouse_read();

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

void save_cursor_background(void)
{
    for (int y = 0; y < CURSOR_H; y++)
    {
        for (int x = 0; x < CURSOR_W; x++)
        {
            int px = mouse_x + x;
            int py = mouse_y + y;

            if (px >= 0 && px < WIDTH && py >= 0 && py < HEIGHT)
            {
                cursor_background[y * CURSOR_W + x] = backbuffer[py * WIDTH + px];
            }
        }
    }
}

void restore_cursor_background(void)
{
    for (int y = 0; y < CURSOR_H; y++)
    {
        for (int x = 0; x < CURSOR_W; x++)
        {
            int px = mouse_x + x;
            int py = mouse_y + y;

            if (px >= 0 && px < WIDTH && py >= 0 && py < HEIGHT)
            {
                backbuffer[py * WIDTH + px] = cursor_background[y * CURSOR_W + x];
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

static void draw_bmp_icon(
    int x,
    int y,
    const char *name,
    int size
);

static const char *selected_wallpaper = "dwallpaper.bmp";

static void draw_wallpaper(void)
{

    draw_bmp_icon(
        0,
        0,
        selected_wallpaper,
        -3
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
        WHITE,
        FONT_DEFAULT
    );
}

#define ICON_BMP_MAX_SIZE 4096

static unsigned char icon_bmp[ICON_BMP_MAX_SIZE];
extern unsigned int current_dir;

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

#define FS_START_SECTOR   130
#define FS_ENTRY_SECTOR   131
#define FS_BITMAP_SECTOR  147
#define FS_DATA_SECTOR    148

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

    for (unsigned int i = 0; i < 3; i++)
    {
        if (!ata_read_sector(
                start_sector + i,
                fs_sector))
            return;

        for (unsigned int j = 0; j < 512; j++)
            bmp_buffer[i * 512 + j] = fs_sector[j];
    }

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

    int draw_width;
    int draw_height;
    int scale_x;
    int scale_y;

    if (size == -2)
    {
        draw_width = 800;
        draw_height = 600;

        scale_x = draw_width / width;
        scale_y = draw_height / height;

        if (scale_x < 1)
            scale_x = 1;

        if (scale_y < 1)
            scale_y = 1;
    }
    else if (size == -1)
    {
        int sx = 800 / width;
        int sy = 600 / height;

        int scale = sx < sy ? sx : sy;

        if (scale < 1)
            scale = 1;

        draw_width = width * scale;
        draw_height = height * scale;

        x = (800 - draw_width) / 2;
        y = (600 - draw_height) / 2;

        scale_x = scale;
        scale_y = scale;
    }
    else if (size == -3)
    {
        if ((800 * height) > (600 * width))
        {
            draw_width = 800;
            draw_height = (height * 800) / width;
        }
        else
        {
            draw_height = 600;
            draw_width = (width * 600) / height;
        }

        x = (800 - draw_width) / 2;
        y = (600 - draw_height) / 2;

        scale_x = 1;
        scale_y = 1;
    }
    else
    {
        if (size <= 0)
            return;

        draw_width = size;
        draw_height = size;

        scale_x = 1;
        scale_y = 1;
    }

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

            if (r == 0 &&
                g == 0 &&
                b == 0)
                continue;

            unsigned int color =
                (r << 16) |
                (g << 8) |
                b;

            int dx;
            int dy;
            int dw;
            int dh;

            if (size == -2 ||
                size == -3)
            {
                dx =
                    x + (px * draw_width) / width;

                dy =
                    y + (py * draw_height) / height;

                dw =
                    ((px + 1) * draw_width) / width
                    - (px * draw_width) / width;

                dh =
                    ((py + 1) * draw_height) / height
                    - (py * draw_height) / height;
            }
            else if (size > 0)
            {
                dx =
                    x + (px * draw_width) / width;

                dy =
                    y + (py * draw_height) / height;

                dw =
                    ((px + 1) * draw_width) / width
                    - (px * draw_width) / width;

                dh =
                    ((py + 1) * draw_height) / height
                    - (py * draw_height) / height;
            }
            else
            {
                dx = x + px * scale_x;
                dy = y + py * scale_y;
                dw = scale_x;
                dh = scale_y;
            }

            if (dw < 1)
                dw = 1;

            if (dh < 1)
                dh = 1;

            fill_rect(
                dx,
                dy,
                dw,
                dh,
                color
            );
        }
    }
}

/* ============================================================
 * JANELAS
 * ============================================================ */

#define WINDOW_FILES     0
#define WINDOW_TERM      1
#define WINDOW_SETTINGS  2
#define WINDOW_CALC      3
#define WINDOW_3D        4
#define WINDOW_COUNT     5

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
    { 90, 170, 300, 230, "Files", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 270, 150, 430, 260, "Terminal", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 180, 120, 440, 330, "Settings", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 200, 150, 195, 265, "Calculator", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 140, 65, 520, 420, "NyteOS 3D Renderer", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};

static int window_z[WINDOW_COUNT] = {
    WINDOW_FILES,
    WINDOW_TERM,
    WINDOW_SETTINGS,
    WINDOW_CALC,
    WINDOW_3D
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
    fill_rect_alpha(
        x + 6,
        y + 6,
        w,
        h,
        0x00000000,
        50
    );

    fill_rect(
        x,
        y + 25,
        w,
        h - 25,
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

    fill_rect_alpha(
        x + 1,
        y + 1,
        w - 2,
        24,
        active
            ? TITLE_ACTIVE
            : TITLE_COLOR,
        active ? 180 : 140
    );

    draw_string(
        x + 8,
        y + 9,
        title,
        WHITE,
        FONT_DEFAULT
    );

    fill_circle(
        x + w - 59,
        y + 12,
        6,
        YELLOW
    );

    hline(
        x + w - 62,
        y + 11,
        6,
        BLACK
    );

    fill_circle(
        x + w - 37,
        y + 12,
        6,
        GREEN
    );

    rect(
        x + w - 40,
        y + 9,
        6,
        6,
        BLACK
    );

    fill_circle(
        x + w - 15,
        y + 12,
        6,
        RED
    );

    draw_string(
        x + w - 17,
        y + 8,
        "x",
        WHITE,
        FONT_DEFAULT
    );
}


/* ============================================================
 * TERMINAL
 * ============================================================ */

#define FS_FILE 1
#define FS_DIR  2

#define TERM_HISTORY_MAX 20

static char term_history[TERM_HISTORY_MAX][128];
static int term_history_count = 0;
static int term_history_idx = -1;

static int term_scroll_offset = 0;

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

static void shell_cmd_write(const char *args)
{
    char filename[32];
    int i = 0;

    while (args[i] != '\0' && args[i] != ' ' && i < 31)
    {
        filename[i] = args[i];
        i++;
    }
    filename[i] = '\0';

    if (filename[0] == '\0')
    {
        shell_print("Use: write <file-name> <content>", WHITE);
        return;
    }

    const char *content = args + i;
    while (*content == ' ') content++;

    int idx = fs_find(filename, current_dir);
    struct fs_entry entry;

    if (idx == -1)
    {
        idx = fs_find_free_entry();
        if (idx == -1)
        {
            shell_print("Error: No space for more entries.", RED);
            return;
        }

        for (int j = 0; j < 32; j++) entry.name[j] = 0;
        int j = 0;
        while (filename[j] && j < 31) { entry.name[j] = filename[j]; j++; }

        entry.parent = current_dir;
        entry.type = FS_FILE;
        entry.used = 1;
        entry.start_sector = fs_find_free_data_sector();
        entry.sector_count = 1;
    }
    else
    {
        fs_read_entry(idx, &entry);
        if (entry.type == FS_DIR)
        {
            shell_print("Error: Target is a directory.", RED);
            return;
        }
        if (entry.start_sector == 0)
            entry.start_sector = fs_find_free_data_sector();
    }

    if (entry.start_sector == (unsigned int)-1)
    {
        shell_print("Error: No free data sector.", RED);
        return;
    }

    int len = 0;
    while (content[len] && len < 511) len++;

    for (int j = 0; j < 512; j++)
        fs_sector[j] = (j < len) ? content[j] : 0;

    entry.size = len;

    if (ata_write_sector(entry.start_sector, fs_sector) && fs_write_entry(idx, &entry))
    {
        shell_print("Wrote file sucessfully.", GREEN);
    }
    else
    {
        shell_print("Error when writing file.", RED);
    }
}


/* ============================================================
 * EDIT.
 * ============================================================ */

static char files_view_buffer[513];
static char files_editor_buffer[512];
static int files_editor_len = 0;
static int files_editor_cursor = 0;
static int files_editor_open = 0;
static int files_editor_entry = -1;

static void shell_cmd_edit(const char *name)
{
    int idx = fs_find(name, current_dir);
    struct fs_entry entry;

    if (idx == -1)
    {
        idx = fs_find_free_entry();
        if (idx == -1)
        {
            shell_print("Error: No space for more entries.", RED);
            return;
        }

        for (int i = 0; i < 32; i++) entry.name[i] = 0;
        int i = 0;
        while (name[i] && i < 31) { entry.name[i] = name[i]; i++; }

        entry.parent = current_dir;
        entry.type = FS_FILE;
        entry.used = 1;
        entry.start_sector = fs_find_free_data_sector();
        entry.sector_count = 1;
        entry.size = 0;

        fs_write_entry(idx, &entry);
    }
    else
    {
        fs_read_entry(idx, &entry);

        if (entry.type == FS_DIR)
        {
            shell_print("Error: Cannot edit a directory.", RED);
            return;
        }

        if (entry.start_sector == 0)
        {
            entry.start_sector = fs_find_free_data_sector();
            fs_write_entry(idx, &entry);
        }
    }

    if (entry.start_sector == (unsigned int)-1)
    {
        shell_print("Error: No free data sector.", RED);
        return;
    }

    files_editor_len = entry.size;
    if (files_editor_len > 511)
        files_editor_len = 511;

    if (entry.size > 0)
    {
        if (ata_read_sector(entry.start_sector, fs_sector))
        {
            for (int i = 0; i < files_editor_len; i++)
                files_editor_buffer[i] = fs_sector[i];
        }
    }

    files_editor_buffer[files_editor_len] = '\0';
    files_editor_cursor = files_editor_len;
    files_editor_entry = idx;
    files_editor_open = 1;

    shell_print("Opening editor window...", GREEN);
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
                if (sector_buffer[i] != 0) { has_data = 1; break; }
            }
            if (has_data) used_sectors++;
        }
    }

    used_sectors += 2;

    if (ata_read_sector(FS_BITMAP_SECTOR, fs_bitmap))
    {
        for (int i = 0; i < 512; i++)
        {
            if (fs_bitmap[i] != 0) used_sectors++;
        }
    }

    int total_bytes_ssd = used_sectors * 512;
    int used_ssd_kb = (total_bytes_ssd + 1023) / 1024;
    unsigned int total_ssd_kb = get_total_ssd_kb();

    if (used_ssd_kb > (int)total_ssd_kb) used_ssd_kb = total_ssd_kb;
    int ssd_pct = (total_ssd_kb > 0) ? (used_ssd_kb * 100) / total_ssd_kb : 0;

    unsigned int graphics_ram_kb = (800 * 600 * 4 * 2) / 1024;
    unsigned int used_ram_kb =
        get_used_ram_kb() +
        graphics_ram_kb +
        software3d_get_memory_kb();

    unsigned int total_ram_kb = get_total_ram_kb();
    if (total_ram_kb == 0) total_ram_kb = 640;
    if (used_ram_kb > total_ram_kb) used_ram_kb = total_ram_kb;

    int ram_pct = (total_ram_kb > 0) ? (used_ram_kb * 100) / total_ram_kb : 0;

    term_print_line("=== NyteOS Monitor ===", WHITE);

    shell_print_two_numbers("Storage: ", used_ssd_kb, " KB / ", total_ssd_kb, ssd_pct);
    shell_print_two_numbers("RAM: ", used_ram_kb, " KB / ", total_ram_kb, ram_pct);
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
        shell_print("NyteOS v0.5.1", WHITE);
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
            "NyteOS v0.5.1",
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
        speaker_beep(440, 180);
        speaker_beep(392, 150);
        speaker_beep(330, 180);

        speaker_beep(294, 220);
        speaker_beep(262, 180);
        speaker_beep(220, 250);

        speaker_beep(196, 220);
        speaker_beep(175, 180);
        speaker_beep(147, 450);
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

    int max_visible_lines = (win->h - 45) / 12;
    if (max_visible_lines <= 0) max_visible_lines = 1;

    int total_lines = term_line_count;
    
    int max_scroll = total_lines - max_visible_lines + 1;
    if (max_scroll < 0) max_scroll = 0;
    if (term_scroll_offset > max_scroll) term_scroll_offset = max_scroll;
    if (term_scroll_offset < 0) term_scroll_offset = 0;

    int start_index = total_lines - max_visible_lines + 1 - term_scroll_offset;
    if (start_index < 0) start_index = 0;

    int start_y = win->y + 32;
    int line_y = start_y;

    for (int i = start_index; i < total_lines && i < start_index + max_visible_lines; i++)
    {
        draw_string(win->x + 10, line_y, term_lines[i], term_colors[i], FONT_DEFAULT);
        line_y += 12;
    }

    if (term_scroll_offset == 0 && line_y < win->y + win->h - 20)
    {
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

        draw_string(win->x + 10, line_y, prompt, WHITE, FONT_DEFAULT);

        int cmd_x = win->x + 10 + (p * 6);
        draw_string(cmd_x, line_y, term_cmd_buffer, WHITE, FONT_DEFAULT);

        int cursor_x = cmd_x + (term_cmd_len * 6);
        fill_rect(cursor_x, line_y, 6, 9, WHITE);
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

    if (index == WINDOW_3D)
    {
        if (!software3d_open())
            return;
    }

    windows[index].open = 1;
    windows[index].dragging = 0;

    window_raise(index);
}


static void window_close(int index)
{
    if (index < 0 || index >= WINDOW_COUNT)
        return;

    if (index == WINDOW_3D)
        software3d_close();

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
static void draw_taskbar(void);
static void desktop_draw(void);
static void files_open_file(int index);

static int files_selected_entry = -1;

#define SETTINGS_MAIN       0
#define SETTINGS_DESKTOP    1
#define SETTINGS_ABOUT      2

static int settings_page = SETTINGS_MAIN;

static void files_delete_entry(int index);

static int files_context_menu = 0;
static int files_context_entry = -1;
static int files_context_x = 0;
static int files_context_y = 0;

static int files_editor_x = 120;
static int files_editor_y = 80;
static int files_editor_dragging = 0;
static int files_editor_drag_ox = 0;
static int files_editor_drag_oy = 0;

static int calc_value1 = 0;
static char calc_op = 0;
static char calc_display[16] = "0";
static int calc_new_number = 1;

static void calc_int_to_str(int n, char *buf);

static void window_manager_mouse_down(void)
{
    int bar_y = HEIGHT - 42;
    int total_width = WINDOW_COUNT * 42 - 6;
    int task_start = (WIDTH - total_width) / 2;

    int task = -1;

    if (mouse_y >= bar_y + 7 && mouse_y < bar_y + 35 &&
        mouse_x >= task_start && mouse_x < task_start + total_width)
    {
        int offset = mouse_x - task_start;
        int index = offset / 42;

        if (index >= 0 && index < WINDOW_COUNT && offset % 42 < 36)
            task = index;
    }

    if (task >= 0)
    {
        if (!windows[task].open)
        {
            window_open(task);
        }
        else if (windows[task].minimized)
        {
            windows[task].minimized = 0;
            window_raise(task);
        }
        else if (active_window == task)
        {
            window_minimize(task);
        }
        else
        {
            window_raise(task);
        }

        desktop_draw();
        return;
    }

    int index = window_at(mouse_x, mouse_y);

    if (index >= 0 && index < WINDOW_COUNT)
    {
        Window *win = &windows[index];

        window_raise(index);

        if (point_in_close_button(win, mouse_x, mouse_y))
        {
            window_close(index);
            return;
        }

        if (point_in_minimize_button(win, mouse_x, mouse_y))
        {
            window_minimize(index);
            return;
        }

        if (point_in_maximize_button(win, mouse_x, mouse_y))
        {
            window_toggle_maximize(index);
            return;
        }

        if (point_in_titlebar(win, mouse_x, mouse_y))
        {
            win->dragging = 1;
            win->active = 1;

            win->drag_offset_x = mouse_x - win->x;
            win->drag_offset_y = mouse_y - win->y;

            return;
        }

        if (index == WINDOW_SETTINGS)
        {
            if (settings_page == SETTINGS_MAIN)
            {
                if (mouse_x >= win->x + 10 && mouse_x < win->x + 220 &&
                    mouse_y >= win->y + 130 && mouse_y < win->y + 155)
                {
                    settings_page = SETTINGS_DESKTOP;
                    desktop_draw();
                    return;
                }

                if (mouse_x >= win->x + 10 && mouse_x < win->x + 220 &&
                    mouse_y >= win->y + 185 && mouse_y < win->y + 210)
                {
                    settings_page = SETTINGS_ABOUT;
                    desktop_draw();
                    return;
                }
            }
            else if (settings_page == SETTINGS_DESKTOP)
            {
                if (mouse_x >= win->x + 20 && mouse_x < win->x + 220 &&
                    mouse_y >= win->y + 85 && mouse_y < win->y + 185)
                {
                    selected_wallpaper = "dwallpaper.bmp";
                    desktop_draw();
                    return;
                }

                if (mouse_x >= win->x + 20 && mouse_x < win->x + 220 &&
                    mouse_y >= win->y + 205 && mouse_y < win->y + 285)
                {
                    selected_wallpaper = "wallpaper2.bmp";
                    desktop_draw();
                    return;
                }

                if (mouse_x >= win->x + 10 && mouse_x < win->x + 120 &&
                    mouse_y >= win->y + 287 && mouse_y < win->y + 340)
                {
                    settings_page = SETTINGS_MAIN;
                    desktop_draw();
                    return;
                }
            }
            else if (settings_page == SETTINGS_ABOUT)
            {
                if (mouse_x >= win->x + 10 && mouse_x < win->x + 120 &&
                    mouse_y >= win->y + 275 && mouse_y < win->y + 325)
                {
                    settings_page = SETTINGS_MAIN;
                    desktop_draw();
                    return;
                }
            }
        }

        if (mouse_right)
        {
            files_context_menu = 0;

            int idx = window_at(mouse_x, mouse_y);

            if (idx == WINDOW_FILES &&
                idx >= 0 && idx < WINDOW_COUNT)
            {
                Window *w = &windows[idx];

                int first_y = w->y + 60;

                if (current_dir != 0)
                    first_y += 24;

                if (mouse_x >= w->x + 10 &&
                    mouse_x < w->x + w->w - 10 &&
                    mouse_y >= first_y)
                {
                    int row = (mouse_y - first_y) / 24;
                    int visible_row = 0;

                    for (int i = 0; i < FS_MAX_ENTRIES; i++)
                    {
                        struct fs_entry entry;

                        if (!fs_read_entry(i, &entry))
                            continue;

                        if (!entry.used || i == 0)
                            continue;

                        if (entry.parent != current_dir)
                            continue;

                        if (visible_row == row)
                        {
                            if (entry.type == FS_FILE && entry.parent != 0)
                            {
                                files_context_entry = i;
                                files_context_x = mouse_x;
                                files_context_y = mouse_y;
                                files_context_menu = 1;
                            }

                            desktop_draw();
                            return;
                        }

                        visible_row++;
                    }
                }
            }

            desktop_draw();
            return;
        }

        if (index == WINDOW_3D)
{
    software3d_mouse_down(
        win->x,
        win->y,
        mouse_x,
        mouse_y,
        win->w,
        win->h
    );

    desktop_draw();
    return;
}

        if (files_context_menu)
        {
            if (mouse_x >= files_context_x &&
                mouse_x < files_context_x + 110 &&
                mouse_y >= files_context_y &&
                mouse_y < files_context_y + 35)
            {
                files_delete_entry(files_context_entry);
            }

            files_context_menu = 0;
            desktop_draw();
            return;
        }

        if (index == WINDOW_FILES)
        {
            if (mouse_x >= win->x + win->w - 110 &&
                mouse_x < win->x + win->w - 15 &&
                mouse_y >= win->y + 30 &&
                mouse_y < win->y + 55)
            {
                files_creating = 1;
                files_new_name_len = 0;
                files_new_name[0] = '\0';

                desktop_draw();
                return;
            }

            int first_y = win->y + 60;

            if (current_dir != 0)
            {
                if (mouse_x >= win->x + 10 &&
                    mouse_x < win->x + win->w - 10 &&
                    mouse_y >= first_y &&
                    mouse_y < first_y + 24)
                {
                    struct fs_entry dir;

                    if (fs_read_entry(current_dir, &dir))
                    {
                        current_dir = dir.parent;
                        files_selected_entry = -1;
                        desktop_draw();
                    }

                    return;
                }

                first_y += 24;
            }

            if (mouse_x >= win->x + 10 &&
                mouse_x < win->x + win->w - 10 &&
                mouse_y >= first_y)
            {
                int row = (mouse_y - first_y) / 24;
                int visible_row = 0;

                for (int i = 0; i < FS_MAX_ENTRIES; i++)
                {
                    struct fs_entry entry;

                    if (!fs_read_entry(i, &entry))
                        continue;

                    if (!entry.used || i == 0)
                        continue;

                    if (entry.parent != current_dir)
                        continue;

                    if (visible_row == row)
                    {
                        files_selected_entry = i;

                        if (entry.type == FS_DIR)
                        {
                            current_dir = i;
                            files_selected_entry = -1;
                            desktop_draw();
                        }
                        else
                        {
                            files_open_file(i);
                        }

                        return;
                    }

                    visible_row++;
                }
            }

            return;
        }

        if (index == WINDOW_CALC)
        {
            int col = (mouse_x - (win->x + 10)) / 45;
            int row = (mouse_y - (win->y + 75)) / 45;
            int dx = (mouse_x - (win->x + 10)) % 45;
            int dy = (mouse_y - (win->y + 75)) % 45;

            if (col >= 0 && col < 4 &&
                row >= 0 && row < 4 &&
                dx < 40 && dy < 40)
            {
                char keys[4][4] = {
                    {'7', '8', '9', '+'},
                    {'4', '5', '6', '-'},
                    {'1', '2', '3', '*'},
                    {'C', '0', '=', '/'}
                };

                char key = keys[row][col];

                if (key >= '0' && key <= '9')
                {
                    if (calc_new_number)
                    {
                        calc_display[0] = key;
                        calc_display[1] = '\0';
                        calc_new_number = 0;
                    }
                    else
                    {
                        int len = 0;

                        while (calc_display[len])
                            len++;

                        if (len < 9)
                        {
                            calc_display[len] = key;
                            calc_display[len + 1] = '\0';
                        }
                    }
                }
                else if (key == 'C')
                {
                    calc_display[0] = '0';
                    calc_display[1] = '\0';
                    calc_value1 = 0;
                    calc_op = 0;
                    calc_new_number = 1;
                }
                else if (key == '+' || key == '-' ||
                         key == '*' || key == '/')
                {
                    int val = 0;

                    for (int i = 0; calc_display[i]; i++)
                    {
                        if (calc_display[i] == '-')
                            continue;

                        val = val * 10 + (calc_display[i] - '0');
                    }

                    if (calc_display[0] == '-')
                        val = -val;

                    calc_value1 = val;
                    calc_op = key;
                    calc_new_number = 1;
                }
                else if (key == '=')
                {
                    int val = 0;

                    for (int i = 0; calc_display[i]; i++)
                    {
                        if (calc_display[i] == '-')
                            continue;

                        val = val * 10 + (calc_display[i] - '0');
                    }

                    if (calc_display[0] == '-')
                        val = -val;

                    int result = val;

                    if (calc_op == '+')
                        result = calc_value1 + val;

                    if (calc_op == '-')
                        result = calc_value1 - val;

                    if (calc_op == '*')
                        result = calc_value1 * val;

                    if (calc_op == '/')
                    {
                        if (val != 0)
                            result = calc_value1 / val;
                        else
                            result = 0;
                    }

                    calc_int_to_str(result, calc_display);
                    calc_new_number = 1;
                    calc_op = 0;
                }

                desktop_draw();
                return;
            }

            return;
        }

        if (files_editor_open)
        {
            int ew = 560;
            int eh = 400;

            if (mouse_x >= files_editor_x &&
                mouse_x < files_editor_x + ew &&
                mouse_y >= files_editor_y &&
                mouse_y < files_editor_y + eh)
            {
                if (mouse_x >= files_editor_x + ew - 21 &&
                    mouse_x < files_editor_x + ew - 9 &&
                    mouse_y >= files_editor_y + 6 &&
                    mouse_y < files_editor_y + 18)
                {
                    files_editor_open = 0;
                    files_editor_entry = -1;
                    desktop_draw();
                    return;
                }

                if (mouse_y >= files_editor_y &&
                    mouse_y < files_editor_y + 25)
                {
                    files_editor_dragging = 1;
                    files_editor_drag_ox = mouse_x - files_editor_x;
                    files_editor_drag_oy = mouse_y - files_editor_y;
                    return;
                }

                return;
            }
        }

        return;
    }
}

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

    int task_x = 60;

    for (int i = 0; i < WINDOW_COUNT; i++) {

        if (
            x >= task_x &&
            x < task_x + 36
        ) {
            return i;
        }

        task_x += 42;
    }

    return -1;
}


static void window_manager_mouse_up(void)
{
    for (int i = 0; i < WINDOW_COUNT; i++) {
        windows[i].dragging = 0;
    }

    files_editor_dragging = 0;
    software3d_mouse_up();
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

    if (files_editor_dragging)
    {
        files_editor_x = mouse_x - files_editor_drag_ox;
        files_editor_y = mouse_y - files_editor_drag_oy;

        if (files_editor_x < 0) files_editor_x = 0;
        if (files_editor_y < 0) files_editor_y = 0;
        if (files_editor_x + 560 > WIDTH) files_editor_x = WIDTH - 560;
        if (files_editor_y + 400 > HEIGHT - 42) files_editor_y = HEIGHT - 42 - 400;

        desktop_draw();
        return;
    }
}

/* ============================================================
 * CALCULATOR
 * ============================================================ */

static void calc_int_to_str(int n, char *buf) {
    if (n == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    int i = 0, is_neg = 0;
    if (n < 0) {
        is_neg = 1;
        n = -n;
    }
    while (n > 0 && i < 14) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    if (is_neg) buf[i++] = '-';
    buf[i] = '\0';
    
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = t;
    }
}

static void draw_calculator(void) {
    Window *win = &windows[WINDOW_CALC];
    if (!win->open) return;

    draw_window(win->x, win->y, win->w, win->h, win->title, win->active);

    fill_rect(win->x + 10, win->y + 35, 175, 30, 0x000F0F14);
    draw_string(win->x + 15, win->y + 46, calc_display, WHITE, FONT_DEFAULT);

    const char* labels[4][4] = {
        {"7", "8", "9", "+"},
        {"4", "5", "6", "-"},
        {"1", "2", "3", "*"},
        {"C", "0", "=", "/"}
    };

    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            int bx = win->x + 30 + (col * 45);
            int by = win->y + 95 + (row * 45);

            int cx = bx;
            int cy = by;

            fill_circle(cx, cy, 19, 0x00304050);
            draw_circle(cx, cy, 19, WINDOW_BORDER);

            draw_string(cx - 2, cy - 3, labels[row][col], WHITE, FONT_79);
        }
    }
}

/* ============================================================
 * FILE MANAGER
 * ============================================================ */

static void files_finish_create(void);

static void files_delete_entry(int index)
{
    if (index < 0)
        return;

    struct fs_entry entry;

    if (!fs_read_entry(index, &entry))
        return;

    if (!entry.used)
        return;

    if (entry.parent == 0)
        return;

    if (entry.type != FS_FILE)
        return;

    unsigned char bitmap[512];

    if (!ata_read_sector(FS_BITMAP_SECTOR, bitmap))
        return;

    for (unsigned int i = 0; i < entry.sector_count; i++)
    {
        unsigned int sector = entry.start_sector + i;

        if (sector >= FS_DATA_SECTOR)
            bitmap[sector - FS_DATA_SECTOR] = 0;
    }

    if (!ata_write_sector(FS_BITMAP_SECTOR, bitmap))
        return;

    entry.used = 0;

    fs_write_entry(index, &entry);

    files_selected_entry = -1;
}

static void draw_files_context_menu(void)
{
    if (!files_context_menu)
        return;

    fill_rect(
        files_context_x,
        files_context_y,
        110,
        35,
        DARK_BLUE
    );

    draw_string(
        files_context_x + 12,
        files_context_y + 10,
        "Delete",
        RED,
        FONT_DEFAULT
    );
}

static void files_editor_save(void)
{
    if (files_editor_entry < 0)
        return;

    struct fs_entry entry;

    if (!fs_read_entry(files_editor_entry, &entry))
        return;

    if (entry.parent == 0)
        return;

    unsigned char buffer[512];

    for (int i = 0; i < 512; i++)
        buffer[i] = 0;

    for (int i = 0; i < files_editor_len && i < 511; i++)
        buffer[i] = (unsigned char)files_editor_buffer[i];

    if (!ata_write_sector(entry.start_sector, buffer))
        return;

    entry.size = files_editor_len;
    entry.sector_count = 1;

    fs_write_entry(files_editor_entry, &entry);

    desktop_draw();
}

static void files_editor_key(char key)
{
    if (key == 19)
    {
        files_editor_save();
        return;
    }

    if (key == 27)
    {
        files_editor_open = 0;
        files_editor_entry = -1;
        desktop_draw();
        return;
    }

    if (key == '\b')
    {
        if (files_editor_cursor > 0)
        {
            files_editor_cursor--;

            for (int i = files_editor_cursor;
                 i < files_editor_len;
                 i++)
            {
                files_editor_buffer[i] =
                    files_editor_buffer[i + 1];
            }

            files_editor_len--;
            files_editor_buffer[files_editor_len] = '\0';

            desktop_draw();
        }

        return;
    }

    if (key >= 32 && key <= 126)
    {
        if (files_editor_len < 511)
        {
            for (int i = files_editor_len;
                 i > files_editor_cursor;
                 i--)
            {
                files_editor_buffer[i] =
                    files_editor_buffer[i - 1];
            }

            files_editor_buffer[files_editor_cursor] = key;

            files_editor_cursor++;
            files_editor_len++;

            files_editor_buffer[files_editor_len] = '\0';

            desktop_draw();
        }

        return;
    }
}

static void files_create_key(char key)
{
    if (key == 27)
    {
        files_creating = 0;
        files_new_name_len = 0;
        files_new_name[0] = '\0';
        desktop_draw();
        return;
    }

    if (key == '\b')
    {
        if (files_new_name_len > 0)
        {
            files_new_name_len--;
            files_new_name[files_new_name_len] = '\0';
            desktop_draw();
        }

        return;
    }

    if (key == '\r' || key == '\n')
    {
        files_finish_create();
        return;
    }

    if (key >= 32 && key <= 126)
    {
        if (files_new_name_len < 31)
        {
            files_new_name[files_new_name_len++] = key;
            files_new_name[files_new_name_len] = '\0';

            desktop_draw();
        }

        return;
    }
}

static void files_finish_create(void)
{

    if (current_dir == 0)
    {
        files_creating = 0;
        files_new_name_len = 0;
        files_new_name[0] = '\0';
        desktop_draw();
        return;
    }

    if (files_new_name_len == 0)
    {
        files_creating = 0;
        desktop_draw();
        return;
    }

    files_new_name[files_new_name_len] = '\0';

    if (fs_find(files_new_name, current_dir) >= 0)
    {
        files_creating = 0;
        desktop_draw();
        return;
    }

    int index = fs_find_free_entry();

    if (index < 0)
    {
        files_creating = 0;
        desktop_draw();
        return;
    }

    int sector = fs_find_free_data_sector();

    if (sector < 0)
    {
        files_creating = 0;
        desktop_draw();
        return;
    }

    struct fs_entry entry;

    for (int i = 0; i < 64; i++)
        ((unsigned char *)&entry)[i] = 0;

    for (int i = 0; i < files_new_name_len; i++)
        entry.name[i] = files_new_name[i];

    entry.size = 0;
    entry.start_sector = sector;
    entry.sector_count = 1;
    entry.parent = current_dir;
    entry.type = FS_FILE;
    entry.used = 1;

    if (!fs_write_entry(index, &entry))
    {
        files_creating = 0;
        desktop_draw();
        return;
    }

    files_creating = 0;
    files_new_name_len = 0;
    files_new_name[0] = '\0';

    desktop_draw();
}

static void files_open_file(int index)
{
    struct fs_entry entry;

    if (!fs_read_entry(index, &entry))
        return;

    if (entry.type == FS_DIR)
        return;

    if (files_editor_open)
        return;

    unsigned char sector[512];

    if (!ata_read_sector(entry.start_sector, sector))
        return;

    files_editor_len = entry.size;

    if (files_editor_len > 511)
        files_editor_len = 511;

    for (int i = 0; i < files_editor_len; i++)
        files_editor_buffer[i] = sector[i];

    files_editor_buffer[files_editor_len] = '\0';

    files_editor_cursor = files_editor_len;
    files_editor_entry = index;
    files_editor_open = 1;

    desktop_draw();
}

static void files_save_file(void)
{
    if (files_editor_entry < 0)
        return;

    struct fs_entry entry;

    if (!fs_read_entry(files_editor_entry, &entry))
        return;

    unsigned char sector[512];

    for (int i = 0; i < 512; i++)
        sector[i] = 0;

    for (int i = 0; i < files_editor_len; i++)
        sector[i] = files_editor_buffer[i];

    if (!ata_write_sector(entry.start_sector, sector))
        return;

    entry.size = files_editor_len;

    fs_write_entry(files_editor_entry, &entry);
}

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
    win->x + win->w - 100,
    win->y + 38,
    "+ FILE",
    GREEN,
    FONT_DEFAULT
);

if (files_creating)
{
    fill_rect(
        win->x + 30,
        win->y + 90,
        win->w - 60,
        55,
        BLACK
    );

    draw_string(
        win->x + 40,
        win->y + 102,
        "New file:",
        WHITE,
        FONT_DEFAULT
    );

    draw_string(
        win->x + 40,
        win->y + 122,
        files_new_name,
        WHITE,
        FONT_DEFAULT
    );
}

    int y = win->y + 60;

    if (current_dir != 0)
    {
        draw_string(win->x + 20, y, "..", YELLOW, FONT_DEFAULT);
        y += 24;
    }

    int folders = 0;
    int files = 0;

    for (int i = 0; i < FS_MAX_ENTRIES; i++)
    {
        struct fs_entry entry;

        if (!fs_read_entry(i, &entry))
            continue;

        if (!entry.used)
            continue;

        if (i == 0)
            continue;

        if (entry.parent != current_dir)
            continue;

        if (entry.type == FS_DIR)
        {
            draw_string(
                win->x + 20,
                y,
                "[DIR]",
                CYAN,
                FONT_DEFAULT
            );

            folders++;
        }
        else
        {
            draw_string(
                win->x + 20,
                y,
                "[FILE]",
                GRAY,
                FONT_DEFAULT
            );

            files++;
        }

        draw_string(
            win->x + 70,
            y,
            entry.name,
            WHITE,
            FONT_DEFAULT
        );

        y += 24;

        if (y > win->y + win->h - 45)
            break;
    }

    char info[32];
    int p = 0;

    info[p++] = '0' + folders;
    info[p++] = ' ';
    info[p++] = 'f';
    info[p++] = 'o';
    info[p++] = 'l';
    info[p++] = 'd';
    info[p++] = 'e';
    info[p++] = 'r';
    info[p++] = 's';
    info[p++] = ',';
    info[p++] = ' ';

    info[p++] = '0' + files;
    info[p++] = ' ';
    info[p++] = 'f';
    info[p++] = 'i';
    info[p++] = 'l';
    info[p++] = 'e';
    info[p++] = 's';
    info[p] = '\0';

    draw_string(
        win->x + 20,
        win->y + win->h - 25,
        info,
        GRAY,
        FONT_DEFAULT
    );
}

/* ============================================================
 * SETTINGS
 * ============================================================ */

static void draw_settings(void)
{
    Window *win = &windows[WINDOW_SETTINGS];

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

    if (settings_page == SETTINGS_MAIN)
    {
        draw_bmp_icon(
            win->x + 20,
            win->y + 46,
            "settings.bmp",
            32
        );

        draw_string(
            win->x + 22,
            win->y + 94,
            "Settings",
            WHITE,
            FONT_DEFAULT
        );

        draw_string(
            win->x + 20,
            win->y + 135,
            "Desktop",
            WHITE,
            FONT_DEFAULT
        );

        draw_string(
            win->x + 20,
            win->y + 155,
            "Appearance",
            WHITE,
            FONT_DEFAULT
        );

        draw_string(
            win->x + 20,
            win->y + 175,
            "System",
            WHITE,
            FONT_DEFAULT
        );

        draw_string(
            win->x + 20,
            win->y + 195,
            "About NyteOS",
            WHITE,
            FONT_DEFAULT
        );
    }

    if (settings_page == SETTINGS_DESKTOP)
{
    draw_string(
        win->x + 20,
        win->y + 50,
        "Desktop",
        WHITE,
        FONT_DEFAULT
    );

    draw_bmp_icon(
        win->x + 20,
        win->y + 85,
        "dwallpaper.bmp",
        100
    );

    draw_string(
        win->x + 140,
        win->y + 120,
        "Default",
        WHITE,
        FONT_DEFAULT
    );

    draw_bmp_icon(
        win->x + 20,
        win->y + 205,
        "wallpaper2.bmp",
        100
    );

    draw_string(
        win->x + 140,
        win->y + 240,
        "Wallpaper 2",
        WHITE,
        FONT_DEFAULT
    );

    draw_string(
        win->x + 20,
        win->y + 285,
        "< Back",
        CYAN,
        FONT_DEFAULT
    );

    return;
}

    if (settings_page == SETTINGS_ABOUT)
    {
        draw_bmp_icon(
            win->x + 20,
            win->y + 46,
            "nyteos.bmp",
            32
        );

        draw_string(
            win->x + 22,
            win->y + 94,
            "NyteOS",
            WHITE,
            FONT_DEFAULT
        );

        draw_string(
            win->x + 20,
            win->y + 130,
            "A lightweight operating system",
            GRAY,
            FONT_DEFAULT
        );

        draw_string(
            win->x + 20,
            win->y + 150,
            "made from scratch.",
            GRAY,
            FONT_DEFAULT
        );

        draw_string(
            win->x + 20,
            win->y + 185,
            "Version 0.5.1",
            WHITE,
            FONT_DEFAULT
        );

        draw_string(
            win->x + 20,
            win->y + 205,
            "x86 / 32-bit",
            WHITE,
            FONT_DEFAULT
        );

        draw_string(
            win->x + 20,
            win->y + 225,
            "NyteFS",
            CYAN,
            FONT_DEFAULT
        );

        draw_string(
            win->x + 20,
            win->y + 245,
            "Built with love <3",
            GRAY,
            FONT_DEFAULT
        );

        draw_string(
            win->x + 20,
            win->y + 285,
            "< Back",
            CYAN,
            FONT_DEFAULT
        );
    }
}

/* ============================================================
 * RELÓGIO RTC
 * ============================================================ */

static int clock_last_minute = -1;

static void draw_clock(void)
{
    rtc_time_t now;
    rtc_get_time(&now);

    static const char *months[] = {
        "January", "February", "March",
        "April", "May", "June",
        "July", "August", "September",
        "October", "November", "December"
    };

    char clock[6];
    char date[40];

    clock[0] = '0' + now.hour / 10;
    clock[1] = '0' + now.hour % 10;
    clock[2] = ':';
    clock[3] = '0' + now.minute / 10;
    clock[4] = '0' + now.minute % 10;
    clock[5] = '\0';

    int pos = 0;

    if (now.month >= 1 && now.month <= 12) {
        const char *month = months[now.month - 1];

        while (*month)
            date[pos++] = *month++;

        date[pos++] = ' ';

        if (now.day >= 10)
            date[pos++] = '0' + now.day / 10;

        date[pos++] = '0' + now.day % 10;
        date[pos++] = ',';
        date[pos++] = ' ';

        uint16_t divisor = 10000;

        while (divisor > 1 && now.year / divisor == 0)
            divisor /= 10;

        while (divisor > 0) {
            date[pos++] = '0' + (now.year / divisor) % 10;
            divisor /= 10;
        }

        date[pos] = '\0';
    } else {
        date[0] = '\0';
    }

    int date_width = pos * 7;

    draw_string(
        WIDTH - 54,
        HEIGHT - 30,
        clock,
        WHITE,
        FONT_DEFAULT
    );

    draw_string(
        WIDTH - date_width - 8,
        HEIGHT - 17,
        date,
        WHITE,
        FONT_DEFAULT
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
        y + 6,
        "nyteos.bmp",
        30
    );

    vline(
        46,
        y + 7,
        28,
        TASKBAR_BORDER
    );

    const char *icon_names[WINDOW_COUNT] = {
        "files.bmp",
        "term.bmp",
        "settings.bmp",
        "calc.bmp",
        "3d.bmp"
    };

    int total_width = WINDOW_COUNT * 42 - 6;
    int task_x = (WIDTH - total_width) / 2;

    for (int i = 0; i < WINDOW_COUNT; i++) {
        fill_rect(
            task_x,
            y + 7,
            36,
            28,
            windows[i].active
                ? GRAY
                : TASKBAR_COLOR
        );

        draw_bmp_icon(task_x + 6, y + 9, icon_names[i], 24);

        if (windows[i].open) {
            fill_rect(task_x + 16, y + 37, 4, 2, WHITE);
        }

        task_x += 42;
    }

    draw_clock();
}


/* ============================================================
 * DESKTOP
 * ============================================================ */

static void desktop_draw(void)
{
    draw_wallpaper();

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

    if (i == WINDOW_SETTINGS)
        draw_settings();

    if (i == WINDOW_CALC)
        draw_calculator();

    if (i == WINDOW_3D)
    {
        Window *win = &windows[WINDOW_3D];

        draw_window(
            win->x,
            win->y,
            win->w,
            win->h,
            win->title,
            win->active
        );

        software3d_draw_content(
            win->x,
            win->y,
            win->w,
            win->h
        );
    }
}

if (files_editor_open)
{
    draw_window(
        files_editor_x,
        files_editor_y,
        560,
        400,
        "FILE",
        1
    );

    int x = files_editor_x + 20;
    int y = files_editor_y + 40;
    int start_x = x;
    int max_x = files_editor_x + 540;
    int char_width = 8;
    int line_height = 16;

    for (int i = 0; i < files_editor_len; i++)
    {
        char c = files_editor_buffer[i];

        if (c == '\n')
        {
            x = start_x;
            y += line_height;
            continue;
        }

        if (x + char_width > max_x)
        {
            x = start_x;
            y += line_height;
        }

        if (y >= files_editor_y + 345)
            break;

        char text[2] = {c, '\0'};
        draw_string(x, y, text, WHITE, FONT_DEFAULT);
        x += char_width;
    }

    draw_string(
        files_editor_x + 20,
        files_editor_y + 360,
        "CTRL+S: SAVE    ESC: CLOSE",
        GRAY,
        FONT_DEFAULT
    );
}

    draw_taskbar();

    if (files_context_menu)
        draw_files_context_menu();
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
    if (files_editor_open)
    {
        files_editor_key(key);
        return;
    }

    if (files_creating)
    {
        files_create_key(key);
        return;
    }

    if ((unsigned char)key == 0x80 || key == 17)
    {
        if (term_history_count > 0 && term_history_idx < term_history_count - 1)
        {
            term_history_idx++;
            int h_idx = term_history_count - 1 - term_history_idx;
            
            int i = 0;
            while (term_history[h_idx][i] != '\0')
            {
                term_cmd_buffer[i] = term_history[h_idx][i];
                i++;
            }
            term_cmd_buffer[i] = '\0';
            term_cmd_len = i;
        }
        return;
    }

    if ((unsigned char)key == 0x81 || key == 18)
    {
        if (term_history_idx > 0)
        {
            term_history_idx--;
            int h_idx = term_history_count - 1 - term_history_idx;

            int i = 0;
            while (term_history[h_idx][i] != '\0')
            {
                term_cmd_buffer[i] = term_history[h_idx][i];
                i++;
            }
            term_cmd_buffer[i] = '\0';
            term_cmd_len = i;
        }
        else if (term_history_idx == 0)
        {
            term_history_idx = -1;
            term_cmd_buffer[0] = '\0';
            term_cmd_len = 0;
        }
        return;
    }

    if (key == '\n' || key == '\r')
    {
        term_cmd_buffer[term_cmd_len] = '\0';

        if (term_cmd_len > 0)
        {
            if (term_history_count < TERM_HISTORY_MAX)
            {
                int i = 0;
                while (term_cmd_buffer[i] != '\0')
                {
                    term_history[term_history_count][i] = term_cmd_buffer[i];
                    i++;
                }
                term_history[term_history_count][i] = '\0';
                term_history_count++;
            }
            else
            {
                for (int h = 1; h < TERM_HISTORY_MAX; h++)
                {
                    for (int c = 0; c < 128; c++)
                        term_history[h - 1][c] = term_history[h][c];
                }
                int i = 0;
                while (term_cmd_buffer[i] != '\0')
                {
                    term_history[TERM_HISTORY_MAX - 1][i] = term_cmd_buffer[i];
                    i++;
                }
                term_history[TERM_HISTORY_MAX - 1][i] = '\0';
            }
        }

        term_history_idx = -1;
        term_scroll_offset = 0;

        char full_line[160];
        int p = 0;
        const char *base = "nyte:/";

        while (*base && p < 150)
            full_line[p++] = *base++;

        if (current_dir != 0)
        {
            struct fs_entry entry;
            if (fs_read_entry(current_dir, &entry))
            {
                int i = 0;
                while (i < 32 && entry.name[i] != '\0' && p < 150)
                {
                    full_line[p++] = entry.name[i];
                    i++;
                }
            }
            else
            {
                if (p < 150) full_line[p++] = '?';
            }
        }

        if (p < 150) full_line[p++] = '>';
        if (p < 150) full_line[p++] = ' ';

        int i = 0;
        while (term_cmd_buffer[i] != '\0' && p < 158)
        {
            full_line[p++] = term_cmd_buffer[i++];
        }
        full_line[p] = '\0';

        term_print_line(full_line, WHITE);

        term_execute_command(term_cmd_buffer);

        term_cmd_len = 0;
        term_cmd_buffer[0] = '\0';
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

    if (key >= 32 && key <= 126)
    {
        if (term_cmd_len < sizeof(term_cmd_buffer) - 1)
        {
            term_cmd_buffer[term_cmd_len++] = key;
            term_cmd_buffer[term_cmd_len] = '\0';
        }
    }
}

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
    int old_right = 0;

    while (1) {

        int current_x = mouse_x;
        int current_y = mouse_y;

        mouse_x = old_x;
        mouse_y = old_y;
        restore_cursor_background();

        mouse_x = current_x;
        mouse_y = current_y;

        char key;

        while (keyboard_pop(&key))
        {
            if (files_creating)
            {
                if (key == '\n' || key == '\r')
                {
                    files_finish_create();
                }
                else if (key == '\b' || key == 127)
                {
                    if (files_new_name_len > 0)
                    {
                        files_new_name_len--;
                        files_new_name[files_new_name_len] = '\0';
                        desktop_draw();
                    }
                }
                else if (key >= 32 && key <= 126)
                {
                    if (files_new_name_len < 31)
                    {
                        files_new_name[files_new_name_len++] = key;
                        files_new_name[files_new_name_len] = '\0';
                        desktop_draw();
                    }
                }
            }
            else
            {
                shell_process_key(key);
                desktop_draw();
            }
        }

        mouse_poll();

        if (windows[WINDOW_3D].open &&
            !windows[WINDOW_3D].minimized &&
            software3d_mouse_move(mouse_x, mouse_y, mouse_left))
        {
            desktop_draw();
        }

        rtc_time_t rtc_now;
        rtc_get_time(&rtc_now);

        if (rtc_now.minute != clock_last_minute)
        {
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

        int skip_drag = 0;

        if (mouse_right && !old_right)
        {
            window_manager_mouse_down();
            desktop_draw();
        }

        if (mouse_left && !old_left)
        {
            window_manager_mouse_down();
            desktop_draw();
            skip_drag = 1;
        }

        if (mouse_left && !skip_drag)
        {
            int before_x = 0;
            int before_y = 0;

            if (active_window >= 0)
            {
                before_x = windows[active_window].x;
                before_y = windows[active_window].y;
            }

            window_manager_drag();

            if (active_window >= 0)
            {
                Window *win = &windows[active_window];

                if (win->x != before_x || win->y != before_y)
                {
                    desktop_draw();
                }
            }
        }

        if (!mouse_left && old_left)
        {
            window_manager_mouse_up();
        }

        save_cursor_background();
        draw_mouse_cursor();

        old_x = mouse_x;
        old_y = mouse_y;
        old_left = mouse_left;
        old_right = mouse_right;

        swap_buffers();
    }
}