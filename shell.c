#include <stdint.h>
#include <stddef.h>
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

extern volatile char key_buffer[1024];
extern volatile int kb_head;
extern volatile int kb_tail;

extern int keyboard_pop(char *key);


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


#define FS_FILE 1
#define FS_DIR  2

#define TASKBAR_H 42

struct fs_entry {
    char name[32];
    uint64_t size;
    uint64_t start_sector;
    uint64_t sector_count;
    unsigned int parent;
    unsigned char type;
    unsigned char used;
    unsigned char reserved[2];
};

_Static_assert(sizeof(struct fs_entry) == 64,
               "Shell NyteFS entry structure must stay 64 bytes");

extern uint32_t fs_max_entries;
extern uint64_t fs_total_sectors;
extern uint64_t fs_entry_lba;
extern uint64_t fs_bitmap_lba;
extern uint64_t fs_bitmap_sectors;
extern uint64_t fs_data_start;
extern uint64_t fs_data_end;
extern uint32_t fs_version;
extern int fs_read_entry(int index, struct fs_entry *entry);
extern int fs_read_file(struct fs_entry *entry,
                        unsigned char *buffer,
                        uint64_t capacity);
extern int fs_write_file(int index,
                         struct fs_entry *entry,
                         const unsigned char *buffer,
                         uint64_t size);
extern void *kmalloc(size_t size);
extern void kfree(void *ptr);

static int files_creating = 0;
static int files_renaming = 0;
static int files_rename_entry = -1;
static int files_name_dialog_x = -1;
static int files_name_dialog_y = -1;
static int files_name_dialog_dragging = 0;
static int files_name_dialog_drag_ox;
static int files_name_dialog_drag_oy;
static unsigned char files_creating_type = FS_FILE;
static char files_new_name[32];
static int files_new_name_len = 0;
static char files_search[32];
static int files_search_len = 0;
static int files_search_focused = 0;

extern int fs_find(const char *name, int parent);
extern int fs_find_free_entry(void);
extern uint64_t fs_find_free_data_sector(void);
extern int fs_set_data_sector(uint64_t sector, int allocated);
extern int fs_free_data_range(uint64_t start_sector, uint64_t sector_count);
extern int fs_write_entry(int index, struct fs_entry *entry);
extern int fs_read_sector(uint64_t sector, unsigned char *buffer);
extern int fs_write_sector(uint64_t sector, unsigned char *buffer);
extern unsigned int current_dir;
static unsigned int terminal_dir;

struct files_listing_item
{
    uint32_t index;
    struct fs_entry entry;
};
static int files_listing_get(int row, uint32_t *index,
                             struct fs_entry *entry);

static struct files_listing_item *files_listing;
static uint32_t *files_search_indices;
static uint32_t files_listing_capacity;
static uint32_t files_listing_count;
static uint32_t files_search_match_count;
static unsigned int files_listing_dir = (unsigned int)-1;
static int files_listing_valid;
static int files_search_filter_valid;
static void bmp_icon_cache_clear(void);

static void files_listing_invalidate(void)
{
    files_listing_valid = 0;
    files_search_filter_valid = 0;
}

static int files_name_matches_search(const char *name)
{
    if (!files_search_len)
        return 1;

    for (int start = 0; name[start]; start++)
    {
        int i = 0;
        while (files_search[i] && name[start + i])
        {
            char a = name[start + i];
            char b = files_search[i];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b)
                break;
            i++;
        }
        if (i == files_search_len)
            return 1;
    }
    return 0;
}

static int files_listing_ensure(void)
{
    if (files_listing_valid && files_listing_dir == current_dir)
        return 1;

    if (!files_listing && fs_max_entries)
    {
        size_t bytes = (size_t)fs_max_entries * sizeof(*files_listing);
        files_listing = kmalloc(bytes);
        if (files_listing)
            files_listing_capacity = fs_max_entries;
    }

    if (!files_listing)
        return 0;

    files_listing_count = 0;
    for (uint32_t i = 0; i < fs_max_entries; i++)
    {
        struct fs_entry entry;
        if (!fs_read_entry((int)i, &entry))
            continue;
        if (!entry.used || i == 0 || entry.parent != current_dir)
            continue;
        if (files_listing_count >= files_listing_capacity)
            break;
        files_listing[files_listing_count].index = i;
        files_listing[files_listing_count].entry = entry;
        files_listing_count++;
    }

    files_listing_dir = current_dir;
    files_listing_valid = 1;
    files_search_filter_valid = 0;
    return 1;
}

void term_print_line(const char* str, uint32_t color);

void fs_list_term(unsigned int dir_idx) {
    struct fs_entry entry;
    int found = 0;

    for (uint32_t i = 0; i < fs_max_entries; i++) {
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

extern void ata_select(uint16_t io, uint8_t drive);
extern uint64_t ata_get_drive_size_mb(uint16_t io, uint8_t drive);
extern int installer_prepare_disk(uint16_t io, uint8_t drive);
extern int installer_get_source_disk(uint16_t *io, uint8_t *drive);

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
static int8_t mouse_packet[4];
static int mouse_packet_size = 3;
static int mouse_wheel_delta = 0;


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

    mouse_write(0xF3);
    mouse_read();
    mouse_write(200);
    mouse_read();

    mouse_write(0xF3);
    mouse_read();
    mouse_write(100);
    mouse_read();

    mouse_write(0xF3);
    mouse_read();
    mouse_write(80);
    mouse_read();

    mouse_write(0xF2);
    mouse_read();

    uint8_t mouse_id = mouse_read();

    mouse_packet_size = (mouse_id == 3 || mouse_id == 4) ? 4 : 3;

    mouse_write(0xF4);
    mouse_read();

    mouse_x = WIDTH / 2;
    mouse_y = HEIGHT / 2;
}

static void mouse_poll(void)
{
    while (inb(0x64) & 1)
    {
        uint8_t status = inb(0x64);

        if (!(status & 0x20))
        {
            (void)inb(0x60);
            continue;
        }

        uint8_t data = inb(0x60);

        if (mouse_packet_index == 0)
        {
            if (!(data & 0x08))
                continue;
        }

        mouse_packet[mouse_packet_index++] = (int8_t)data;

        if (mouse_packet_index < mouse_packet_size)
            continue;

        mouse_packet_index = 0;

        int8_t dx = mouse_packet[1];
        int8_t dy = mouse_packet[2];

        mouse_left  = mouse_packet[0] & 1;
        mouse_right = mouse_packet[0] & 2;

        if (mouse_packet_size == 4)
        {
            int wheel = mouse_packet[3] & 0x0F;

            if (wheel & 0x08)
                wheel -= 16;

            mouse_wheel_delta += wheel;
        }

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

static int wallpaper_name_matches(const unsigned char *data,
                                  uint64_t size,
                                  const char *name)
{
    uint64_t i = 0;
    while (name[i])
    {
        if (i >= size || data[i] != (unsigned char)name[i])
            return 0;
        i++;
    }
    return i == size;
}

static void wallpaper_load_setting(void)
{
    int index = fs_find("wallpaper.cfg", 0);
    if (index < 0)
        return;

    struct fs_entry entry;
    unsigned char value[32];
    if (!fs_read_entry(index, &entry) || entry.type != FS_FILE ||
        entry.size >= sizeof(value) ||
        !fs_read_file(&entry, value, sizeof(value)))
        return;

    if (wallpaper_name_matches(value, entry.size, "wallpaper2.bmp"))
        selected_wallpaper = "wallpaper2.bmp";
    else if (wallpaper_name_matches(value, entry.size, "dwallpaper.bmp"))
        selected_wallpaper = "dwallpaper.bmp";
}

static int wallpaper_save_setting(const char *name)
{
    int index = fs_find("wallpaper.cfg", 0);
    struct fs_entry entry;

    if (index >= 0)
    {
        if (!fs_read_entry(index, &entry) || entry.type != FS_FILE)
            return 0;
    }
    else
    {
        index = fs_find_free_entry();
        if (index < 0)
            return 0;

        for (int i = 0; i < sizeof(entry); i++)
            ((unsigned char *)&entry)[i] = 0;

        const char filename[] = "wallpaper.cfg";
        for (int i = 0; filename[i]; i++)
            entry.name[i] = filename[i];

        entry.parent = 0;
        entry.type = FS_FILE;
        entry.used = 1;
    }

    uint64_t length = 0;
    while (name[length])
        length++;

    int saved = fs_write_file(index, &entry,
                              (const unsigned char *)name, length);
    if (saved)
        files_listing_invalidate();
    return saved;
}

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
        if (!fs_read_sector(
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

static unsigned char bmp_buffer[4096];
#define BMP_ICON_CACHE_COUNT 16
struct bmp_icon_cache_entry
{
    char name[32];
    uint32_t size;
    struct fs_entry entry;
    unsigned char *data;
};
static struct bmp_icon_cache_entry bmp_icon_cache[BMP_ICON_CACHE_COUNT];
#define IMAGE_VIEWER_MAX_SIZE (4U * 1024U * 1024U)
static unsigned char *image_viewer_buffer;
static uint32_t *image_viewer_pixels;
static uint64_t image_viewer_size;
static int image_viewer_pixel_width;
static int image_viewer_pixel_height;

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

static int bmp_icon_cache_find(const char *name, struct fs_entry *entry)
{
    for (int i = 0; i < BMP_ICON_CACHE_COUNT; i++)
    {
        struct bmp_icon_cache_entry *cached = &bmp_icon_cache[i];
        if (!cached->data)
            continue;

        int match = 1;
        for (int j = 0; j < 32; j++)
        {
            if (cached->name[j] != name[j])
            {
                match = 0;
                break;
            }
            if (!name[j])
                break;
        }
        if (!match)
            continue;

        *entry = cached->entry;
        for (uint32_t j = 0; j < cached->size; j++)
            bmp_buffer[j] = cached->data[j];
        return 1;
    }
    return 0;
}

static int bmp_icon_cache_load(const char *name, struct fs_entry *entry)
{
    int empty_slot = -1;

    for (int i = 0; i < BMP_ICON_CACHE_COUNT; i++)
    {
        struct bmp_icon_cache_entry *cached = &bmp_icon_cache[i];
        if (!cached->data)
        {
            if (empty_slot < 0)
                empty_slot = i;
            continue;
        }

        int match = 1;
        for (int j = 0; j < 32; j++)
        {
            if (cached->name[j] != name[j])
            {
                match = 0;
                break;
            }
            if (!name[j])
                break;
        }

        if (match && cached->size == entry->size)
        {
            for (uint32_t j = 0; j < cached->size; j++)
                bmp_buffer[j] = cached->data[j];
            return 1;
        }
    }

    if (!fs_read_file(entry, bmp_buffer, sizeof(bmp_buffer)))
        return 0;

    if (empty_slot >= 0)
    {
        struct bmp_icon_cache_entry *cached = &bmp_icon_cache[empty_slot];
        cached->data = kmalloc((size_t)entry->size);
        if (cached->data)
        {
            cached->size = (uint32_t)entry->size;
            cached->entry = *entry;
            for (uint32_t j = 0; j < cached->size; j++)
                cached->data[j] = bmp_buffer[j];
            for (int j = 0; j < 32; j++)
                cached->name[j] = name[j];
        }
    }

    return 1;
}

static void bmp_icon_cache_clear(void)
{
    for (int i = 0; i < BMP_ICON_CACHE_COUNT; i++)
    {
        if (bmp_icon_cache[i].data)
            kfree(bmp_icon_cache[i].data);
        bmp_icon_cache[i].data = 0;
        bmp_icon_cache[i].size = 0;
        bmp_icon_cache[i].name[0] = '\0';
    }
}

static void draw_window(
    int x, int y, int w, int h, const char *title, int active, int show_minimize
);

static void draw_image_viewer(int window_x,
                              int window_y,
                              int window_w,
                              int window_h,
                              int active)
{
    draw_window(window_x, window_y, window_w, window_h,
                "Image Viewer", active, 0);

    int view_x = window_x + 10;
    int view_y = window_y + 32;
    int view_w = window_w - 20;
    int view_h = window_h - 42;
    fill_rect(view_x, view_y, view_w, view_h, 0x00000000);

    if (!image_viewer_buffer || image_viewer_size < 54)
    {
        draw_string(view_x + 8, view_y + 8,
                    "Could not load BMP image.", WHITE, FONT_DEFAULT);
        return;
    }

    const unsigned char *bmp = image_viewer_buffer;
    uint32_t size = (uint32_t)image_viewer_size;
    uint32_t pixel_offset = bmp_u32(bmp + 10);
    uint32_t dib_size = bmp_u32(bmp + 14);
    int32_t width = (int32_t)bmp_u32(bmp + 18);
    int32_t signed_height = (int32_t)bmp_u32(bmp + 22);
    uint16_t bpp = (uint16_t)bmp_u16(bmp + 28);
    uint32_t compression = bmp_u32(bmp + 30);

    if (bmp[0] != 'B' || bmp[1] != 'M' || dib_size < 40 ||
        dib_size > size - 14 || width <= 0 ||
        signed_height == 0 || signed_height == INT32_MIN ||
        width > 32768 || signed_height > 32768 || signed_height < -32768 ||
        (bpp != 8 && bpp != 24 && bpp != 32) || compression != 0)
    {
        draw_string(view_x + 8, view_y + 8,
                    "Unsupported or invalid BMP.", WHITE, FONT_DEFAULT);
        return;
    }

    int bottom_up = signed_height > 0;
    int32_t height = bottom_up ? signed_height : -signed_height;
    uint32_t row_size = (((uint32_t)width * bpp + 31) / 32) * 4;
    if (pixel_offset > size || row_size > size - pixel_offset ||
        (uint32_t)height > (size - pixel_offset) / row_size)
    {
        draw_string(view_x + 8, view_y + 8,
                    "Invalid BMP pixel data.", WHITE, FONT_DEFAULT);
        return;
    }

    uint32_t palette_count = bmp_u32(bmp + 46);
    uint64_t palette_offset = 14U + dib_size;
    if (bpp == 8)
    {
        if (!palette_count)
            palette_count = 256;
        if (palette_count > 256 ||
            palette_offset + (uint64_t)palette_count * 4U > pixel_offset ||
            palette_offset + (uint64_t)palette_count * 4U > size)
        {
            draw_string(view_x + 8, view_y + 8,
                        "Invalid BMP palette.", WHITE, FONT_DEFAULT);
            return;
        }
    }

    int draw_w = view_w;
    int draw_h = (int)(((uint32_t)height * view_w) / (uint32_t)width);
    if (draw_h > view_h)
    {
        draw_h = view_h;
        draw_w = (int)(((uint32_t)width * view_h) / (uint32_t)height);
    }
    if (draw_w < 1 || draw_h < 1)
        return;

    int origin_x = view_x + (view_w - draw_w) / 2;
    int origin_y = view_y + (view_h - draw_h) / 2;

    if (image_viewer_pixel_width != draw_w ||
        image_viewer_pixel_height != draw_h || !image_viewer_pixels)
    {
        if (image_viewer_pixels)
            kfree(image_viewer_pixels);
        image_viewer_pixels = 0;
        image_viewer_pixel_width = 0;
        image_viewer_pixel_height = 0;

        uint32_t pixel_count = (uint32_t)draw_w * (uint32_t)draw_h;
        image_viewer_pixels = kmalloc((size_t)pixel_count * sizeof(uint32_t));
        if (!image_viewer_pixels)
        {
            draw_string(view_x + 8, view_y + 8,
                        "Not enough memory to display image.",
                        WHITE, FONT_DEFAULT);
            return;
        }

        for (int y = 0; y < draw_h; y++)
        {
            uint32_t source_y = ((uint32_t)y * (uint32_t)height) / draw_h;
            uint32_t bmp_y = bottom_up
                ? (uint32_t)height - 1 - source_y : source_y;
            uint32_t row = pixel_offset + bmp_y * row_size;

            for (int x = 0; x < draw_w; x++)
            {
                uint32_t source_x = ((uint32_t)x * (uint32_t)width) / draw_w;
                uint32_t pixel = row + source_x * (bpp / 8);
                uint32_t red, green, blue;

                if (bpp == 8)
                {
                    uint32_t index = bmp[pixel];
                    if (index >= palette_count)
                    {
                        image_viewer_pixels[y * draw_w + x] = 0;
                        continue;
                    }
                    uint64_t color = palette_offset + (uint64_t)index * 4U;
                    blue = bmp[color];
                    green = bmp[color + 1];
                    red = bmp[color + 2];
                }
                else
                {
                    blue = bmp[pixel];
                    green = bmp[pixel + 1];
                    red = bmp[pixel + 2];
                }

                image_viewer_pixels[y * draw_w + x] =
                    (red << 16) | (green << 8) | blue;
            }
        }

        image_viewer_pixel_width = draw_w;
        image_viewer_pixel_height = draw_h;
    }

    for (int y = 0; y < draw_h; y++)
    {
        uint32_t *destination = &backbuffer[(origin_y + y) * WIDTH + origin_x];
        uint32_t *source = &image_viewer_pixels[y * draw_w];
        for (int x = 0; x < draw_w; x++)
            destination[x] = source[x];
    }
}

static void draw_bmp_icon(
    int x,
    int y,
    const char *name,
    int size
)
{
    struct fs_entry entry;
    if (!bmp_icon_cache_find(name, &entry))
    {
        int index = fs_find(name, 0);
        if (index < 0 || !fs_read_entry(index, &entry) ||
            entry.type == FS_DIR || entry.size == 0 ||
            entry.size > sizeof(bmp_buffer) ||
            !bmp_icon_cache_load(name, &entry))
            return;
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
#define WINDOW_IMAGE     5
#define WINDOW_COUNT     6
#define WINDOW_TASKBAR_COUNT 5

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
    { 140, 65, 520, 420, "NyteOS 3D Renderer", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 150, 85, 500, 400, "Image Viewer", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};

static int window_z[WINDOW_COUNT] = {
    WINDOW_FILES,
    WINDOW_TERM,
    WINDOW_SETTINGS,
    WINDOW_CALC,
    WINDOW_3D,
    WINDOW_IMAGE
};

static int active_window = -1;

static void draw_window(
    int x,
    int y,
    int w,
    int h,
    const char *title,
    int active,
    int show_minimize
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

    if (show_minimize)
    {
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
    }

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

#define TERM_HISTORY_MAX 20

static char term_history[TERM_HISTORY_MAX][128];
static int term_history_count = 0;
static int term_history_idx = -1;

static int term_scroll_offset = 0;

extern volatile char key_buffer[1024];

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

    for (uint32_t i = 0; i < fs_max_entries; i++)
    {
        if (!fs_read_entry((int)i, &entry))
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
    if (terminal_dir == 0)
    {
        shell_print("/", CYAN);
        return;
    }

    struct fs_entry entry;

    if (fs_read_entry(terminal_dir, &entry))
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
    if (fs_find(name, terminal_dir) != -1)
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
    entry.parent = terminal_dir;
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
    int idx = fs_find(name, terminal_dir);

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

    char line[65];
    int p = 0;

    for (uint64_t sector = 0;
         sector < entry.sector_count;
         sector++)
    {
        if (!fs_read_sector(entry.start_sector + sector, fs_sector))
        {
            shell_print("Error reading file.", RED);
            return;
        }

        uint64_t remaining = entry.size - sector * 512;
        unsigned int count = remaining > 512 ? 512 : (unsigned int)remaining;
        for (unsigned int i = 0; i < count; i++)
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
    int idx = fs_find(name, terminal_dir);

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

    if (entry.type == FS_FILE)
    {
        if (!fs_free_data_range(entry.start_sector, entry.sector_count))
        {
            shell_print("Error freeing file sectors.", RED);
            return;
        }
    }

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

    int idx = fs_find(filename, terminal_dir);
    struct fs_entry entry;

    if (idx == -1)
    {
        idx = fs_find_free_entry();
        if (idx == -1)
        {
            shell_print("Error: No space for more entries.", RED);
            return;
        }

        for (int j = 0; j < 64; j++)
            ((unsigned char *)&entry)[j] = 0;
        int j = 0;
        while (filename[j] && j < 31) { entry.name[j] = filename[j]; j++; }

        entry.parent = terminal_dir;
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
        {
            entry.start_sector = fs_find_free_data_sector();
            entry.sector_count = 1;
        }
    }

    if (entry.start_sector == (uint64_t)-1)
    {
        shell_print("Error: No free data sector.", RED);
        return;
    }

    uint64_t len = 0;
    while (content[len]) len++;

    if (fs_write_file(idx, &entry,
                      (const unsigned char *)content, len))
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

#define FILES_EDITOR_CAPACITY 16384
static char files_view_buffer[513];
static char files_editor_buffer[FILES_EDITOR_CAPACITY + 1];
static int files_editor_len = 0;
static int files_editor_cursor = 0;
static int files_editor_open = 0;
static int files_editor_entry = -1;

static int files_extension_is(const char *name, const char *ext);

static void shell_cmd_edit(const char *name)
{
    int idx = fs_find(name, terminal_dir);
    struct fs_entry entry;

    if (idx == -1)
    {
        idx = fs_find_free_entry();
        if (idx == -1)
        {
            shell_print("Error: No space for more entries.", RED);
            return;
        }

        for (int i = 0; i < 64; i++)
            ((unsigned char *)&entry)[i] = 0;
        int i = 0;
        while (name[i] && i < 31) { entry.name[i] = name[i]; i++; }

        entry.parent = terminal_dir;
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
            entry.sector_count = 1;
            fs_write_entry(idx, &entry);
        }
    }

    if (entry.start_sector == (uint64_t)-1)
    {
        shell_print("Error: No free data sector.", RED);
        return;
    }

    if (entry.size > FILES_EDITOR_CAPACITY)
    {
        shell_print("File is too large for the text editor.", RED);
        return;
    }

    files_editor_len = (int)entry.size;

    if (entry.size > 0)
    {
        if (!fs_read_file(&entry,
                          (unsigned char *)files_editor_buffer,
                          FILES_EDITOR_CAPACITY))
            return;
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
    uint64_t used_sectors = fs_data_start +
        fs_bitmap_sectors +
        (((uint64_t)fs_max_entries * FS_ENTRY_SIZE + FS_SECTOR_SIZE - 1) /
         FS_SECTOR_SIZE);

    for (uint64_t map_sector = 0;
         map_sector < fs_bitmap_sectors;
         map_sector++)
    {
        if (!fs_read_sector(fs_bitmap_lba + map_sector, fs_bitmap))
            break;

        for (int i = 0; i < FS_SECTOR_SIZE; i++)
        {
            if (fs_version == 1)
            {
                if (fs_bitmap[i])
                    used_sectors++;
            }
            else
            {
                for (int bit = 0; bit < 8; bit++)
                {
                    if (fs_bitmap[i] & (1U << bit))
                        used_sectors++;
                }
            }
        }
    }

    uint64_t used_ssd_kb64 = (used_sectors + 1) / 2;
    int used_ssd_kb = used_ssd_kb64 > 0x7FFFFFFF
        ? 0x7FFFFFFF
        : (int)used_ssd_kb64;
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

    if (kb_tail >= 1024)
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
        shell_print("NyteOS v0.6 TNE", WHITE);
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
            "NyteOS v0.6 TNE",
            WHITE
        );
    }

    else if (term_strcmp(command, "whichdir") == 0)
    {
        shell_cmd_whichdir();
    }

    else if (term_strcmp(command, "list") == 0)
    {
        shell_cmd_list(terminal_dir);
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
                    terminal_dir
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
            terminal_dir = 0;

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
                    terminal_dir
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
                    terminal_dir = idx;

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

    if (shell_strncmp(command, "mkdir ", 6) == 0 ||
        shell_strncmp(command, "rem ", 4) == 0 ||
        shell_strncmp(command, "write ", 6) == 0 ||
        shell_strncmp(command, "edit ", 5) == 0)
    {
        files_listing_invalidate();
        bmp_icon_cache_clear();
    }
}

static void draw_terminal(void)
{
    Window *win = &windows[WINDOW_TERM];
    if (!win->open) return;

    draw_window(win->x, win->y, win->w, win->h, win->title, win->active, 1);

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

        if (terminal_dir != 0)
        {
            struct fs_entry entry;
            if (fs_read_entry(terminal_dir, &entry))
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

    if (index == WINDOW_IMAGE && !image_viewer_buffer)
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

    if (index == WINDOW_IMAGE)
    {
        if (image_viewer_pixels)
            kfree(image_viewer_pixels);
        if (image_viewer_buffer)
            kfree(image_viewer_buffer);
        image_viewer_pixels = 0;
        image_viewer_buffer = 0;
        image_viewer_size = 0;
        image_viewer_pixel_width = 0;
        image_viewer_pixel_height = 0;
    }

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

static int files_scroll_offset = 0;

#define SETTINGS_MAIN       0
#define SETTINGS_DESKTOP    1
#define SETTINGS_ABOUT      2

static int settings_page = SETTINGS_MAIN;

static void files_delete_entry(int index);
static void files_create_key(char key);
static int files_copy_entry(int source_index, unsigned int destination);
static int files_move_entry(int source_index, unsigned int destination);
static int files_paste_entry(unsigned int destination);

static int files_context_menu = 0;
static int files_context_entry = -1;
static int files_context_is_file = 0;
static int files_context_is_dir = 0;
static int files_context_is_root = 0;
static int files_context_x = 0;
static int files_context_y = 0;
static int files_clipboard_entry = -1;
static int files_clipboard_mode = 0;
static char files_status_message[40];
static uint32_t files_status_color = RED;
static void files_set_status(const char *message, uint32_t color);
static void files_clear_status(void);
static void files_finish_create(void);

#define FILES_NAME_DIALOG_W 360
#define FILES_NAME_DIALOG_H 180
#define FILES_CONTEXT_MENU_W 160
#define FILES_CONTEXT_MENU_ROW_H 22
#define FILES_CONTEXT_MENU_PAD 4
#define FILES_CLIPBOARD_NONE 0
#define FILES_CLIPBOARD_COPY 1
#define FILES_CLIPBOARD_MOVE 2

static int files_context_menu_rows(void)
{
    if (files_context_entry < 0)
        return 3;
    return files_context_is_dir ? 5 : 4;
}

static int files_context_menu_height(void)
{
    return FILES_CONTEXT_MENU_PAD * 2 +
           files_context_menu_rows() * FILES_CONTEXT_MENU_ROW_H;
}

static void files_name_dialog_position(int *x, int *y)
{
    if (files_name_dialog_x < 0 || files_name_dialog_y < 0)
    {
        files_name_dialog_x = (WIDTH - FILES_NAME_DIALOG_W) / 2;
        files_name_dialog_y =
            (HEIGHT - TASKBAR_H - FILES_NAME_DIALOG_H) / 2;
    }
    *x = files_name_dialog_x;
    *y = files_name_dialog_y;
}

static int files_editor_x = 120;
static int files_editor_y = 80;
static int files_editor_w = 560;
static int files_editor_h = 400;
static int files_editor_maximized = 0;
static int files_editor_old_x;
static int files_editor_old_y;
static int files_editor_old_w;
static int files_editor_old_h;
static int files_editor_dragging = 0;
static int files_editor_drag_ox = 0;
static int files_editor_drag_oy = 0;

static void files_editor_toggle_maximize(void)
{
    if (!files_editor_maximized)
    {
        files_editor_old_x = files_editor_x;
        files_editor_old_y = files_editor_y;
        files_editor_old_w = files_editor_w;
        files_editor_old_h = files_editor_h;
        files_editor_x = 0;
        files_editor_y = 0;
        files_editor_w = WIDTH;
        files_editor_h = HEIGHT - TASKBAR_H;
        files_editor_maximized = 1;
    }
    else
    {
        files_editor_x = files_editor_old_x;
        files_editor_y = files_editor_old_y;
        files_editor_w = files_editor_old_w;
        files_editor_h = files_editor_old_h;
        files_editor_maximized = 0;
    }

    desktop_draw();
}

static void files_editor_close_window(void)
{
    files_editor_open = 0;
    files_editor_entry = -1;
    files_editor_dragging = 0;

    if (files_editor_maximized)
    {
        files_editor_x = files_editor_old_x;
        files_editor_y = files_editor_old_y;
        files_editor_w = files_editor_old_w;
        files_editor_h = files_editor_old_h;
        files_editor_maximized = 0;
    }

    desktop_draw();
}

static int calc_value1 = 0;
static char calc_op = 0;
static char calc_display[16] = "0";
static int calc_new_number = 1;

static void calc_int_to_str(int n, char *buf);

static void window_manager_mouse_down(void)
{
    files_search_focused = 0;

    if (files_creating || files_renaming)
    {
        if (!mouse_left)
            return;

        int x, y;
        files_name_dialog_position(&x, &y);
        int button_y = y + FILES_NAME_DIALOG_H - 42;

        if (mouse_y >= button_y && mouse_y < button_y + 30)
        {
            if (mouse_x >= x + FILES_NAME_DIALOG_W - 180 &&
                mouse_x < x + FILES_NAME_DIALOG_W - 100)
            {
                files_finish_create();
                return;
            }
            if (mouse_x >= x + FILES_NAME_DIALOG_W - 95 &&
                mouse_x < x + FILES_NAME_DIALOG_W - 20)
            {
                files_creating = 0;
                files_renaming = 0;
                files_rename_entry = -1;
                files_name_dialog_dragging = 0;
                files_new_name_len = 0;
                files_new_name[0] = '\0';
                desktop_draw();
                return;
            }
        }

        if (mouse_y >= y + 4 && mouse_y < y + 25 &&
            mouse_x >= x + FILES_NAME_DIALOG_W - 28 &&
            mouse_x < x + FILES_NAME_DIALOG_W)
        {
            files_creating = 0;
            files_renaming = 0;
            files_rename_entry = -1;
            files_name_dialog_dragging = 0;
            files_new_name_len = 0;
            files_new_name[0] = '\0';
            desktop_draw();
            return;
        }

        if (mouse_y >= y && mouse_y < y + 25)
        {
            files_name_dialog_dragging = 1;
            files_name_dialog_drag_ox = mouse_x - x;
            files_name_dialog_drag_oy = mouse_y - y;
        }

        return;
    }

    if (files_editor_open &&
        mouse_x >= files_editor_x &&
        mouse_x < files_editor_x + files_editor_w &&
        mouse_y >= files_editor_y &&
        mouse_y < files_editor_y + files_editor_h)
    {
        if (mouse_y < files_editor_y + 25)
        {
            if (mouse_x >= files_editor_x + files_editor_w - 21 &&
                mouse_x < files_editor_x + files_editor_w - 9)
            {
                files_editor_close_window();
                return;
            }

            if (mouse_x >= files_editor_x + files_editor_w - 43 &&
                mouse_x < files_editor_x + files_editor_w - 22)
            {
                files_editor_toggle_maximize();
                return;
            }

            if (mouse_x >= files_editor_x + files_editor_w - 64 &&
                mouse_x < files_editor_x + files_editor_w - 43)
                return;

            files_editor_dragging = 1;
            files_editor_drag_ox = mouse_x - files_editor_x;
            files_editor_drag_oy = mouse_y - files_editor_y;
        }

        return;
    }

    int bar_y = HEIGHT - 42;
    int total_width = WINDOW_TASKBAR_COUNT * 42 - 6;
    int task_start = (WIDTH - total_width) / 2;

    int task = -1;

    if (mouse_y >= bar_y + 7 && mouse_y < bar_y + 35 &&
        mouse_x >= task_start && mouse_x < task_start + total_width)
    {
        int offset = mouse_x - task_start;
        int index = offset / 42;

        if (index >= 0 && index < WINDOW_TASKBAR_COUNT && offset % 42 < 36)
            task = index;
    }

    if (task >= 0)
    {
        files_context_menu = 0;
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

    if (files_context_menu)
    {
        if (index == WINDOW_FILES &&
            mouse_x >= files_context_x &&
            mouse_x < files_context_x + FILES_CONTEXT_MENU_W &&
            mouse_y >= files_context_y &&
            mouse_y < files_context_y + files_context_menu_height())
        {
            int row_y = mouse_y - files_context_y - FILES_CONTEXT_MENU_PAD;
            int action = row_y >= 0
                ? row_y / FILES_CONTEXT_MENU_ROW_H : -1;
            if (action < 0 || action >= files_context_menu_rows())
                action = -1;

            if (files_context_entry < 0 && action == 0 && current_dir != 0)
            {
                files_creating = 1;
                files_renaming = 0;
                files_rename_entry = -1;
                files_creating_type = FS_FILE;
                files_new_name_len = 0;
                files_new_name[0] = '\0';
            }
            else if (files_context_entry < 0 && action == 1 &&
                     current_dir != 0)
            {
                files_creating = 1;
                files_renaming = 0;
                files_rename_entry = -1;
                files_creating_type = FS_DIR;
                files_new_name_len = 0;
                files_new_name[0] = '\0';
            }
            else if (files_context_entry < 0 && action == 2)
            {
                files_paste_entry(current_dir);
            }
            else if (files_context_is_file && !files_context_is_root &&
                     action == 0 && files_context_entry >= 0)
            {
                files_clipboard_entry = files_context_entry;
                files_clipboard_mode = FILES_CLIPBOARD_COPY;
                files_set_status("Ready to copy", GREEN);
            }
            else if (files_context_entry >= 0 && !files_context_is_root &&
                     action == 1)
            {
                files_clipboard_entry = files_context_entry;
                files_clipboard_mode = FILES_CLIPBOARD_MOVE;
                files_set_status("Ready to move", GREEN);
            }
            else if (files_context_is_dir && action == 3 &&
                     files_context_entry >= 0)
            {
                files_paste_entry((unsigned int)files_context_entry);
            }
            else if (action == 2 &&
                     files_context_entry >= 0 && !files_context_is_root)
            {
                struct fs_entry entry;
                if (fs_read_entry(files_context_entry, &entry) &&
                    entry.used && entry.parent != 0)
                {
                    files_creating = 0;
                    files_renaming = 1;
                    files_rename_entry = files_context_entry;
                    files_new_name_len = 0;
                    while (files_new_name_len < 31 &&
                           entry.name[files_new_name_len])
                    {
                        files_new_name[files_new_name_len] =
                            entry.name[files_new_name_len];
                        files_new_name_len++;
                    }
                    files_new_name[files_new_name_len] = '\0';
                }
            }
            else if (!files_context_is_dir && action == 3 &&
                     files_context_is_file && !files_context_is_root &&
                     files_context_entry >= 0)
            {
                files_delete_entry(files_context_entry);
            }

            files_context_menu = 0;
            window_raise(WINDOW_FILES);
            desktop_draw();
            return;
        }

        files_context_menu = 0;
    }

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
                    wallpaper_save_setting(selected_wallpaper);
                    desktop_draw();
                    return;
                }

                if (mouse_x >= win->x + 20 && mouse_x < win->x + 220 &&
                    mouse_y >= win->y + 205 && mouse_y < win->y + 285)
                {
                    selected_wallpaper = "wallpaper2.bmp";
                    wallpaper_save_setting(selected_wallpaper);
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

                if (mouse_x >= w->x + 10 &&
                    mouse_x < w->x + w->w - 10 &&
                    mouse_y >= w->y + 56 &&
                    mouse_y < w->y + w->h - 35)
                {
                    files_context_entry = -1;
                    files_context_is_file = 0;
                    files_context_is_dir = 0;
                    files_context_is_root = 0;
                    int first_y = w->y + 60 +
                                  (current_dir != 0 ? 24 : 0);
                    if (mouse_y >= first_y)
                    {
                        int row = ((mouse_y - first_y) / 24) +
                                  files_scroll_offset;
                        uint32_t entry_index;
                        struct fs_entry entry;
                        if (files_listing_get(row, &entry_index, &entry))
                        {
                            files_context_entry = entry_index;
                            files_context_is_file = entry.type == FS_FILE;
                            files_context_is_dir = entry.type == FS_DIR;
                            files_context_is_root = entry.parent == 0;
                        }
                    }

                    files_context_x = mouse_x;
                    files_context_y = mouse_y;
                    if (files_context_x >
                        w->x + w->w - FILES_CONTEXT_MENU_W - 8)
                        files_context_x =
                            w->x + w->w - FILES_CONTEXT_MENU_W - 8;
                    if (files_context_y >
                        w->y + w->h - 35 - files_context_menu_height())
                        files_context_y =
                            w->y + w->h - 35 - files_context_menu_height();
                    files_context_menu = 1;
                    desktop_draw();
                    return;
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

        if (index == WINDOW_FILES)
        {
            files_search_focused = 0;
            if (mouse_x >= win->x + 10 &&
                mouse_x < win->x + win->w - 10 &&
                mouse_y >= win->y + 30 && mouse_y < win->y + 56)
            {
                files_search_focused = 1;
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
                        files_scroll_offset = 0;
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
                int row = ((mouse_y - first_y) / 24) + files_scroll_offset;
                uint32_t entry_index;
                struct fs_entry entry;
                if (files_listing_get(row, &entry_index, &entry))
                {
                    files_selected_entry = entry_index;

                    if (entry.type == FS_DIR)
                    {
                        current_dir = entry_index;
                        files_scroll_offset = 0;
                        files_selected_entry = -1;
                        files_listing_invalidate();
                        desktop_draw();
                    }
                    else
                        files_open_file(entry_index);
                }
                return;
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

        return;
    }
}

static void window_minimize(int index)
{
    if (index < 0 || index >= WINDOW_COUNT)
        return;

    if (index == WINDOW_IMAGE)
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

    for (int i = 0; i < WINDOW_TASKBAR_COUNT; i++) {

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
    files_name_dialog_dragging = 0;
    software3d_mouse_up();
}


static void window_manager_drag(void)
{
    if (files_name_dialog_dragging)
    {
        files_name_dialog_x = mouse_x - files_name_dialog_drag_ox;
        files_name_dialog_y = mouse_y - files_name_dialog_drag_oy;

        if (files_name_dialog_x < 0)
            files_name_dialog_x = 0;
        if (files_name_dialog_y < 0)
            files_name_dialog_y = 0;
        if (files_name_dialog_x + FILES_NAME_DIALOG_W > WIDTH)
            files_name_dialog_x = WIDTH - FILES_NAME_DIALOG_W;
        if (files_name_dialog_y + FILES_NAME_DIALOG_H > HEIGHT - TASKBAR_H)
            files_name_dialog_y = HEIGHT - TASKBAR_H - FILES_NAME_DIALOG_H;

        desktop_draw();
        return;
    }

    if (files_editor_dragging)
    {
        files_editor_x = mouse_x - files_editor_drag_ox;
        files_editor_y = mouse_y - files_editor_drag_oy;

        if (files_editor_x < 0) files_editor_x = 0;
        if (files_editor_y < 0) files_editor_y = 0;
        if (files_editor_x + files_editor_w > WIDTH)
            files_editor_x = WIDTH - files_editor_w;
        if (files_editor_y + files_editor_h > HEIGHT - TASKBAR_H)
            files_editor_y = HEIGHT - TASKBAR_H - files_editor_h;

        desktop_draw();
        return;
    }

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

    draw_window(win->x, win->y, win->w, win->h, win->title, win->active, 1);

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

static void files_set_status(const char *message, uint32_t color)
{
    int i = 0;
    while (i < (int)sizeof(files_status_message) - 1 && message[i])
    {
        files_status_message[i] = message[i];
        i++;
    }
    files_status_message[i] = '\0';
    files_status_color = color;
}

static void files_clear_status(void)
{
    files_status_message[0] = '\0';
}

static int files_copy_entry(int source_index, unsigned int destination)
{
    struct fs_entry source;
    struct fs_entry folder;
    if (source_index < 0 || !fs_read_entry(source_index, &source) ||
        !source.used || source.parent == 0 || source.type != FS_FILE ||
        !fs_read_entry((int)destination, &folder) || !folder.used ||
        folder.type != FS_DIR)
    {
        files_set_status("Cannot copy this item", RED);
        return 0;
    }
    if (source.size > 0xFFFFFFFFULL)
    {
        files_set_status("File too large to copy", RED);
        return 0;
    }

    unsigned char *buffer = 0;
    if (source.size)
    {
        buffer = (unsigned char *)kmalloc((size_t)source.size);
        if (!buffer || !fs_read_file(&source, buffer, source.size))
        {
            if (buffer)
                kfree(buffer);
            files_set_status(buffer ? "Could not read file" :
                             "Not enough RAM", RED);
            return 0;
        }
    }

    char candidate[32];
    int suffix = 0;
    int new_index = -1;
    files_set_status("Name limit reached", RED);
    for (; suffix < 1000; suffix++)
    {
        for (int i = 0; i < 32; i++)
            candidate[i] = '\0';
        int base_len = 0;
        while (base_len < 31 && source.name[base_len])
            base_len++;
        int stem_len = base_len;
        for (int i = 1; i < base_len - 1; i++)
        {
            if (source.name[i] == '.')
                stem_len = i;
        }
        int extension_len = base_len - stem_len;
        int copy_number = suffix + (destination == source.parent ? 1 : 0);
        if (suffix == 0 && destination != source.parent)
        {
            for (int i = 0; i < base_len; i++)
                candidate[i] = source.name[i];
        }
        else
        {
            const char *tag = copy_number == 1 ? " copy" : " copy ";
            int tag_len = copy_number == 1 ? 5 : 6;
            int number = copy_number;
            char digits[5];
            int digit_count = 0;
            if (copy_number > 1)
            {
                do
                {
                    digits[digit_count++] = (char)('0' + number % 10);
                    number /= 10;
                }
                while (number && digit_count < 4);
            }
            int keep_extension = extension_len;
            int max_extension = 31 - tag_len - digit_count - 1;
            if (keep_extension > max_extension)
                keep_extension = max_extension;
            int keep = stem_len;
            int max_stem = 31 - tag_len - digit_count - keep_extension;
            if (keep > max_stem)
                keep = max_stem;
            if (keep < 1)
                keep = 1;
            int pos = 0;
            while (pos < keep)
            {
                candidate[pos] = source.name[pos];
                pos++;
            }
            for (int i = 0; i < tag_len; i++)
                candidate[pos++] = tag[i];
            for (int i = digit_count - 1; i >= 0; i--)
                candidate[pos++] = digits[i];
            for (int i = 0; i < keep_extension; i++)
                candidate[pos++] = source.name[stem_len + i];
            candidate[pos] = '\0';
        }
        if (fs_find(candidate, (int)destination) < 0)
        {
            new_index = fs_find_free_entry();
            if (new_index >= 0)
                break;
            files_set_status("No free file entries", RED);
            break;
        }
    }

    int saved = 0;
    if (new_index >= 0)
    {
        struct fs_entry copy = source;
        for (int i = 0; i < 32; i++)
            copy.name[i] = i < 31 ? candidate[i] : '\0';
        copy.parent = destination;
        copy.size = 0;
        copy.start_sector = 0;
        copy.sector_count = 0;
        copy.used = 1;
        saved = fs_write_file(new_index, &copy, buffer, source.size);
    }
    if (buffer)
        kfree(buffer);
    if (saved)
    {
        files_clear_status();
        files_listing_invalidate();
    }
    else if (new_index >= 0)
    {
        files_set_status("Disk full or write failed", RED);
    }
    return saved;
}

static int files_move_entry(int source_index, unsigned int destination)
{
    struct fs_entry source;
    struct fs_entry folder;
    if (source_index < 0 || !fs_read_entry(source_index, &source) ||
        !source.used || source.parent == 0 ||
        !fs_read_entry((int)destination, &folder) || !folder.used ||
        folder.type != FS_DIR)
    {
        files_set_status("Cannot move this item", RED);
        return 0;
    }
    if (source.parent == destination)
    {
        files_clear_status();
        return 1;
    }
    if (fs_find(source.name, (int)destination) >= 0)
    {
        files_set_status("Name already exists", RED);
        return 0;
    }

    if (source.type == FS_DIR)
    {
        unsigned int parent = destination;
        for (uint32_t i = 0; i < fs_max_entries; i++)
        {
            if (parent == (unsigned int)source_index)
            {
                files_set_status("Cannot move folder into itself", RED);
                return 0;
            }
            if (parent == 0)
                break;
            struct fs_entry ancestor;
            if (!fs_read_entry((int)parent, &ancestor) || !ancestor.used ||
                ancestor.type != FS_DIR || ancestor.parent >= fs_max_entries)
            {
                files_set_status("Invalid folder path", RED);
                return 0;
            }
            parent = ancestor.parent;
            if (i + 1 == fs_max_entries && parent != 0)
            {
                files_set_status("Invalid folder path", RED);
                return 0;
            }
        }
    }

    source.parent = destination;
    if (!fs_write_entry(source_index, &source))
    {
        files_set_status("Could not move item", RED);
        return 0;
    }
    files_clear_status();
    files_listing_invalidate();
    return 1;
}

static int files_paste_entry(unsigned int destination)
{
    if (files_clipboard_entry < 0)
    {
        files_set_status("Nothing to paste", RED);
        return 0;
    }
    if (destination == 0)
    {
        files_set_status("Cannot paste in root", RED);
        return 0;
    }
    struct fs_entry source;
    if (!fs_read_entry(files_clipboard_entry, &source) || !source.used)
    {
        files_clipboard_entry = -1;
        files_clipboard_mode = FILES_CLIPBOARD_NONE;
        files_set_status("Source item no longer exists", RED);
        return 0;
    }
    if (files_clipboard_mode == FILES_CLIPBOARD_COPY)
    {
        if (files_copy_entry(files_clipboard_entry, destination))
        {
            files_set_status("File copied", GREEN);
            return 1;
        }
        return 0;
    }
    if (files_clipboard_mode == FILES_CLIPBOARD_MOVE &&
        files_move_entry(files_clipboard_entry, destination))
    {
        files_clipboard_entry = -1;
        files_clipboard_mode = FILES_CLIPBOARD_NONE;
        files_set_status("Item moved", GREEN);
        return 1;
    }
    return 0;
}

static void files_delete_entry(int index)
{
    if (index < 0)
    {
        files_set_status("No file selected", RED);
        return;
    }

    struct fs_entry entry;

    if (!fs_read_entry(index, &entry))
    {
        files_set_status("Could not read file entry", RED);
        return;
    }

    if (!entry.used)
    {
        files_set_status("File no longer exists", RED);
        return;
    }

    if (entry.parent == 0)
    {
        files_set_status("Cannot change root items", RED);
        return;
    }

    if (entry.type != FS_FILE)
    {
        files_set_status("Cannot delete this folder", RED);
        return;
    }

    if (!fs_free_data_range(entry.start_sector, entry.sector_count))
    {
        files_set_status("Could not free file data", RED);
        return;
    }

    entry.used = 0;

    if (fs_write_entry(index, &entry))
    {
        files_set_status("File deleted", GREEN);
        files_listing_invalidate();
    }
    else
    {
        files_set_status("Could not delete file", RED);
    }

    files_selected_entry = -1;
}

static void draw_files_context_icon(int x, int y, int icon, uint32_t color)
{
    if (icon == 0) /* New File */
    {
        rect(x + 3, y, 11, 15, color);
        hline(x + 6, y + 5, 5, color);
        hline(x + 6, y + 8, 5, color);
        hline(x + 6, y + 11, 4, color);
    }
    else if (icon == 1) /* New Folder */
    {
        fill_rect(x + 1, y + 2, 8, 4, color);
        fill_rect(x + 1, y + 5, 15, 10, color);
        rect(x + 1, y + 5, 15, 10, color);
    }
    else if (icon == 2) /* Paste */
    {
        rect(x + 2, y + 2, 13, 13, color);
        fill_rect(x + 6, y, 6, 4, color);
        hline(x + 5, y + 7, 7, color);
        hline(x + 5, y + 10, 7, color);
    }
    else if (icon == 3) /* Copy */
    {
        rect(x + 1, y + 1, 10, 12, color);
        rect(x + 5, y + 4, 10, 12, color);
    }
    else if (icon == 4) /* Move */
    {
        hline(x + 1, y + 7, 12, color);
        vline(x + 2, y + 4, 7, color);
        hline(x + 10, y + 4, 5, color);
        vline(x + 14, y + 4, 7, color);
        hline(x + 10, y + 10, 5, color);
        fill_rect(x + 11, y + 5, 4, 5, color);
    }
    else if (icon == 5) /* Rename */
    {
        hline(x + 2, y + 13, 12, color);
        fill_rect(x + 5, y + 7, 10, 3, color);
        vline(x + 4, y + 8, 4, color);
    }
    else /* Delete */
    {
        rect(x + 3, y + 4, 11, 11, color);
        hline(x + 2, y + 2, 13, color);
        fill_rect(x + 6, y, 5, 2, color);
        vline(x + 6, y + 7, 6, color);
        vline(x + 10, y + 7, 6, color);
    }
}

static void draw_files_context_menu(void)
{
    if (!files_context_menu)
        return;

    int x = files_context_x;
    int y = files_context_y;
    int height = files_context_menu_height();
    fill_rect_alpha(x + 4, y + 4, FILES_CONTEXT_MENU_W,
                    height, BLACK, 50);
    fill_rect(x, y, FILES_CONTEXT_MENU_W, height,
              0x00191B22);
    rect(x, y, FILES_CONTEXT_MENU_W, height,
         0x00606070);
    fill_rect(x + 1, y + 1, 3, height - 2, 0x003080C0);

    const char *labels[5];
    uint32_t colors[5];
    int icons[5];
    if (files_context_entry < 0)
    {
        labels[0] = "New File";
        labels[1] = "New Folder";
        labels[2] = "Paste";
        icons[0] = 0;
        icons[1] = 1;
        icons[2] = 2;
        colors[0] = colors[1] = current_dir ? WHITE : GRAY;
        colors[2] = files_clipboard_entry >= 0 && current_dir
            ? WHITE : GRAY;
    }
    else if (files_context_is_dir)
    {
        labels[0] = "Copy";
        labels[1] = "Move";
        labels[2] = "Rename";
        labels[3] = "Paste Here";
        labels[4] = "Delete";
        icons[0] = 3;
        icons[1] = 4;
        icons[2] = 5;
        icons[3] = 2;
        icons[4] = 6;
        colors[0] = GRAY; /* Recursive directory copy is not supported yet. */
        colors[1] = files_context_is_root ? GRAY : WHITE;
        colors[2] = files_context_is_root ? GRAY : WHITE;
        colors[3] = files_clipboard_entry >= 0 ? WHITE : GRAY;
        colors[4] = GRAY; /* Non-empty directory deletion is not supported. */
    }
    else
    {
        labels[0] = "Copy";
        labels[1] = "Move";
        labels[2] = "Rename";
        labels[3] = "Delete";
        icons[0] = 3;
        icons[1] = 4;
        icons[2] = 5;
        icons[3] = 6;
        colors[0] = colors[1] = colors[2] = colors[3] =
            files_context_is_root ? GRAY : WHITE;
        if (!files_context_is_root)
            colors[3] = RED;
    }

    for (int row = 0; row < files_context_menu_rows(); row++)
    {
        int row_y = y + FILES_CONTEXT_MENU_PAD +
                    row * FILES_CONTEXT_MENU_ROW_H;
        if (colors[row] != GRAY)
            fill_rect(x + 5, row_y, FILES_CONTEXT_MENU_W - 7,
                      FILES_CONTEXT_MENU_ROW_H, 0x00242832);
        draw_files_context_icon(x + 10, row_y + 3, icons[row], colors[row]);
        draw_string(x + 34, row_y + 6, labels[row], colors[row],
                    FONT_DEFAULT);
    }
}

static void draw_files_name_dialog(void)
{
    int x, y;
    files_name_dialog_position(&x, &y);
    int input_x = x + 20;
    int input_y = y + 66;
    int input_w = FILES_NAME_DIALOG_W - 40;
    int input_h = 28;
    int button_y = y + FILES_NAME_DIALOG_H - 42;
    const char *title = files_renaming ? "Rename" :
        (files_creating_type == FS_DIR ? "New Folder" : "New File");

    fill_rect_alpha(x + 6, y + 6, FILES_NAME_DIALOG_W,
                    FILES_NAME_DIALOG_H, 0x00000000, 50);
    fill_rect(x, y + 25, FILES_NAME_DIALOG_W,
              FILES_NAME_DIALOG_H - 25, WINDOW_COLOR);
    rect(x, y, FILES_NAME_DIALOG_W, FILES_NAME_DIALOG_H, TITLE_ACTIVE);
    fill_rect_alpha(x + 1, y + 1, FILES_NAME_DIALOG_W - 2, 24,
                    TITLE_ACTIVE, 180);
    draw_string(x + 8, y + 9, title, WHITE, FONT_DEFAULT);
    fill_circle(x + FILES_NAME_DIALOG_W - 15, y + 12, 6, RED);
    draw_string(x + FILES_NAME_DIALOG_W - 17, y + 8,
                "x", WHITE, FONT_DEFAULT);

    draw_string(x + 20, y + 42,
                files_renaming ? "Rename to:" : "Name:",
                WHITE, FONT_DEFAULT);

    fill_rect(input_x, input_y, input_w, input_h, 0x00141418);
    rect(input_x, input_y, input_w, input_h, GREEN);
    draw_string(input_x + 8, input_y + 9, files_new_name,
                WHITE, FONT_DEFAULT);
    fill_rect(input_x + 8 + files_new_name_len * 8,
              input_y + 8, 2, 11, WHITE);

    fill_rect(x + FILES_NAME_DIALOG_W - 180, button_y, 80, 28,
              0x00006020);
    rect(x + FILES_NAME_DIALOG_W - 180, button_y, 80, 28, GREEN);
    draw_string(x + FILES_NAME_DIALOG_W - 161, button_y + 10,
                files_renaming ? "Rename" : "OK", WHITE, FONT_DEFAULT);

    fill_rect(x + FILES_NAME_DIALOG_W - 95, button_y, 75, 28,
              DARK_GRAY);
    rect(x + FILES_NAME_DIALOG_W - 95, button_y, 75, 28, LIGHT_GRAY);
    draw_string(x + FILES_NAME_DIALOG_W - 82, button_y + 10,
                "Cancel", WHITE, FONT_DEFAULT);
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

    if (!fs_write_file(files_editor_entry,
                       &entry,
                       (const unsigned char *)files_editor_buffer,
                       (uint64_t)files_editor_len))
        return;
}

static void files_editor_key(char key)
{
    if (key == 19)
    {
        files_editor_save();
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

        }

        return;
    }

    if (key >= 32 && key <= 126)
    {
        if (files_editor_len < FILES_EDITOR_CAPACITY)
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

        }

        return;
    }
}

static void files_create_key(char key)
{
    if (key == 27)
    {
        files_creating = 0;
        files_renaming = 0;
        files_rename_entry = -1;
        files_name_dialog_dragging = 0;
        files_new_name_len = 0;
        files_new_name[0] = '\0';
        return;
    }

    if (key == '\b')
    {
        if (files_new_name_len > 0)
        {
            files_new_name_len--;
            files_new_name[files_new_name_len] = '\0';
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
        }

        return;
    }
}

static void files_finish_create(void)
{
    if (files_renaming)
    {
        struct fs_entry entry;
        if (files_new_name_len == 0 || files_rename_entry < 0 ||
            !fs_read_entry(files_rename_entry, &entry) ||
            !entry.used || entry.parent == 0)
        {
            files_set_status(files_new_name_len == 0
                ? "Name cannot be empty" : "Cannot rename this item", RED);
            files_renaming = 0;
            files_rename_entry = -1;
            files_new_name_len = 0;
            files_new_name[0] = '\0';
            desktop_draw();
            return;
        }

        int existing = fs_find(files_new_name, entry.parent);
        if (existing >= 0 && existing != files_rename_entry)
        {
            files_set_status("Name already exists", RED);
            files_renaming = 0;
            files_rename_entry = -1;
            files_new_name_len = 0;
            files_new_name[0] = '\0';
            desktop_draw();
            return;
        }

        for (int i = 0; i < 32; i++)
            entry.name[i] = '\0';
        for (int i = 0; i < files_new_name_len; i++)
            entry.name[i] = files_new_name[i];

        if (fs_write_entry(files_rename_entry, &entry))
        {
            files_listing_invalidate();
            files_set_status("Item renamed", GREEN);
        }
        else
        {
            files_set_status("Could not rename item", RED);
        }

        files_renaming = 0;
        files_rename_entry = -1;
        files_new_name_len = 0;
        files_new_name[0] = '\0';
        desktop_draw();
        return;
    }

    if (current_dir == 0)
    {
        files_set_status("Cannot create items in root", RED);
        files_creating = 0;
        files_new_name_len = 0;
        files_new_name[0] = '\0';
        desktop_draw();
        return;
    }

    if (files_new_name_len == 0)
    {
        files_set_status("Name cannot be empty", RED);
        files_creating = 0;
        desktop_draw();
        return;
    }

    files_new_name[files_new_name_len] = '\0';

    char final_name[32];
    int final_name_len = files_new_name_len;
    for (int i = 0; i < 32; i++)
        final_name[i] = '\0';
    for (int i = 0; i < final_name_len; i++)
        final_name[i] = files_new_name[i];

    if (files_creating_type == FS_FILE)
    {
        int has_extension = 0;
        for (int i = 1; i < final_name_len - 1; i++)
        {
            if (final_name[i] == '.')
                has_extension = 1;
        }
        if (!has_extension)
        {
            if (final_name_len > 0 && final_name[final_name_len - 1] == '.')
                final_name_len--;
            if (final_name_len > 27)
                final_name_len = 27;
            final_name[final_name_len++] = '.';
            final_name[final_name_len++] = 't';
            final_name[final_name_len++] = 'x';
            final_name[final_name_len++] = 't';
            final_name[final_name_len] = '\0';
        }
    }

    if (fs_find(final_name, current_dir) >= 0)
    {
        files_set_status("Name already exists", RED);
        files_creating = 0;
        desktop_draw();
        return;
    }

    int index = fs_find_free_entry();

    if (index < 0)
    {
        files_set_status("No free file entries", RED);
        files_creating = 0;
        desktop_draw();
        return;
    }

    uint64_t sector = 0;
    if (files_creating_type == FS_FILE)
    {
        sector = fs_find_free_data_sector();
        if (sector == (uint64_t)-1)
        {
            files_set_status("Disk is full", RED);
            files_creating = 0;
            desktop_draw();
            return;
        }
    }

    struct fs_entry entry;

    for (int i = 0; i < 64; i++)
        ((unsigned char *)&entry)[i] = 0;

    for (int i = 0; i < final_name_len; i++)
        entry.name[i] = final_name[i];

    entry.size = 0;
    entry.start_sector = files_creating_type == FS_FILE ? sector : 0;
    entry.sector_count = files_creating_type == FS_FILE ? 1 : 0;
    entry.parent = current_dir;
    entry.type = files_creating_type;
    entry.used = 1;

    if (!fs_write_entry(index, &entry))
    {
        if (files_creating_type == FS_FILE)
            fs_free_data_range(sector, 1);
        files_set_status("Could not create item", RED);
        files_creating = 0;
        desktop_draw();
        return;
    }

    files_listing_invalidate();
    files_set_status(files_creating_type == FS_FILE
        ? "File created" : "Folder created", GREEN);

    files_creating = 0;
    files_renaming = 0;
    files_rename_entry = -1;
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

    if (files_extension_is(entry.name, ".bmp"))
    {
        if (!entry.size || entry.size > IMAGE_VIEWER_MAX_SIZE)
            return;

        unsigned char *buffer = kmalloc((size_t)entry.size);
        if (!buffer)
            return;

        if (!fs_read_file(&entry, buffer, IMAGE_VIEWER_MAX_SIZE))
        {
            kfree(buffer);
            return;
        }

        if (image_viewer_buffer)
            kfree(image_viewer_buffer);
        if (image_viewer_pixels)
            kfree(image_viewer_pixels);

        image_viewer_buffer = buffer;
        image_viewer_size = entry.size;
        image_viewer_pixels = 0;
        image_viewer_pixel_width = 0;
        image_viewer_pixel_height = 0;

        int window_w = 500;
        int window_h = 400;
        if (entry.size >= 54 && buffer[0] == 'B' && buffer[1] == 'M')
        {
            int32_t bmp_width = (int32_t)bmp_u32(buffer + 18);
            int32_t bmp_height = (int32_t)bmp_u32(buffer + 22);
            if (bmp_height < 0 && bmp_height != INT32_MIN)
                bmp_height = -bmp_height;

            if (bmp_width > 0 && bmp_height > 0 &&
                bmp_width <= 32768 && bmp_height <= 32768)
            {
                window_w = bmp_width + 24;
                window_h = bmp_height + 54;
                if (window_w < 320) window_w = 320;
                if (window_h < 240) window_h = 240;
                if (window_w > WIDTH - 24) window_w = WIDTH - 24;
                if (window_h > HEIGHT - TASKBAR_H - 16)
                    window_h = HEIGHT - TASKBAR_H - 16;
            }
        }

        windows[WINDOW_IMAGE].w = window_w;
        windows[WINDOW_IMAGE].h = window_h;
        windows[WINDOW_IMAGE].x = (WIDTH - window_w) / 2;
        windows[WINDOW_IMAGE].y =
            (HEIGHT - TASKBAR_H - window_h) / 2;
        windows[WINDOW_IMAGE].minimized = 0;
        windows[WINDOW_IMAGE].maximized = 0;
        window_open(WINDOW_IMAGE);
        desktop_draw();
        return;
    }

    if (files_editor_open)
        return;

    if (entry.size > FILES_EDITOR_CAPACITY)
        return;

    files_editor_len = (int)entry.size;
    if (entry.size &&
        !fs_read_file(&entry,
                      (unsigned char *)files_editor_buffer,
                      FILES_EDITOR_CAPACITY))
        return;

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

    fs_write_file(files_editor_entry,
                  &entry,
                  (const unsigned char *)files_editor_buffer,
                  (uint64_t)files_editor_len);
}

static int files_extension_is(const char *name, const char *ext)
{
    int i = 0;
    int j = 0;

    while (name[i] && name[i] != '.')
        i++;

    if (!name[i])
        return 0;

    while (name[i] && ext[j]) {
        char a = name[i++];
        char b = ext[j++];

        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;

        if (a != b)
            return 0;
    }

    return name[i] == '\0' && ext[j] == '\0';
}

static const char *files_get_icon(const struct fs_entry *entry)
{
    if (entry->type == FS_DIR)
        return "folder.bmp";

    if (files_extension_is(entry->name, ".txt") ||
        files_extension_is(entry->name, ".md")  ||
        files_extension_is(entry->name, ".log") ||
        files_extension_is(entry->name, ".ini"))
        return "txt.bmp";

    if (files_extension_is(entry->name, ".bmp") ||
        files_extension_is(entry->name, ".rimg") ||
        files_extension_is(entry->name, ".png") ||
        files_extension_is(entry->name, ".jpg") ||
        files_extension_is(entry->name, ".jpeg"))
        return "image.bmp";

    if (files_extension_is(entry->name, ".c")   ||
        files_extension_is(entry->name, ".h")   ||
        files_extension_is(entry->name, ".cpp") ||
        files_extension_is(entry->name, ".asm") ||
        files_extension_is(entry->name, ".py")  ||
        files_extension_is(entry->name, ".sh"))
        return "code.bmp";

    return "file.bmp";
}

static int files_count_entries(void)
{
    if (files_listing_ensure())
    {
        if (!files_search_len)
            return (int)files_listing_count;

        if (files_search_filter_valid)
            return (int)files_search_match_count;

        if (!files_search_indices && files_listing_capacity)
            files_search_indices = kmalloc(
                (size_t)files_listing_capacity * sizeof(*files_search_indices));

        int count = 0;
        for (uint32_t i = 0; i < files_listing_count; i++)
            if (files_name_matches_search(files_listing[i].entry.name))
            {
                if (files_search_indices)
                    files_search_indices[count] = i;
                count++;
            }
        files_search_match_count = (uint32_t)count;
        files_search_filter_valid = files_search_indices != 0;
        return count;
    }

    int total = 0;

    for (uint32_t i = 0; i < fs_max_entries; i++)
    {
        struct fs_entry entry;
        if (!fs_read_entry((int)i, &entry) || !entry.used || i == 0 ||
            entry.parent != current_dir ||
            !files_name_matches_search(entry.name))
            continue;
        total++;
    }

    return total;
}

static int files_listing_get(int row, uint32_t *index,
                             struct fs_entry *entry)
{
    if (row < 0)
        return 0;
    if (files_listing_ensure())
    {
        if (!files_search_len)
        {
            if ((uint32_t)row >= files_listing_count)
                return 0;
            *index = files_listing[row].index;
            *entry = files_listing[row].entry;
            return 1;
        }

        if (!files_search_filter_valid)
            files_count_entries();
        if (files_search_filter_valid)
        {
            if ((uint32_t)row >= files_search_match_count)
                return 0;
            uint32_t item = files_search_indices[row];
            *index = files_listing[item].index;
            *entry = files_listing[item].entry;
            return 1;
        }

        int matched_row = 0;
        for (uint32_t i = 0; i < files_listing_count; i++)
        {
            if (!files_name_matches_search(files_listing[i].entry.name))
                continue;
            if (matched_row++ == row)
            {
                *index = files_listing[i].index;
                *entry = files_listing[i].entry;
                return 1;
            }
        }
        return 0;
    }

    int visible_row = 0;
    for (uint32_t i = 0; i < fs_max_entries; i++)
    {
        struct fs_entry candidate;
        if (!fs_read_entry((int)i, &candidate) || !candidate.used || i == 0 ||
            candidate.parent != current_dir ||
            !files_name_matches_search(candidate.name))
            continue;
        if (visible_row++ == row)
        {
            *index = i;
            *entry = candidate;
            return 1;
        }
    }
    return 0;
}


static int files_visible_rows(void)
{
    Window *win = &windows[WINDOW_FILES];

    int first_y = win->y + 60;

    if (current_dir != 0)
        first_y += 24;

    int bottom_y = win->y + win->h - 45;
    int rows = (bottom_y - first_y) / 24;

    if (rows < 1)
        rows = 1;

    return rows;
}

static void files_scroll(int delta)
{
    Window *win = &windows[WINDOW_FILES];

    if (!win->open || win->minimized)
        return;

    if (mouse_x < win->x || mouse_x >= win->x + win->w ||
        mouse_y < win->y || mouse_y >= win->y + win->h)
        return;

    int total = files_count_entries();
    int visible = files_visible_rows();
    int max_scroll = total - visible;

    if (max_scroll < 0)
        max_scroll = 0;

    files_scroll_offset += delta;

    if (files_scroll_offset < 0)
        files_scroll_offset = 0;

    if (files_scroll_offset > max_scroll)
        files_scroll_offset = max_scroll;

    desktop_draw();
}

static void files_append_number(char *buffer, int *pos, int number)
{
    char digits[12];
    int count = 0;

    if (number == 0)
        digits[count++] = '0';
    else
    {
        while (number > 0)
        {
            digits[count++] = '0' + (number % 10);
            number /= 10;
        }
    }

    while (count > 0)
        buffer[(*pos)++] = digits[--count];
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
        win->active,
        1
    );

    fill_rect(
        win->x + 10,
        win->y + 31,
        win->w - 20,
        24,
        files_search_focused ? 0x00303038 : 0x00202028
    );
    rect(win->x + 10, win->y + 31, win->w - 20, 24,
         files_search_focused ? GREEN : DARK_GRAY);
    draw_string(
        win->x + 17,
        win->y + 38,
        files_search_len ? files_search : "Search files...",
        files_search_len ? WHITE : GRAY,
        FONT_DEFAULT
    );

    int first_y = win->y + 60;
    int y = first_y;

    if (current_dir != 0)
    {
        draw_string(
            win->x + 20,
            y,
            "..",
            YELLOW,
            FONT_DEFAULT
        );

        y += 24;
    }

    int folders = 0;
    int files = 0;
    int visible_rows = files_visible_rows();
    int total_entries = files_count_entries();
    int visible_row = 0;

    int max_scroll = total_entries - visible_rows;

    if (max_scroll < 0)
        max_scroll = 0;

    if (files_scroll_offset > max_scroll)
        files_scroll_offset = max_scroll;

    for (int row = 0; row < total_entries; row++)
    {
        uint32_t index;
        struct fs_entry entry;
        if (!files_listing_get(row, &index, &entry))
            continue;
        (void)index;

        if (entry.type == FS_DIR)
            folders++;
        else
            files++;

        if (visible_row < files_scroll_offset)
        {
            visible_row++;
            continue;
        }

        if (visible_row >= files_scroll_offset + visible_rows)
        {
            visible_row++;
            continue;
        }

        int item_y = y + ((visible_row - files_scroll_offset) * 24);

        draw_bmp_icon(
            win->x + 20,
            item_y - 4,
            files_get_icon(&entry),
            20
        );

        draw_string(
            win->x + 48,
            item_y,
            entry.name,
            WHITE,
            FONT_DEFAULT
        );

        visible_row++;
    }

    if (max_scroll < 0)
        max_scroll = 0;

    if (files_scroll_offset > max_scroll)
        files_scroll_offset = max_scroll;

    if (total_entries > visible_rows)
    {
        int track_x = win->x + win->w - 10;
        int track_y = y;
        int track_h = visible_rows * 24;

        int thumb_h = (visible_rows * track_h) / total_entries;

        if (thumb_h < 12)
            thumb_h = 12;

        int thumb_y = track_y;

        if (max_scroll > 0)
        {
            thumb_y +=
                (files_scroll_offset * (track_h - thumb_h)) /
                max_scroll;
        }

        fill_rect(
            track_x,
            track_y,
            3,
            track_h,
            DARK_GRAY
        );

        fill_rect(
            track_x,
            thumb_y,
            3,
            thumb_h,
            LIGHT_GRAY
        );
    }

    char info[64];
    int p = 0;

    files_append_number(info, &p, folders);

    const char *text = " folders, ";
    while (*text)
        info[p++] = *text++;

    files_append_number(info, &p, files);

    text = " files";
    while (*text)
        info[p++] = *text++;

    info[p] = '\0';

    draw_string(
        win->x + 20,
        win->y + win->h - 25,
        files_status_message[0] ? files_status_message : info,
        files_status_message[0] ? files_status_color : GRAY,
        FONT_DEFAULT
    );

    draw_files_context_menu();
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
        win->active,
        1
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
            "Version 0.6 TNE",
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

    const char *icon_names[WINDOW_TASKBAR_COUNT] = {
        "files.bmp",
        "term.bmp",
        "settings.bmp",
        "calc.bmp",
        "3d.bmp"
    };

    int total_width = WINDOW_TASKBAR_COUNT * 42 - 6;
    int task_x = (WIDTH - total_width) / 2;

    for (int i = 0; i < WINDOW_TASKBAR_COUNT; i++) {
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
            win->active,
            1
        );

        software3d_draw_content(
            win->x,
            win->y,
            win->w,
            win->h
        );
    }

    if (i == WINDOW_IMAGE)
        draw_image_viewer(
            windows[i].x,
            windows[i].y,
            windows[i].w,
            windows[i].h,
            windows[i].active
        );
}

if (files_editor_open)
{
    draw_window(
        files_editor_x,
        files_editor_y,
        files_editor_w,
        files_editor_h,
        "FILE",
        1,
        0
    );

    int start_x = files_editor_x + 20;
    int max_x = files_editor_x + files_editor_w - 20;
    int char_width = 8;
    int line_height = 16;
    int columns = (max_x - start_x) / char_width;
    int total_lines = 1;
    int column = 0;

    for (int i = 0; i < files_editor_len; i++)
    {
        if (files_editor_buffer[i] == '\n')
        {
            total_lines++;
            column = 0;
        }
        else if (++column > columns)
        {
            total_lines++;
            column = 1;
        }
    }

    int visible_lines = (files_editor_h - 90) / line_height;
    if (visible_lines < 1)
        visible_lines = 1;
    int first_line = total_lines > visible_lines
        ? total_lines - visible_lines : 0;
    int line = 0;
    int x = start_x;
    int y = files_editor_y + 40;
    column = 0;

    for (int i = 0; i < files_editor_len; i++)
    {
        char c = files_editor_buffer[i];

        if (c == '\n')
        {
            line++;
            column = 0;
            x = start_x;
            y += line_height;
            continue;
        }

        if (column >= columns)
        {
            line++;
            column = 0;
            x = start_x;
            y += line_height;
        }

        if (line >= first_line)
            draw_char(x, y, (unsigned char)c, WHITE);
        x += char_width;
        column++;
    }

    draw_string(
        files_editor_x + 20,
        files_editor_y + files_editor_h - 30,
        "CTRL+S: SAVE",
        GRAY,
        FONT_DEFAULT
    );
}

    if (files_creating || files_renaming)
        draw_files_name_dialog();

    draw_taskbar();
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

    if (files_creating || files_renaming)
    {
        files_create_key(key);
        return;
    }

    if (files_search_focused)
    {
        if (key == 27 || key == '\r' || key == '\n')
        {
            files_search_focused = 0;
        }
        else if (key == '\b' || key == 127)
        {
            if (files_search_len > 0)
            {
                files_search_len--;
                files_search[files_search_len] = '\0';
                files_scroll_offset = 0;
                files_search_filter_valid = 0;
            }
        }
        else if (key >= 32 && key <= 126 && files_search_len < 31)
        {
            files_search[files_search_len++] = key;
            files_search[files_search_len] = '\0';
            files_scroll_offset = 0;
            files_search_filter_valid = 0;
        }
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

        if (terminal_dir != 0)
        {
            struct fs_entry entry;
            if (fs_read_entry(terminal_dir, &entry))
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

uint16_t installer_target_io = 0;
uint8_t installer_target_drive = 0;
uint64_t installer_target_size = 0;
int installer_target_selected = 0;

void installer_progress_update(uint32_t phase, uint32_t step)
{
    static const char *phase_names[] = {
        "Preparing installation...",
        "Copying NyteOS to the selected disk...",
        "Preparing filesystem bitmap...",
        "Creating filesystem entry table...",
        "Copying filesystem bitmap...",
        "Copying system files...",
        "Finishing installation..."
    };

    fill_rect(0, 0, WIDTH, HEIGHT, 0x00101824);
    fill_rect(0, 0, WIDTH, 70, 0x00202735);
    draw_string(32, 25, "NyteOS Setup", ORANGE, FONT_DEFAULT);

    if (phase == 99)
    {
        draw_string(40, 210, "Installation failed.", RED, FONT_DEFAULT);
        draw_string(40, 245, "Check the selected virtual disk and try again.", WHITE, FONT_DEFAULT);
        draw_string(40, 568, "Press a key to return to disk selection.", LIGHT_GRAY, FONT_DEFAULT);
        swap_buffers();
        return;
    }

    if (phase >= sizeof(phase_names) / sizeof(phase_names[0]))
        phase = 0;

    draw_string(40, 190, "Installing NyteOS. Please wait...", WHITE, FONT_DEFAULT);
    draw_string(40, 225, phase_names[phase], LIGHT_GRAY, FONT_DEFAULT);
    draw_string(40, 255, "This can take a while. Do not shutdown the computer.", LIGHT_GRAY, FONT_DEFAULT);

    fill_rect(40, 315, 720, 24, 0x003A4658);
    int position = step % 100;
    if (position > 50)
        position = 100 - position;
    fill_rect(48 + position * 11, 319, 110, 16, ORANGE);

    draw_string(40, 365, "The disk is being written and verified.", WHITE, FONT_DEFAULT);
    swap_buffers();
}

void installer_ui(void)
{
    static const uint16_t ports[] = {0x1F0, 0x170};
    static const uint8_t drives[] = {0xE0, 0xF0};

    uint64_t sizes[4] = {0};
    int selected = 0;
    int count = 0;

    framebuffer_init();

    if (!framebuffer)
        return;

    installer_target_selected = 0;

    uint16_t source_io;
    uint8_t source_drive;

    if (!installer_get_source_disk(&source_io, &source_drive))
        return;

    for (int i = 0; i < 4; i++)
    {
        sizes[i] = ata_get_drive_size_mb(
            ports[i / 2],
            drives[i % 2]
        );

        if (ports[i / 2] == source_io &&
            drives[i % 2] == source_drive)
        {
            sizes[i] = 0;
            continue;
        }

        if (sizes[i])
            count++;
        }

    while (1)
    {
        fill_rect(0, 0, WIDTH, HEIGHT, 0x00101824);

        fill_rect(0, 0, WIDTH, 70, 0x00202735);
        draw_string(32, 25, "NyteOS Setup", ORANGE, FONT_DEFAULT);

        draw_string(32, 110, "Welcome to NyteOS installation.", WHITE, FONT_DEFAULT);
        draw_string(32, 145, "Select the disk where NyteOS will be installed:", LIGHT_GRAY, FONT_DEFAULT);

        int item = 0;

        for (int i = 0; i < 4; i++)
        {
            if (!sizes[i])
                continue;

            int y = 210 + item * 65;

            if (item == selected)
                fill_rect(25, y - 12, 750, 48, 0x003A4658);

            draw_string(
                45,
                y,
                item == selected ? ">" : " ",
                ORANGE,
                FONT_DEFAULT
            );

            draw_string(
                70,
                y,
                i == 0 ? "Primary Master" :
                i == 1 ? "Primary Slave" :
                i == 2 ? "Secondary Master" :
                         "Secondary Slave",
                WHITE,
                FONT_DEFAULT
            );

            char size_text[32];
            char digits[12];

            uint64_t mb = sizes[i];
            uint32_t value;
            const char *unit;

            int digit_count = 0;
            int pos = 0;

            if (mb >= 1024ULL * 1024ULL)
            {
                value = (uint32_t)(mb / (1024ULL * 1024ULL));
                unit = "TB";
            }
            else if (mb >= 1024ULL)
            {
                value = (uint32_t)(mb / 1024ULL);
                unit = "GB";
            }
            else
            {
                value = (uint32_t)mb;
                unit = "MB";
            }

            do
            {
                digits[digit_count++] = '0' + (value % 10U);
                value /= 10U;
            }
            while (value && digit_count < (int)sizeof(digits));

            size_text[pos++] = '(';

            while (digit_count > 0)
                size_text[pos++] = digits[--digit_count];

            size_text[pos++] = ' ';
            size_text[pos++] = unit[0];
            size_text[pos++] = unit[1];
            size_text[pos++] = ')';
            size_text[pos] = '\0';

            draw_string(330, y, size_text, LIGHT_GRAY, FONT_DEFAULT);

            item++;
        }

        if (!count)
            draw_string(32, 220, "No ATA disks detected.", RED, FONT_DEFAULT);

        fill_rect(0, 550, WIDTH, 50, 0x00202735);
        draw_string(32, 568, "UP/DOWN: Select    ENTER: Continue    ESC: Skip", WHITE, FONT_DEFAULT);

        swap_buffers();

        char key;

        while (keyboard_pop(&key))
        {
            if (key == 17 && selected > 0)
                selected--;

            if (key == 18 && selected < count - 1)
                selected++;

            if (key == 27)
                return;

            if (key == '\n' && count > 0)
            {
                int found = 0;

                for (int i = 0; i < 4; i++)
                {
                    if (!sizes[i])
                        continue;

                    if (found == selected)
                    {
                        installer_target_io = ports[i / 2];
                        installer_target_drive = drives[i % 2];
                        installer_target_size = sizes[i];

                        ata_select(
                            installer_target_io,
                            installer_target_drive
                        );

                        if (installer_prepare_disk(
                                installer_target_io,
                                installer_target_drive))
                        {
                            installer_target_selected = 1;
                            return;
                        }

                        installer_progress_update(99, 0);
                    }

                    found++;
                }
            }
        }

        __asm__ volatile ("hlt");
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

    wallpaper_load_setting();

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
        int keyboard_activity = 0;

        while (keyboard_pop(&key))
        {
            keyboard_activity = 1;

            if (files_creating || files_renaming)
            {
                files_create_key(key);
            }
            else
            {
                shell_process_key(key);
            }
        }

        if (keyboard_activity)
            desktop_draw();

        mouse_poll();

        if (mouse_wheel_delta != 0)
        {
            int delta = mouse_wheel_delta;

            mouse_wheel_delta = 0;

            files_scroll(delta);
        }

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
