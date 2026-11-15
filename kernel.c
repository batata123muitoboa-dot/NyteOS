// kernel

#include <stdint.h>
#include <stddef.h>
#include "rtc.h"
#include "pit.h"
#include "heap.h"
#include "speaker.h"

extern void keyboard_stub(void);

extern void exception0(void);
extern void exception1(void);
extern void exception2(void);
extern void exception3(void);
extern void exception4(void);
extern void exception5(void);
extern void exception6(void);
extern void exception7(void);
extern void exception8(void);
extern void exception9(void);
extern void exception10(void);
extern void exception11(void);
extern void exception12(void);
extern void exception13(void);
extern void exception14(void);
extern void exception15(void);
extern void exception16(void);
extern void exception17(void);
extern void exception18(void);
extern void exception19(void);
extern void exception20(void);
extern void exception21(void);
extern void exception22(void);
extern void exception23(void);
extern void exception24(void);
extern void exception25(void);
extern void exception26(void);
extern void exception27(void);
extern void exception28(void);
extern void exception29(void);
extern void exception30(void);
extern void exception31(void);

extern void nyteos_bsod(uint32_t vector, uint32_t error);


#define BOOT_INFO_ADDR 0x8800
#define VIDEO_MEMORY 0xB8000

#define FS_V1_START_SECTOR 130
#define FS_V1_ENTRY_SECTOR 131
#define FS_V1_BITMAP_SECTOR 147
#define FS_V1_DATA_SECTOR 148
#define FS_V2_START_SECTOR 183
#define FS_V2_DATA_SECTOR 184
#define FS_V2_MAX_ENTRIES 16384
#define FS_ENTRY_SIZE 64
#define FS_SECTOR_SIZE 512

volatile uint32_t framebuffer;
volatile uint32_t framebuffer_pitch;
volatile uint32_t framebuffer_bpp;

uint64_t fs_start_lba = FS_V1_START_SECTOR;
uint64_t fs_total_sectors = 0;
uint64_t fs_entry_lba = FS_V1_ENTRY_SECTOR;
uint64_t fs_bitmap_lba = FS_V1_BITMAP_SECTOR;
uint64_t fs_bitmap_sectors = 1;
uint64_t fs_data_start = FS_V1_DATA_SECTOR;
uint64_t fs_data_end = FS_V1_DATA_SECTOR + 512;
uint32_t fs_max_entries = 128;
uint32_t fs_version = 1;

#define ATA_DATA       0x1F0
#define ATA_ERROR      0x1F1
#define ATA_SECTOR_CNT 0x1F2
#define ATA_LBA_LOW    0x1F3
#define ATA_LBA_MID    0x1F4
#define ATA_LBA_HIGH   0x1F5
#define ATA_DRIVE      0x1F6
#define ATA_STATUS     0x1F7
#define ATA_COMMAND    0x1F7

#define ATA_READ       0x20
#define ATA_WRITE      0x30
#define ATA_READ_EXT   0x24
#define ATA_WRITE_EXT  0x34

#define ATA_BSY 0x80
#define ATA_DRQ 0x08
#define ATA_ERR 0x01

void framebuffer_init(void)
{
    volatile uint32_t *boot_info =
        (volatile uint32_t *)BOOT_INFO_ADDR;

    framebuffer = boot_info[0];
    framebuffer_pitch = boot_info[1];
    framebuffer_bpp = boot_info[2];
}

extern int nyteos_is_installed(void);
extern int installer_target_selected;
extern uint16_t installer_target_io;
extern uint8_t installer_target_drive;

/* ============================================================
   VGA (Com suporte a scrolling)
   ============================================================ */

char *video = (char *)VIDEO_MEMORY;
int cursor = 0;

static inline void outb(unsigned short port, unsigned char value)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static inline unsigned char inb(unsigned short port)
{
    unsigned char value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

static inline void outw(unsigned short port, unsigned short value)
{
    __asm__ volatile (
        "outw %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static inline unsigned short inw(unsigned short port)
{
    unsigned short value;

    __asm__ volatile (
        "inw %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}


void update_cursor(void)
{
    unsigned short pos = cursor;

    outb(0x3D4, 0x0F);
    outb(0x3D5, pos & 0xFF);

    outb(0x3D4, 0x0E);
    outb(0x3D5, (pos >> 8) & 0xFF);
}


void scroll(void)
{
    for (int i = 0; i < 24 * 80 * 2; i++)
    {
        video[i] = video[i + 80 * 2];
    }

    for (int i = 24 * 80 * 2; i < 25 * 80 * 2; i += 2)
    {
        video[i] = ' ';
        video[i + 1] = 0x07;
    }

    cursor = 24 * 80;
}


void putchar(char c)
{
    if (c == '\n')
    {
        cursor = ((cursor / 80) + 1) * 80;

        if (cursor >= 80 * 25)
            scroll();

        update_cursor();
        return;
    }

    video[cursor * 2] = c;
    video[cursor * 2 + 1] = 0x07;

    cursor++;

    if (cursor >= 80 * 25)
        scroll();

    update_cursor();
}


void print(const char *str)
{
    while (*str)
        putchar(*str++);
}


void clear_screen(void)
{
    for (int i = 0; i < 80 * 25; i++)
    {
        video[i * 2] = ' ';
        video[i * 2 + 1] = 0x07;
    }

    cursor = 0;
    update_cursor();
}

void print_colored(const char* str, uint8_t color);

/* ============================================================
   IDT & PIC (Interrupts)
   ============================================================ */

struct idt_entry {
    uint16_t base_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t flags;
    uint16_t base_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct idt_entry idt[256];
struct idt_ptr idtp;

#define KEY_BUFFER_SIZE 1024

volatile char key_buffer[KEY_BUFFER_SIZE];
volatile int kb_head = 0;
volatile int kb_tail = 0;

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low = (base & 0xFFFF);
    idt[num].selector = sel;
    idt[num].zero = 0;
    idt[num].flags = flags;
    idt[num].base_high = (base >> 16) & 0xFFFF;
}

void pic_remap(void) {
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);

    outb(0x21, 0xFD); 
    outb(0xA1, 0xFF);
}

static inline void keyboard_push(char key)
{
    int next = (kb_head + 1) % KEY_BUFFER_SIZE;

    if (next == kb_tail)
        kb_tail = (kb_tail + 1) % KEY_BUFFER_SIZE;

    key_buffer[kb_head] = key;
    kb_head = next;
}

int keyboard_pop(char *key)
{
    if (kb_head == kb_tail)
        return 0;

    *key = key_buffer[kb_tail];
    kb_tail = (kb_tail + 1) % KEY_BUFFER_SIZE;

    return 1;
}

volatile int ctrl_down = 0;
volatile int shift_down = 0;
volatile int caps_lock = 0;

void keyboard_handler_main(void)
{
    unsigned char scancode = inb(0x60);
    static int is_e0 = 0;

    if (scancode == 0xE0)
    {
        is_e0 = 1;
        outb(0x20, 0x20);
        return;
    }

    if (scancode == 0x1D)
    {
        ctrl_down = 1;
        is_e0 = 0;
        outb(0x20, 0x20);
        return;
    }

    if (scancode == 0x9D)
    {
        ctrl_down = 0;
        is_e0 = 0;
        outb(0x20, 0x20);
        return;
    }

    if (scancode == 0x2A || scancode == 0x36)
    {
        shift_down = 1;
        is_e0 = 0;
        outb(0x20, 0x20);
        return;
    }

    if (scancode == 0xAA || scancode == 0xB6)
    {
        shift_down = 0;
        is_e0 = 0;
        outb(0x20, 0x20);
        return;
    }

    if (scancode == 0x3A)
    {
        caps_lock = !caps_lock;
        is_e0 = 0;
        outb(0x20, 0x20);
        return;
    }

    if (scancode & 0x80)
    {
        is_e0 = 0;
        outb(0x20, 0x20);
        return;
    }

    if (is_e0)
    {
        is_e0 = 0;

        if (scancode == 0x48)
        {
            keyboard_push(17);
        }
        else if (scancode == 0x50)
        {
            keyboard_push(18);
        }

        outb(0x20, 0x20);
        return;
    }

    static const char keymap[128] = {
        0, 27,
        '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
        '-', '=', '\b',
        '\t',
        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',
        '[', ']', '\n',
        0,
        'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l',
        ';', '\'', '`',
        0,
        '\\',
        'z', 'x', 'c', 'v', 'b', 'n', 'm',
        ',', '.', '/',
        0,
        '*',
        0,
        ' '
    };

    if (scancode < 128)
    {
        char c = keymap[scancode];

        if (c)
        {
            if (ctrl_down)
            {
                if (c == 's')
                    keyboard_push(19);
                else if (c == 'c')
                    keyboard_push(3);
                else if (c == 'x')
                    keyboard_push(24);
                else
                    keyboard_push(c);
            }
            else
            {
                int upper = 0;

                if (c >= 'a' && c <= 'z')
                {
                    upper = shift_down ^ caps_lock;
                }

                if (upper)
                    c -= 'a' - 'A';

                keyboard_push(c);
            }
        }
    }

    outb(0x20, 0x20);
}

void default_interrupt_handler(void) {
    print("Unhandled Interrupt!\n");
}

void exception_handler(uint32_t vector, uint32_t error)
{
    __asm__ volatile ("cli");

    nyteos_bsod(vector, error);

    while (1)
        __asm__ volatile ("hlt");
}

static struct idt_ptr idtp_val;

static int ps2_wait_input(void)
{
    for (int i = 0; i < 100000; i++)
        if (!(inb(0x64) & 2))
            return 1;

    return 0;
}

static int ps2_wait_output(void)
{
    for (int i = 0; i < 100000; i++)
        if (inb(0x64) & 1)
            return 1;

    return 0;
}

static void keyboard_controller_init(void)
{
    ps2_wait_input();
    outb(0x64, 0xAD);

    while (inb(0x64) & 1)
        inb(0x60);

    if (!ps2_wait_input())
        return;

    outb(0x64, 0x20);

    if (!ps2_wait_output())
        return;

    uint8_t config = inb(0x60);

    config |= 0x01;
    config &= ~0x10;

    if (!ps2_wait_input())
        return;

    outb(0x64, 0x60);

    if (!ps2_wait_input())
        return;

    outb(0x60, config);

    if (!ps2_wait_input())
        return;

    outb(0x64, 0xAE);

    if (!ps2_wait_input())
        return;

    outb(0x60, 0xF4);

    if (ps2_wait_output())
        inb(0x60);
}

void init_idt(void)
{
    for (int i = 0; i < 256; i++)
        idt_set_gate(i, 0, 0, 0);

    keyboard_controller_init();

    pic_remap();

    idt_set_gate(0x20, (uint32_t)0, 0x08, 0x8E);
    idt_set_gate(0x21, (uint32_t)keyboard_stub, 0x08, 0x8E);

    idt_set_gate(0,  (uint32_t)exception0,  0x08, 0x8E);
    idt_set_gate(1,  (uint32_t)exception1,  0x08, 0x8E);
    idt_set_gate(2,  (uint32_t)exception2,  0x08, 0x8E);
    idt_set_gate(3,  (uint32_t)exception3, 0x08, 0x8E);
    idt_set_gate(4,  (uint32_t)exception4, 0x08, 0x8E);
    idt_set_gate(5,  (uint32_t)exception5, 0x08, 0x8E);
    idt_set_gate(6,  (uint32_t)exception6, 0x08, 0x8E);
    idt_set_gate(7,  (uint32_t)exception7, 0x08, 0x8E);
    idt_set_gate(8,  (uint32_t)exception8, 0x08, 0x8E);
    idt_set_gate(9,  (uint32_t)exception9, 0x08, 0x8E);
    idt_set_gate(10, (uint32_t)exception10, 0x08, 0x8E);
    idt_set_gate(11, (uint32_t)exception11, 0x08, 0x8E);
    idt_set_gate(12, (uint32_t)exception12, 0x08, 0x8E);
    idt_set_gate(13, (uint32_t)exception13, 0x08, 0x8E);
    idt_set_gate(14, (uint32_t)exception14, 0x08, 0x8E);
    idt_set_gate(15, (uint32_t)exception15, 0x08, 0x8E);
    idt_set_gate(16, (uint32_t)exception16, 0x08, 0x8E);
    idt_set_gate(17, (uint32_t)exception17, 0x08, 0x8E);
    idt_set_gate(18, (uint32_t)exception18, 0x08, 0x8E);
    idt_set_gate(19, (uint32_t)exception19, 0x08, 0x8E);
    idt_set_gate(20, (uint32_t)exception20, 0x08, 0x8E);
    idt_set_gate(21, (uint32_t)exception21, 0x08, 0x8E);
    idt_set_gate(22, (uint32_t)exception22, 0x08, 0x8E);
    idt_set_gate(23, (uint32_t)exception23, 0x08, 0x8E);
    idt_set_gate(24, (uint32_t)exception24, 0x08, 0x8E);
    idt_set_gate(25, (uint32_t)exception25, 0x08, 0x8E);
    idt_set_gate(26, (uint32_t)exception26, 0x08, 0x8E);
    idt_set_gate(27, (uint32_t)exception27, 0x08, 0x8E);
    idt_set_gate(28, (uint32_t)exception28, 0x08, 0x8E);
    idt_set_gate(29, (uint32_t)exception29, 0x08, 0x8E);
    idt_set_gate(30, (uint32_t)exception30, 0x08, 0x8E);
    idt_set_gate(31, (uint32_t)exception31, 0x08, 0x8E);

    idtp_val.limit = sizeof(idt) - 1;
    idtp_val.base = (uint32_t)&idt;

    __asm__ volatile ("lidt (%0)" : : "r"(&idtp_val));
}

/* ============================================================
   ATA PIO
   ============================================================ */

#define ATA_PRIMARY 0x1F0
#define ATA_SECONDARY 0x170

#define ATA_READ  0x20
#define ATA_WRITE 0x30
#define ATA_IDENTIFY 0xEC

#define ATA_BSY 0x80
#define ATA_DRQ 0x08
#define ATA_ERR 0x01

static uint16_t ata_io = ATA_PRIMARY;
static uint8_t current_drive = 0xE0;

#define LIVE_RAMDISK_SECTORS 8192

static unsigned char *live_ramdisk;
static uint64_t fs_storage_sectors;
static uint16_t fs_device_io;
static uint8_t fs_device_drive;
static int fs_live_mode;
static int fs_storage_ready;

static inline void io_wait(void)
{
    for (int i = 0; i < 16; i++)
        inb(0x80);
}

void ata_select(uint16_t io, uint8_t drive)
{
    ata_io = io;
    current_drive = drive;

    outb(ata_io + 6, drive);
    io_wait();
}

static int ata_wait_bsy(void)
{
    for (int i = 0; i < 100000; i++)
    {
        uint8_t status = inb(ata_io + 7);

        if (!(status & ATA_BSY))
            return 1;
    }

    return 0;
}

static int ata_wait_drq(void)
{
    for (int i = 0; i < 100000; i++)
    {
        uint8_t status = inb(ata_io + 7);

        if (status & ATA_ERR)
            return 0;

        if (!(status & ATA_BSY) && (status & ATA_DRQ))
            return 1;
    }

    return 0;
}

int ata_read_sector(uint64_t lba, unsigned char *buffer)
{
    if (lba >= (1ULL << 48))
        return 0;

    ata_select(ata_io, current_drive);

    if (!ata_wait_bsy())
        return 0;

    if (lba <= 0x0FFFFFFFULL)
    {
        outb(ata_io + 6, current_drive | ((lba >> 24) & 0x0F));
        outb(ata_io + 2, 1);
        outb(ata_io + 3, lba & 0xFF);
        outb(ata_io + 4, (lba >> 8) & 0xFF);
        outb(ata_io + 5, (lba >> 16) & 0xFF);
        outb(ata_io + 7, ATA_READ);
    }
    else
    {
        outb(ata_io + 6, current_drive);
        outb(ata_io + 2, 0);
        outb(ata_io + 3, (lba >> 24) & 0xFF);
        outb(ata_io + 4, (lba >> 32) & 0xFF);
        outb(ata_io + 5, (lba >> 40) & 0xFF);
        outb(ata_io + 2, 1);
        outb(ata_io + 3, lba & 0xFF);
        outb(ata_io + 4, (lba >> 8) & 0xFF);
        outb(ata_io + 5, (lba >> 16) & 0xFF);
        outb(ata_io + 7, ATA_READ_EXT);
    }

    if (!ata_wait_drq())
        return 0;

    for (int i = 0; i < 256; i++)
    {
        uint16_t value = inw(ata_io);
        buffer[i * 2] = value & 0xFF;
        buffer[i * 2 + 1] = value >> 8;
    }

    return 1;
}

int ata_write_sector(uint64_t lba, unsigned char *buffer)
{
    if (lba >= (1ULL << 48))
        return 0;

    ata_select(ata_io, current_drive);

    if (!ata_wait_bsy())
        return 0;

    if (lba <= 0x0FFFFFFFULL)
    {
        outb(ata_io + 6, current_drive | ((lba >> 24) & 0x0F));
        outb(ata_io + 2, 1);
        outb(ata_io + 3, lba & 0xFF);
        outb(ata_io + 4, (lba >> 8) & 0xFF);
        outb(ata_io + 5, (lba >> 16) & 0xFF);
        outb(ata_io + 7, ATA_WRITE);
    }
    else
    {
        outb(ata_io + 6, current_drive);
        outb(ata_io + 2, 0);
        outb(ata_io + 3, (lba >> 24) & 0xFF);
        outb(ata_io + 4, (lba >> 32) & 0xFF);
        outb(ata_io + 5, (lba >> 40) & 0xFF);
        outb(ata_io + 2, 1);
        outb(ata_io + 3, lba & 0xFF);
        outb(ata_io + 4, (lba >> 8) & 0xFF);
        outb(ata_io + 5, (lba >> 16) & 0xFF);
        outb(ata_io + 7, ATA_WRITE_EXT);
    }

    if (!ata_wait_drq())
        return 0;

    for (int i = 0; i < 256; i++)
    {
        uint16_t value =
            buffer[i * 2] |
            ((uint16_t)buffer[i * 2 + 1] << 8);

        outw(ata_io, value);
    }

    io_wait();

    return ata_wait_bsy();
}

int fs_read_sector(uint64_t lba, unsigned char *buffer)
{
    if (!fs_storage_ready || lba >= fs_storage_sectors)
        return 0;

    if (fs_live_mode)
    {
        const unsigned char *source = live_ramdisk + lba * FS_SECTOR_SIZE;
        for (int i = 0; i < FS_SECTOR_SIZE; i++)
            buffer[i] = source[i];
        return 1;
    }

    ata_select(fs_device_io, fs_device_drive);
    return ata_read_sector(lba, buffer);
}

int fs_write_sector(uint64_t lba, unsigned char *buffer)
{
    if (!fs_storage_ready || lba >= fs_storage_sectors)
        return 0;

    if (fs_live_mode)
    {
        unsigned char *destination = live_ramdisk + lba * FS_SECTOR_SIZE;
        for (int i = 0; i < FS_SECTOR_SIZE; i++)
            destination[i] = buffer[i];
        return 1;
    }

    ata_select(fs_device_io, fs_device_drive);
    return ata_write_sector(lba, buffer);
}

void print_number(int n);

static int ata_identify(uint16_t io, uint8_t drive, uint16_t identify[256])
{
    uint16_t previous_io = ata_io;
    uint8_t previous_drive = current_drive;
    uint8_t status = 0;
    int ready = 0;

    ata_select(io, drive);

    if (!ata_wait_bsy())
        goto done;

    outb(io + 2, 0);
    outb(io + 3, 0);
    outb(io + 4, 0);
    outb(io + 5, 0);
    outb(io + 7, ATA_IDENTIFY);

    for (int i = 0; i < 100000; i++)
    {
        status = inb(io + 7);

        if (status == 0 || status == 0xFF || (status & ATA_ERR))
            goto done;

        if (!(status & ATA_BSY) && (status & ATA_DRQ))
        {
            ready = 1;
            break;
        }
    }

    if (!ready)
        goto done;

    if (inb(io + 4) != 0 || inb(io + 5) != 0)
    {
        ready = 0;
        goto done;
    }

    for (int i = 0; i < 256; i++)
        identify[i] = inw(io);

done:
    ata_select(previous_io, previous_drive);
    return ready;
}

static uint64_t ata_identify_sector_count(uint16_t identify[256])
{
    uint64_t sectors = 0;

    if ((identify[83] & 0xC000) == 0x4000 &&
        (identify[83] & (1 << 10)))
    {
        sectors =
            ((uint64_t)identify[103] << 48) |
            ((uint64_t)identify[102] << 32) |
            ((uint64_t)identify[101] << 16) |
            (uint64_t)identify[100];
    }

    if (!sectors)
    {
        sectors =
            ((uint32_t)identify[61] << 16) |
            identify[60];
    }

    return sectors;
}

static uint64_t ata_get_drive_sector_count(uint16_t io, uint8_t drive)
{
    uint16_t identify[256];

    if (!ata_identify(io, drive, identify))
        return 0;

    return ata_identify_sector_count(identify);
}

uint64_t ata_get_drive_size_mb(uint16_t io, uint8_t drive)
{
    uint64_t sectors = ata_get_drive_sector_count(io, drive);

    return (sectors * 512ULL) / (1024ULL * 1024ULL);
}

int installer_get_source_disk(uint16_t *io, uint8_t *drive)
{
    static const uint16_t ports[4] = {
        0x1F0, 0x1F0, 0x170, 0x170
    };

    static const uint8_t drives[4] = {
        0xE0, 0xF0, 0xE0, 0xF0
    };

    static const uint64_t marker_sectors[2] = {
        FS_V2_START_SECTOR,
        FS_V1_START_SECTOR
    };
    const unsigned char *bios_marker_sector =
        (const unsigned char *)0x9000;
    unsigned char sector[512];
    uint16_t previous_io = ata_io;
    uint8_t previous_drive = current_drive;
    int matches = 0;
    uint64_t source_start = 0;

    for (int candidate = 0; candidate < 4; candidate++)
    {
        int match = 1;

        ata_select(ports[candidate], drives[candidate]);

        for (int marker = 0; marker < 2; marker++)
        {
            if (!ata_read_sector(marker_sectors[marker], sector))
                continue;

            int match = 1;
            for (int i = 0; i < 512; i++)
            {
                if (sector[i] != bios_marker_sector[i])
                {
                    match = 0;
                    break;
                }
            }

            if (!match)
                continue;

            *io = ports[candidate];
            *drive = drives[candidate];
            source_start = marker_sectors[marker];
            matches++;
        }
    }

    ata_select(previous_io, previous_drive);
    if (matches != 1)
        return 0;

    fs_start_lba = source_start;
    return 1;
}

int nyteos_is_installed(void)
{
    return *(volatile uint32_t *)(BOOT_INFO_ADDR + 20) == 1;
}

static uint32_t fs_get_u32(const unsigned char *data);
static uint64_t fs_get_u64(const unsigned char *data);
static void fs_put_u32(unsigned char *data, uint32_t value);
static void fs_put_u64(unsigned char *data, uint64_t value);
extern void installer_progress_update(uint32_t phase, uint32_t step);
static int installer_format_target(
    uint16_t source_io,
    uint8_t source_drive,
    uint16_t target_io,
    uint8_t target_drive,
    uint64_t source_start,
    uint64_t total_sectors,
    const unsigned char *source_superblock
);

int installer_prepare_disk(uint16_t io, uint8_t drive)
{
    uint16_t source_io;
    uint8_t source_drive;

    unsigned char sector[512];
    unsigned char verify[512];

    if (!installer_get_source_disk(&source_io, &source_drive))
        return 0;

    if (io == source_io && drive == source_drive)
        return 0;

    uint64_t target_sectors = ata_get_drive_sector_count(io, drive);
    if (!target_sectors)
        return 0;

    ata_select(source_io, source_drive);

    if (!ata_read_sector(fs_start_lba, sector))
        return 0;

    if (fs_get_u32(sector + 8) != 2 ||
        sector[0] != 'N' || sector[1] != 'Y' ||
        sector[2] != 'T' || sector[3] != 'E' ||
        sector[4] != 'F' || sector[5] != 'S' ||
        sector[6] != '0' || sector[7] != '1' ||
        sector[32] != 'N' || sector[36] != 'S')
        return 0;

    uint64_t source_sectors =
        fs_get_u32(sector + 12) |
        ((uint64_t)fs_get_u32(sector + 40) << 32);

    if (!source_sectors || source_sectors > target_sectors)
        return 0;

    unsigned char source_superblock[512];
    for (int i = 0; i < 512; i++)
        source_superblock[i] = sector[i];

    installer_progress_update(1, 0);
    for (uint64_t lba = 0; lba < source_sectors; lba++)
    {
        ata_select(source_io, source_drive);

        if (!ata_read_sector(lba, sector))
            return 0;

        ata_select(io, drive);

        if (!ata_write_sector(lba, sector))
            return 0;

        if (!ata_read_sector(lba, verify))
            return 0;

        for (int i = 0; i < 512; i++)
        {
            if (sector[i] != verify[i])
                return 0;
        }

        if ((lba & 255U) == 255U)
            installer_progress_update(1, (uint32_t)(lba >> 8));
    }

    installer_progress_update(2, 0);

    return installer_format_target(
        source_io,
        source_drive,
        io,
        drive,
        fs_start_lba,
        target_sectors,
        source_superblock
    );
}

/* ============================================================
   NyteFS
   ============================================================ */

struct fs_entry
{
    char name[32];

    uint64_t size;
    uint64_t start_sector;
    uint64_t sector_count;
    unsigned int parent;

    unsigned char type;
    unsigned char used;

    unsigned char reserved[2];
};

_Static_assert(sizeof(struct fs_entry) == FS_ENTRY_SIZE,
               "NyteFS entry structure must stay 64 bytes");

#define FS_FILE 1
#define FS_DIR  2

unsigned char fs_sector[512];
unsigned char fs_bitmap[512];
#define KERNEL_FILE_BUFFER_SIZE 16384
static unsigned char kernel_file_buffer[KERNEL_FILE_BUFFER_SIZE + 1];
static uint64_t fs_entry_cache_lba = (uint64_t)-1;
static unsigned char fs_entry_cache[FS_SECTOR_SIZE];

static uint32_t fs_get_u32(const unsigned char *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static uint64_t fs_get_u64(const unsigned char *data)
{
    return (uint64_t)fs_get_u32(data) |
           ((uint64_t)fs_get_u32(data + 4) << 32);
}

static void fs_put_u32(unsigned char *data, uint32_t value)
{
    data[0] = value & 0xFF;
    data[1] = (value >> 8) & 0xFF;
    data[2] = (value >> 16) & 0xFF;
    data[3] = (value >> 24) & 0xFF;
}

static void fs_put_u64(unsigned char *data, uint64_t value)
{
    fs_put_u32(data, (uint32_t)value);
    fs_put_u32(data + 4, (uint32_t)(value >> 32));
}

static int installer_format_target(
    uint16_t source_io,
    uint8_t source_drive,
    uint16_t target_io,
    uint8_t target_drive,
    uint64_t source_start,
    uint64_t total_sectors,
    const unsigned char *source_superblock
)
{
    uint64_t source_entry_lba =
        fs_get_u32(source_superblock + 16) |
        ((uint64_t)fs_get_u32(source_superblock + 44) << 32);
    uint64_t source_bitmap_lba =
        fs_get_u32(source_superblock + 20) |
        ((uint64_t)fs_get_u32(source_superblock + 48) << 32);
    uint64_t source_bitmap_sectors = fs_get_u64(source_superblock + 52);
    uint64_t source_entry_sectors = fs_get_u32(source_superblock + 60);
    uint64_t entry_sectors =
        ((uint64_t)FS_V2_MAX_ENTRIES * FS_ENTRY_SIZE + FS_SECTOR_SIZE - 1) /
        FS_SECTOR_SIZE;
    uint64_t data_start = fs_get_u32(source_superblock + 24);

    uint64_t source_total_sectors =
        fs_get_u32(source_superblock + 12) |
        ((uint64_t)fs_get_u32(source_superblock + 40) << 32);

    if (fs_get_u32(source_superblock + 8) != 2 ||
        fs_get_u32(source_superblock + 28) > FS_V2_MAX_ENTRIES ||
        source_entry_sectors > entry_sectors ||
        source_entry_lba > source_total_sectors ||
        source_entry_sectors > source_total_sectors - source_entry_lba ||
        source_bitmap_lba > source_entry_lba ||
        source_bitmap_sectors > source_entry_lba - source_bitmap_lba ||
        data_start < source_start + 1 ||
        total_sectors <= data_start + entry_sectors)
        return 0;

    uint64_t bitmap_sectors =
        (total_sectors - data_start - entry_sectors + 4095) / 4096;
    uint64_t bitmap_lba = total_sectors - entry_sectors - bitmap_sectors;
    uint64_t entry_lba = bitmap_lba + bitmap_sectors;

    if (!bitmap_sectors || bitmap_sectors < source_bitmap_sectors ||
        bitmap_lba <= data_start || entry_lba + entry_sectors > total_sectors)
        return 0;

    unsigned char zero_sector[FS_SECTOR_SIZE] = {0};
    unsigned char buffer[FS_SECTOR_SIZE];

    ata_select(target_io, target_drive);
    installer_progress_update(2, 0);
    for (uint64_t i = 0; i < bitmap_sectors; i++)
    {
        if (!ata_write_sector(bitmap_lba + i, zero_sector))
            return 0;
        if ((i & 255U) == 255U)
            installer_progress_update(2, (uint32_t)(i >> 8));
    }

    installer_progress_update(3, 0);
    for (uint64_t i = 0; i < entry_sectors; i++)
    {
        if (!ata_write_sector(entry_lba + i, zero_sector))
            return 0;
        if ((i & 255U) == 255U)
            installer_progress_update(3, (uint32_t)(i >> 8));
    }

    installer_progress_update(4, 0);
    for (uint64_t i = 0; i < source_bitmap_sectors; i++)
    {
        ata_select(source_io, source_drive);
        if (!ata_read_sector(source_bitmap_lba + i, buffer))
            return 0;

        ata_select(target_io, target_drive);
        if (!ata_write_sector(bitmap_lba + i, buffer))
            return 0;
        if ((i & 255U) == 255U)
            installer_progress_update(4, (uint32_t)(i >> 8));
    }

    installer_progress_update(5, 0);
    for (uint64_t i = 0; i < source_entry_sectors; i++)
    {
        ata_select(source_io, source_drive);
        if (!ata_read_sector(source_entry_lba + i, buffer))
            return 0;

        ata_select(target_io, target_drive);
        if (!ata_write_sector(entry_lba + i, buffer))
            return 0;
        if ((i & 255U) == 255U)
            installer_progress_update(5, (uint32_t)(i >> 8));
    }

    unsigned char superblock[FS_SECTOR_SIZE];
    for (int i = 0; i < FS_SECTOR_SIZE; i++)
        superblock[i] = source_superblock[i];

    fs_put_u32(superblock + 8, 2);
    fs_put_u32(superblock + 12, (uint32_t)total_sectors);
    fs_put_u32(superblock + 16, (uint32_t)entry_lba);
    fs_put_u32(superblock + 20, (uint32_t)bitmap_lba);
    fs_put_u32(superblock + 24, (uint32_t)data_start);
    fs_put_u32(superblock + 28, FS_V2_MAX_ENTRIES);
    fs_put_u32(superblock + 40, (uint32_t)(total_sectors >> 32));
    fs_put_u32(superblock + 44, (uint32_t)(entry_lba >> 32));
    fs_put_u32(superblock + 48, (uint32_t)(bitmap_lba >> 32));
    fs_put_u64(superblock + 52, bitmap_sectors);
    fs_put_u32(superblock + 60, (uint32_t)entry_sectors);

    superblock[32] = 'N';
    superblock[33] = 'Y';
    superblock[34] = 'T';
    superblock[35] = 'E';
    superblock[36] = 'O';
    superblock[37] = 'S';
    superblock[38] = '0';
    superblock[39] = '1';

    ata_select(target_io, target_drive);
    if (!ata_write_sector(source_start, superblock))
        return 0;

    installer_progress_update(6, 0);
    return 1;
}

int fs_read_entry(int index, struct fs_entry *entry)
{
    if (index < 0 || (uint32_t)index >= fs_max_entries)
        return 0;

    uint64_t sector =
        fs_entry_lba +
        ((uint64_t)index * FS_ENTRY_SIZE) / FS_SECTOR_SIZE;
    unsigned int offset =
        ((unsigned int)index * FS_ENTRY_SIZE) % FS_SECTOR_SIZE;

    if (sector != fs_entry_cache_lba)
    {
        if (!fs_read_sector(sector, fs_entry_cache))
            return 0;
        fs_entry_cache_lba = sector;
    }

    const unsigned char *raw = fs_entry_cache + offset;

    for (int i = 0; i < 32; i++)
        entry->name[i] = raw[i];

    if (fs_version == 1)
    {
        entry->size = fs_get_u32(raw + 32);
        entry->start_sector = fs_get_u32(raw + 36);
        entry->sector_count = fs_get_u32(raw + 40);
        entry->parent = fs_get_u32(raw + 44);
        entry->type = raw[48];
        entry->used = raw[49];
    }
    else
    {
        entry->size = fs_get_u64(raw + 32);
        entry->start_sector = fs_get_u64(raw + 40);
        entry->sector_count = fs_get_u64(raw + 48);
        entry->parent = fs_get_u32(raw + 56);
        entry->type = raw[60];
        entry->used = raw[61];
        entry->reserved[0] = raw[62];
        entry->reserved[1] = raw[63];
    }

    return 1;
}

int fs_write_entry(int index, struct fs_entry *entry)
{
    if (index < 0 || (uint32_t)index >= fs_max_entries)
        return 0;

    uint64_t sector =
        fs_entry_lba +
        ((uint64_t)index * FS_ENTRY_SIZE) / FS_SECTOR_SIZE;
    unsigned int offset =
        ((unsigned int)index * FS_ENTRY_SIZE) % FS_SECTOR_SIZE;

    if (sector != fs_entry_cache_lba)
    {
        if (!fs_read_sector(sector, fs_entry_cache))
            return 0;
        fs_entry_cache_lba = sector;
    }

    unsigned char *raw = fs_entry_cache + offset;
    for (int i = 0; i < 32; i++)
        raw[i] = entry->name[i];

    if (fs_version == 1)
    {
        if (entry->size > 0xFFFFFFFFULL ||
            entry->start_sector > 0xFFFFFFFFULL ||
            entry->sector_count > 0xFFFFFFFFULL)
            return 0;

        fs_put_u32(raw + 32, (uint32_t)entry->size);
        fs_put_u32(raw + 36, (uint32_t)entry->start_sector);
        fs_put_u32(raw + 40, (uint32_t)entry->sector_count);
        fs_put_u32(raw + 44, entry->parent);
        raw[48] = entry->type;
        raw[49] = entry->used;
    }
    else
    {
        fs_put_u64(raw + 32, entry->size);
        fs_put_u64(raw + 40, entry->start_sector);
        fs_put_u64(raw + 48, entry->sector_count);
        fs_put_u32(raw + 56, entry->parent);
        raw[60] = entry->type;
        raw[61] = entry->used;
        raw[62] = entry->reserved[0];
        raw[63] = entry->reserved[1];
    }

    if (!fs_write_sector(sector, fs_entry_cache))
    {
        fs_entry_cache_lba = (uint64_t)-1;
        return 0;
    }

    return 1;
}

int fs_find_free_entry(void)
{
    struct fs_entry entry;

    for (uint32_t i = 0; i < fs_max_entries; i++)
    {
        if (!fs_read_entry((int)i, &entry))
            return -1;
        if (!entry.used)
            return (int)i;
    }

    return -1;
}

static int fs_set_data_range(uint64_t start_sector,
                             uint64_t sector_count,
                             int allocated)
{
    if (!sector_count)
        return 1;

    if (start_sector < fs_data_start ||
        start_sector >= fs_data_end ||
        sector_count > fs_data_end - start_sector)
        return 0;

    if (fs_version == 1)
    {
        if (!fs_read_sector(fs_bitmap_lba, fs_bitmap))
            return 0;

        uint64_t relative = start_sector - fs_data_start;
        if (relative + sector_count > 512)
            return 0;
        for (uint64_t i = 0; i < sector_count; i++)
            fs_bitmap[relative + i] = allocated ? 1 : 0;
        return fs_write_sector(fs_bitmap_lba, fs_bitmap);
    }

    uint64_t relative_start = start_sector - fs_data_start;
    uint64_t relative_end = relative_start + sector_count;
    uint64_t first_map = relative_start / (FS_SECTOR_SIZE * 8);
    uint64_t last_map = (relative_end - 1) / (FS_SECTOR_SIZE * 8);

    for (uint64_t map_sector = first_map;
         map_sector <= last_map;
         map_sector++)
    {
        if (!fs_read_sector(fs_bitmap_lba + map_sector, fs_bitmap))
            return 0;

        uint64_t map_start = map_sector * FS_SECTOR_SIZE * 8;
        uint64_t begin = relative_start > map_start
            ? relative_start : map_start;
        uint64_t map_end = map_start + FS_SECTOR_SIZE * 8;
        uint64_t end = relative_end < map_end
            ? relative_end : map_end;

        for (uint64_t relative = begin; relative < end; relative++)
        {
            unsigned int byte = (relative / 8) % FS_SECTOR_SIZE;
            unsigned char mask = 1U << (relative % 8);
            if (allocated)
                fs_bitmap[byte] |= mask;
            else
                fs_bitmap[byte] &= (unsigned char)~mask;
        }

        if (!fs_write_sector(fs_bitmap_lba + map_sector, fs_bitmap))
            return 0;
    }

    return 1;
}

static uint64_t fs_allocate_data_run(uint64_t requested)
{
    if (!requested)
        return 0;

    uint64_t run_start = 0;
    uint64_t run_length = 0;

    if (fs_version == 1)
    {
        if (!fs_read_sector(fs_bitmap_lba, fs_bitmap))
            return (uint64_t)-1;

        uint64_t limit = fs_data_end - fs_data_start;
        if (limit > 512)
            limit = 512;

        for (uint64_t i = 0; i < limit; i++)
        {
            if (!fs_bitmap[i])
            {
                if (!run_length)
                    run_start = fs_data_start + i;
                run_length++;
                if (run_length == requested)
                    break;
            }
            else
            {
                run_length = 0;
            }
        }
    }
    else
    {
        for (uint64_t map_sector = 0;
             map_sector < fs_bitmap_sectors && run_length < requested;
             map_sector++)
        {
            if (!fs_read_sector(fs_bitmap_lba + map_sector, fs_bitmap))
                return (uint64_t)-1;

            for (uint64_t byte = 0;
                 byte < FS_SECTOR_SIZE && run_length < requested;
                 byte++)
            {
                for (int bit = 0; bit < 8 && run_length < requested; bit++)
                {
                    uint64_t relative =
                        map_sector * FS_SECTOR_SIZE * 8 + byte * 8 + bit;
                    uint64_t sector = fs_data_start + relative;

                    if (sector >= fs_data_end)
                        break;

                    if (!(fs_bitmap[byte] & (1U << bit)))
                    {
                        if (!run_length)
                            run_start = sector;
                        run_length++;
                    }
                    else
                    {
                        run_length = 0;
                    }
                }
            }
        }
    }

    if (run_length != requested)
        return (uint64_t)-1;

    if (!fs_set_data_range(run_start, requested, 1))
        return (uint64_t)-1;

    return run_start;
}

uint64_t fs_find_free_data_sector(void)
{
    return fs_allocate_data_run(1);
}

int fs_set_data_sector(uint64_t sector, int allocated)
{
    if (sector < fs_data_start || sector >= fs_data_end)
        return 0;

    uint64_t relative = sector - fs_data_start;

    if (fs_version == 1)
    {
        if (relative >= 512 || !fs_read_sector(fs_bitmap_lba, fs_bitmap))
            return 0;

        fs_bitmap[relative] = allocated ? 1 : 0;
        return fs_write_sector(fs_bitmap_lba, fs_bitmap);
    }

    uint64_t map_sector = relative / (FS_SECTOR_SIZE * 8);
    unsigned int byte = (relative / 8) % FS_SECTOR_SIZE;
    unsigned char mask = 1U << (relative % 8);

    if (!fs_read_sector(fs_bitmap_lba + map_sector, fs_bitmap))
        return 0;

    if (allocated)
        fs_bitmap[byte] |= mask;
    else
        fs_bitmap[byte] &= (unsigned char)~mask;

    return fs_write_sector(fs_bitmap_lba + map_sector, fs_bitmap);
}

int fs_free_data_range(uint64_t start_sector, uint64_t sector_count)
{
    return fs_set_data_range(start_sector, sector_count, 0);
}

int fs_read_file(struct fs_entry *entry,
                 unsigned char *buffer,
                 uint64_t capacity)
{
    if (!entry || !buffer || entry->type != FS_FILE ||
        entry->size > capacity)
        return 0;

    uint64_t required = (entry->size + FS_SECTOR_SIZE - 1) /
                        FS_SECTOR_SIZE;
    if (required > entry->sector_count ||
        (required && (entry->start_sector < fs_data_start ||
                      entry->start_sector >= fs_data_end ||
                      required > fs_data_end - entry->start_sector)))
        return 0;

    uint64_t copied = 0;
    for (uint64_t i = 0; i < required; i++)
    {
        if (!fs_read_sector(entry->start_sector + i, fs_sector))
            return 0;

        uint64_t remaining = entry->size - copied;
        uint64_t amount = remaining > FS_SECTOR_SIZE
            ? FS_SECTOR_SIZE : remaining;

        for (uint64_t j = 0; j < amount; j++)
            buffer[copied + j] = fs_sector[j];

        copied += amount;
    }

    return 1;
}

int fs_write_file(int index,
                  struct fs_entry *entry,
                  const unsigned char *buffer,
                  uint64_t size)
{
    if (!entry || (!buffer && size) || entry->type != FS_FILE)
        return 0;

    uint64_t required = (size + FS_SECTOR_SIZE - 1) / FS_SECTOR_SIZE;
    uint64_t old_start = entry->start_sector;
    uint64_t old_count = entry->sector_count;
    uint64_t write_start = old_start;
    int moved = 0;

    if (required > old_count)
    {
        write_start = fs_allocate_data_run(required);
        if (write_start == (uint64_t)-1)
            return 0;
        moved = 1;
    }

    if (required)
    {
        uint64_t copied = 0;
        for (uint64_t i = 0; i < required; i++)
        {
            uint64_t remaining = size - copied;
            uint64_t amount = remaining > FS_SECTOR_SIZE
                ? FS_SECTOR_SIZE : remaining;

            for (uint64_t j = 0; j < FS_SECTOR_SIZE; j++)
                fs_sector[j] = j < amount ? buffer[copied + j] : 0;

            if (!fs_write_sector(write_start + i, fs_sector))
            {
                if (moved)
                    fs_set_data_range(write_start, required, 0);
                return 0;
            }

            copied += amount;
        }
    }

    struct fs_entry updated = *entry;
    updated.size = size;
    updated.start_sector = required ? write_start : 0;
    updated.sector_count = required;

    if (!fs_write_entry(index, &updated))
    {
        if (moved)
            fs_set_data_range(write_start, required, 0);
        return 0;
    }

    if (old_count && (moved || required < old_count))
    {
        uint64_t free_start = moved ? old_start : old_start + required;
        uint64_t free_count = moved ? old_count : old_count - required;
        if (!fs_set_data_range(free_start, free_count, 0))
            return 0;
    }

    *entry = updated;
    return 1;
}

static int fs_mount(void)
{
    unsigned char superblock[FS_SECTOR_SIZE];

    if (!fs_read_sector(fs_start_lba, superblock))
        return 0;

    if (superblock[0] != 'N' || superblock[1] != 'Y' ||
        superblock[2] != 'T' || superblock[3] != 'E' ||
        superblock[4] != 'F' || superblock[5] != 'S' ||
        superblock[6] != '0' || superblock[7] != '1')
        return 0;

    fs_version = fs_get_u32(superblock + 8);

    if (fs_version == 1)
    {
        fs_total_sectors = fs_get_u32(superblock + 12);
        fs_entry_lba = FS_V1_ENTRY_SECTOR;
        fs_bitmap_lba = FS_V1_BITMAP_SECTOR;
        fs_bitmap_sectors = 1;
        fs_data_start = FS_V1_DATA_SECTOR;
        fs_data_end = FS_V1_DATA_SECTOR + 512;
        fs_max_entries = 128;
    }
    else if (fs_version == 2)
    {
        fs_total_sectors = fs_get_u32(superblock + 12) |
                           ((uint64_t)fs_get_u32(superblock + 40) << 32);
        fs_entry_lba = fs_get_u32(superblock + 16) |
                       ((uint64_t)fs_get_u32(superblock + 44) << 32);
        fs_bitmap_lba = fs_get_u32(superblock + 20) |
                        ((uint64_t)fs_get_u32(superblock + 48) << 32);
        fs_data_start = fs_get_u32(superblock + 24);
        fs_max_entries = fs_get_u32(superblock + 28);
        fs_bitmap_sectors = fs_get_u64(superblock + 52);
        fs_data_end = fs_bitmap_lba;

        uint64_t entry_sectors = fs_get_u32(superblock + 60);
        uint64_t expected_entry_sectors =
            ((uint64_t)fs_max_entries * FS_ENTRY_SIZE + FS_SECTOR_SIZE - 1) /
            FS_SECTOR_SIZE;

        uint64_t needed_bitmap_sectors =
            (fs_data_end - fs_data_start + 4095) / 4096;

        if (!fs_total_sectors || fs_total_sectors > fs_storage_sectors ||
            !fs_max_entries || fs_max_entries > FS_V2_MAX_ENTRIES ||
            entry_sectors != expected_entry_sectors ||
            fs_entry_lba > fs_total_sectors ||
            entry_sectors > fs_total_sectors - fs_entry_lba ||
            fs_bitmap_lba > fs_entry_lba ||
            fs_bitmap_sectors > fs_entry_lba - fs_bitmap_lba ||
            fs_data_start >= fs_data_end ||
            fs_bitmap_sectors < needed_bitmap_sectors)
            return 0;
    }
    else
    {
        return 0;
    }

    return fs_total_sectors <= fs_storage_sectors &&
           fs_start_lba < fs_total_sectors;
}

static int fs_initialize_storage(void)
{
    uint16_t io;
    uint8_t drive;

    if (!installer_get_source_disk(&io, &drive))
        return 0;

    fs_device_io = io;
    fs_device_drive = drive;
    uint64_t device_sectors = ata_get_drive_sector_count(io, drive);
    unsigned char superblock[FS_SECTOR_SIZE];

    if (!device_sectors)
        return 0;

    ata_select(io, drive);
    if (!ata_read_sector(fs_start_lba, superblock))
        return 0;

    uint32_t version = fs_get_u32(superblock + 8);
    if (version != 1 && version != 2)
        return 0;
    uint64_t image_sectors = version == 2
        ? fs_get_u32(superblock + 12) |
            ((uint64_t)fs_get_u32(superblock + 40) << 32)
        : fs_get_u32(superblock + 12);

    if (!image_sectors || image_sectors > device_sectors)
        return 0;

    fs_live_mode = !nyteos_is_installed();

    if (fs_live_mode)
    {
        if (image_sectors > LIVE_RAMDISK_SECTORS)
            return 0;

        live_ramdisk = kmalloc((size_t)image_sectors * FS_SECTOR_SIZE);
        if (!live_ramdisk)
            return 0;

        for (uint64_t lba = 0; lba < image_sectors; lba++)
        {
            if (!ata_read_sector(lba, fs_sector))
            {
                kfree(live_ramdisk);
                live_ramdisk = 0;
                return 0;
            }

            for (int i = 0; i < FS_SECTOR_SIZE; i++)
                live_ramdisk[lba * FS_SECTOR_SIZE + i] = fs_sector[i];
        }

        fs_storage_sectors = image_sectors;
    }
    else
    {
        fs_storage_sectors = device_sectors;
    }

    fs_storage_ready = 1;
    if (!fs_mount())
    {
        fs_storage_ready = 0;
        if (live_ramdisk)
        {
            kfree(live_ramdisk);
            live_ramdisk = 0;
        }
        return 0;
    }

    return 1;
}

int fs_find(const char *name, unsigned int parent)
{
    struct fs_entry entry;

    for (uint32_t i = 0; i < fs_max_entries; i++)
    {
        if (!fs_read_entry((int)i, &entry))
        {
            continue;
        }

        if (!entry.used)
            continue;

        if (entry.parent != parent)
            continue;

        int same = 1;

        for (int j = 0; j < 32; j++)
        {
            if (entry.name[j] != name[j])
            {
                same = 0;
                break;
            }

            if (name[j] == '\0')
                break;
        }

        if (same)
            return i;
    }

    return -1;
}

void fs_list(unsigned int parent)
{
    struct fs_entry entry;

    for (uint32_t i = 0; i < fs_max_entries; i++)
    {
        if (!fs_read_entry((int)i, &entry))
            return;

        if (!entry.used)
            continue;

        if (entry.parent != parent)
            continue;

        if (entry.type == FS_DIR)
        {
            print_colored("[DIR]  ", 0x0E);
            print_colored(entry.name, 0x0E);
            print_colored("/", 0x0E);
        }
        else
        {
            print_colored("[FILE] ", 0x07);
            print_colored(entry.name, 0x07);
        }

        putchar('\n');
    }
}

void init_nytefs_default(void)
{
    if (fs_find("user", 0) == -1)
    {
        int idx = fs_find_free_entry();
        if (idx != -1)
        {
            struct fs_entry entry;
            for (int i = 0; i < 32; i++) entry.name[i] = 0;
            
            char *name = "user";
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
            entry.parent = 0;
            entry.type = FS_DIR;
            entry.used = 1;

            fs_write_entry(idx, &entry);
        }
    }
}


/* ============================================================
 * RAM USADA
 * ============================================================ */

extern unsigned char _end;

#define KERNEL_LOAD_ADDR 0x10000

static void init_stack_tracker(void)
{
    uint32_t current_esp;

    __asm__ volatile (
        "mov %%esp, %0"
        : "=r"(current_esp)
    );

    volatile uint32_t *p =
        (volatile uint32_t *)0x80000;

    while ((uint32_t)p < current_esp)
    {
        *p = 0xCCCCCCCC;
        p++;
    }
}

unsigned int get_stack_used(void)
{
    volatile uint32_t *p =
        (volatile uint32_t *)0x80000;

    volatile uint32_t *end =
        (volatile uint32_t *)0x90000;

    while (p < end && *p == 0xCCCCCCCC)
        p++;

    return (unsigned int)(
        (uintptr_t)end -
        (uintptr_t)p
    );
}

unsigned int get_used_ram_kb(void)
{
    unsigned int kernel_bytes =
        (unsigned int)(
            (uintptr_t)&_end -
            (uintptr_t)KERNEL_LOAD_ADDR
        );

    unsigned int stack_bytes =
        get_stack_used();

    unsigned int total_bytes =
        kernel_bytes + stack_bytes;

    return (total_bytes + 1023) / 1024;
}


/* ============================================================
   Keyboard
   ============================================================ */

char getchar(void)
{
    while (kb_head == kb_tail)
    {
        __asm__ volatile("hlt");
    }

    char c = key_buffer[kb_tail];
    kb_tail = (kb_tail + 1) % KEY_BUFFER_SIZE;

    return c;
}


/* ============================================================
   String 
   ============================================================ */

int strcmp(const char *a, const char *b)
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

int strncmp(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; i++)
    {
        if (a[i] != b[i] || a[i] == '\0' || b[i] == '\0')
        {
            if (a[i] == b[i]) return 0;
            return 1;
        }
    }
    return 0;
}

void trim_trailing_spaces(char *str)
{
    int len = 0;
    while (str[len] != '\0')
        len++;

    while (len > 0 && str[len - 1] == ' ')
    {
        str[len - 1] = '\0';
        len--;
    }
}


/* ============================================================
   Comandos NyteFS
   ============================================================ */

unsigned int current_dir = 0;

volatile uint16_t* vga_buffer = (volatile uint16_t*)VIDEO_MEMORY;

void print_colored(const char* str, uint8_t color) {
    for (int i = 0; str[i] != '\0'; i++) {
        if (str[i] == '\n') {
            putchar('\n');
        } else {
            uint16_t attribute = (color << 8) | (uint8_t)str[i];
            vga_buffer[cursor] = attribute;
            cursor++;
            if (cursor >= 80 * 25) {
                scroll();
            }
            update_cursor();
        }
    }
}

void cmd_edit(const char *name)
{
    int idx = fs_find(name, current_dir);
    struct fs_entry entry;
    int is_new __attribute__((unused)) = 0;

    if (idx == -1)
    {
        idx = fs_find_free_entry();
        if (idx == -1)
        {
            print("Error: No space for more entries.\n");
            return;
        }
        is_new = 1;

        for (int i = 0; i < FS_ENTRY_SIZE; i++)
            ((unsigned char *)&entry)[i] = 0;
        int i = 0;
        while (name[i] && i < 31) { entry.name[i] = name[i]; i++; }

        entry.parent = current_dir;
        entry.type = FS_FILE;
        entry.used = 1;
        entry.start_sector = fs_find_free_data_sector();
        entry.sector_count = 1;
        entry.size = 0;
    }
    else
    {
        fs_read_entry(idx, &entry);
        if (entry.type == FS_DIR)
        {
            print("Error: Cannot edit a directory.\n");
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
        print("Error: No free data sector.\n");
        return;
    }

    clear_screen();
    print("=== NyteOS Editor [Editing: ");
    print(name);
    print("] ===\n");
    print("Type your text. Press ESC or enter CTRL+C equivalent to save and exit.\n");
    print("------------------------------------------------------------------\n");

    if (entry.size > KERNEL_FILE_BUFFER_SIZE)
    {
        print("File is too large for the editor.\n");
        return;
    }

    int len = 0;

    if (!is_new && entry.size > 0)
    {
        if (fs_read_file(&entry, kernel_file_buffer,
                         KERNEL_FILE_BUFFER_SIZE))
        {
            for (unsigned int i = 0; i < entry.size; i++)
                putchar(kernel_file_buffer[i]);
            len = entry.size;
        }
    }

    while (1)
    {
        char c = getchar();
        if (!c) continue;

        if (c == 27) 
        {
            break;
        }

        if (c == '\b')
        {
            if (len > 0)
            {
                len--;
                cursor--;
                video[cursor * 2] = ' ';
                update_cursor();
            }
            continue;
        }

        if (len < KERNEL_FILE_BUFFER_SIZE)
        {
            kernel_file_buffer[len++] = c;
            putchar(c);
        }
    }

    if (fs_write_file(idx, &entry, kernel_file_buffer, (uint64_t)len))
    {
        clear_screen();
        print("File saved succesfully.\n\n");
    }
    else
    {
        print("\nError when saving to disk.\n");
    }
}

void cmd_whichdir(void) {
    if (current_dir == 0) {
        print_colored("/", 0x0B);
    } else {
        struct fs_entry entry;
        if (fs_read_entry(current_dir, &entry)) {
            print_colored("/", 0x0B);
            print_colored(entry.name, 0x0B);
        } else {
            print_colored("/unknown", 0x0B);
        }
    }
    putchar('\n');
}

void cmd_mkdir(const char *name)
{
    if (fs_find(name, current_dir) != -1)
    {
        print("Error: Folder already exists.\n");
        return;
    }

    int idx = fs_find_free_entry();
    if (idx == -1)
    {
        print("Error: NyteFS full (no available entries)\n");
        return;
    }

    struct fs_entry entry;
    for (int i = 0; i < 32; i++) entry.name[i] = 0;
    
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
        print("Diretoria criada com sucesso.\n");
    else
        print("Erro ao gravar no disco.\n");
}

void cmd_cat(const char *name)
{
    int idx = fs_find(name, current_dir);
    if (idx == -1)
    {
        print("File not found.\n");
        return;
    }

    struct fs_entry entry;
    fs_read_entry(idx, &entry);

    if (entry.type == FS_DIR)
    {
        print("Error: ");
        print(name);
        print(" and one directory.\n");
        return;
    }

    if (entry.size == 0 || entry.start_sector == 0)
    {
        print("[Empty file]\n");
        return;
    }

    for (uint64_t sector = 0; sector < entry.sector_count; sector++)
    {
        if (!fs_read_sector(entry.start_sector + sector, fs_sector))
        {
            print("Error reading file.\n");
            return;
        }

        uint64_t remaining = entry.size - sector * FS_SECTOR_SIZE;
        unsigned int count = remaining > FS_SECTOR_SIZE
            ? FS_SECTOR_SIZE : (unsigned int)remaining;
        for (unsigned int i = 0; i < count; i++)
            putchar(fs_sector[i]);
    }
    putchar('\n');
}

void cmd_rem(const char *name)
{
    int idx = fs_find(name, current_dir);
    if (idx == -1)
    {
        print("File or folder not found.\n");
        return;
    }

    struct fs_entry entry;
    fs_read_entry(idx, &entry);

    if (entry.type == FS_FILE)
    {
        if (!fs_free_data_range(entry.start_sector, entry.sector_count))
        {
            print("Error freeing file sectors.\n");
            return;
        }
    }

    entry.used = 0;

    if (fs_write_entry(idx, &entry))
    {
        print("Done.\n");
    }
}

void cmd_write(const char *name)
{
    int idx = fs_find(name, current_dir);
    struct fs_entry entry;
    int is_new __attribute__((unused)) = 0;

    if (idx == -1)
    {
        idx = fs_find_free_entry();
        if (idx == -1)
        {
            print("Error: No space for more entries.\n");
            return;
        }
        is_new = 1;

        for (int i = 0; i < FS_ENTRY_SIZE; i++)
            ((unsigned char *)&entry)[i] = 0;
        int i = 0;
        while (name[i] && i < 31) { entry.name[i] = name[i]; i++; }

        entry.parent = current_dir;
        entry.type = FS_FILE;
        entry.used = 1;
        entry.start_sector = fs_find_free_data_sector();
        entry.sector_count = 1;
        entry.size = 0;
    }
    else
    {
        fs_read_entry(idx, &entry);
        if (entry.start_sector == 0)
        {
            entry.start_sector = fs_find_free_data_sector();
            entry.sector_count = 1;
        }
    }

    if (entry.start_sector == (uint64_t)-1)
    {
        print("Error: No free data sector.\n");
        return;
    }

    print("Type the file content and press ENTER:\n> ");

    int len = 0;

    while (1)
    {
        char c = getchar();
        if (!c) continue;
        if (c == '\n')
        {
            putchar('\n');
            break;
        }
        if (c == '\b' && len > 0)
        {
            len--;
            cursor--;
            video[cursor * 2] = ' ';
            update_cursor();
            continue;
        }
        if (len < KERNEL_FILE_BUFFER_SIZE)
        {
            kernel_file_buffer[len++] = c;
            putchar(c);
        }
    }

    if (fs_write_file(idx, &entry, kernel_file_buffer, (uint64_t)len))
        print("Ficheiro guardado com sucesso!\n");
    else
        print("Erro ao guardar ficheiro.\n");
}

void print_number(int n)
{
    if (n == 0)
    {
        putchar('0');
        return;
    }
    
    if (n < 0)
    {
        putchar('-');
        n = -n;
    }

    char buf[12];
    int i = 0;
    while (n > 0 && i < 11)
    {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i > 0)
    {
        putchar(buf[--i]);
    }
}

static uint8_t cmos_read(uint8_t reg)
{
    outb(0x70, reg);
    return inb(0x71);
}

unsigned int get_total_ram_kb(void)
{
    unsigned int ext_kb =
        (unsigned int)cmos_read(0x30) |
        ((unsigned int)cmos_read(0x31) << 8);

    unsigned int ext_64kb =
        (unsigned int)cmos_read(0x34) |
        ((unsigned int)cmos_read(0x35) << 8);

    unsigned int total_kb =
        1024 + ext_kb + (ext_64kb * 64);

    if (total_kb < 1024)
        total_kb = 640;

    return total_kb;
}

unsigned int get_total_ssd_kb(void)
{
    if (fs_live_mode)
    {
        uint64_t ramdisk_kb = fs_storage_sectors / 2;
        return ramdisk_kb > 0xFFFFFFFFULL
            ? 0xFFFFFFFFU
            : (unsigned int)ramdisk_kb;
    }

    uint16_t io;
    uint8_t drive;

    if (!installer_get_source_disk(&io, &drive))
        return 0;

    uint16_t identify[256];

    if (!ata_identify(io, drive, identify))
        return 0;

    uint64_t sectors = ata_identify_sector_count(identify);

    if (!sectors)
        return 0;

    uint64_t total_kb = (sectors * 512ULL) / 1024ULL;

    return total_kb > 0xFFFFFFFFULL
        ? 0xFFFFFFFFU
        : (unsigned int)total_kb;
}

void cmd_status(void)
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

        for (int i = 0; i < 512; i++)
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

    int ram_bytes = 0;
    ram_bytes += sizeof(fs_sector); 
    ram_bytes += sizeof(fs_bitmap);
    ram_bytes += sizeof(idt);
    ram_bytes += sizeof(key_buffer);
    ram_bytes += used_ssd_kb * 1024;

    int used_ram_kb = (ram_bytes + 1023) / 1024;

    unsigned int total_ram_kb = get_total_ram_kb(); 
    if (total_ram_kb == 0) total_ram_kb = 640;
    if (used_ram_kb > (int)total_ram_kb) used_ram_kb = total_ram_kb;
    
    int ram_pct = (total_ram_kb > 0) ? (used_ram_kb * 100) / total_ram_kb : 0;

    print("=== NyteOS Monitor ===\n");
    
    print("SSD: ");
    print_number(used_ssd_kb);
    print("KB / ");
    print_number(total_ssd_kb);
    print("KB (");
    print_number(ssd_pct);
    print("% used)\n");

    print("RAM: ");
    print_number(used_ram_kb);
    print("KB / ");
    print_number(total_ram_kb);
    print("KB (");
    print_number(ram_pct);
    print("% used)\n");
}


/* ============================================================
   Shell
   ============================================================ */

void execute_command(char *command)
{
    trim_trailing_spaces(command);

    if (strcmp(command, "help") == 0)
    {
        print("Commands:\n");
        print("  help\n");
        print("  clear\n");
        print("  about\n");
        print("  version\n");
        print("  list\n");
        print("  dir <directory>\n");
        print("  whichdir\n");
        print("  echo <text>\n");
        print("  mkdir <folder-name>\n");
        print("  edit <file-name>\n");
        print("  write <file-name>\n");
        print("  cat <file-name>\n");
        print("  rem <file-or-folder-name>\n");
        print("  status\n");
        print("  reboot\n");
    }

    else if (strcmp(command, "clear") == 0)
    {
        clear_screen();
    }

    else if (strcmp(command, "about") == 0)
    {
        print("NyteOS v0.6 TNE\n");
        print("32-bit Operating System.\n");
        print("Filesystem: NyteFS\n");
    }

    else if (strcmp(command, "version") == 0)
    {
        print("NyteOS v0.6 TNE\n");
    }

    else if (strcmp(command, "whichdir") == 0)
    {
        cmd_whichdir();
    }

    else if (strcmp(command, "list") == 0)
    {
        fs_list(current_dir);
    }
    else if (strncmp(command, "list ", 5) == 0)
    {
        char *target = command + 5;
        while (*target == ' ') target++;

        if (*target == '/' && *(target + 1) != '\0')
        {
            target++;
        }

        if (*target == '\0' || strcmp(target, "/") == 0)
        {
            fs_list(0);
        }
        else
        {
            char clean_target[32];
            int i = 0;
            while (target[i] != '\0' && target[i] != '/' && i < 31)
            {
                clean_target[i] = target[i];
                i++;
            }
            clean_target[i] = '\0';

            int idx = fs_find(clean_target, current_dir);
            if (idx != -1)
            {
                struct fs_entry entry;
                fs_read_entry(idx, &entry);
                if (entry.type == FS_DIR)
                {
                    fs_list(idx);
                }
                else
                {
                    print("Error: Target is a file, not a directory.\n");
                }
            }
            else
            {
                print("Directory not found.\n");
            }
        }
    }

    else if (strcmp(command, "status") == 0)
    {
        cmd_status();
    }

    else if (strcmp(command, "dir") == 0)
    {
        print("Use: dir <folder>\n");
    }
    else if (strncmp(command, "dir ", 4) == 0)
    {
        char *target = command + 4;
        while (*target == ' ') target++;

        if (*target == '\0') print("Use: dir <folder>\n");
        else if (strcmp(target, "/") == 0) 
        {
            current_dir = 0;
            print("Switched to /\n");
        }
        else
        {
            int idx = fs_find(target, current_dir);
            if (idx != -1)
            {
                struct fs_entry entry;
                fs_read_entry(idx, &entry);
                if (entry.type == FS_DIR)
                {
                    current_dir = idx;
                    print("Changing directory to: ");
                    print(target);
                    print("\n");
                }
                else
                {
                    print("Error: Target is a file, not a directory.\n");
                }
            }
            else
            {
                print("Directory not found.\n");
            }
        }
    }

    else if (strcmp(command, "mkdir") == 0)
    {
        print("Use: mkdir <folder-name>\n");
    }
    else if (strncmp(command, "mkdir ", 6) == 0)
    {
        char *target = command + 6;
        while (*target == ' ') target++;
        if (*target == '\0') print("Use: mkdir <folder-name>\n");
        else cmd_mkdir(target);
    }

    else if (strcmp(command, "cat") == 0)
    {
        print("Use: cat <file-name>\n");
    }
    else if (strncmp(command, "cat ", 4) == 0)
    {
        char *target = command + 4;
        while (*target == ' ') target++;
        if (*target == '\0') print("Use: cat <file-name>\n");
        else cmd_cat(target);
    }

    else if (strcmp(command, "rem") == 0)
    {
        print("Use: rem <file-or-folder-name>\n");
    }
    else if (strncmp(command, "rem ", 4) == 0)
    {
        char *target = command + 4;
        while (*target == ' ') target++;
        if (*target == '\0') print("Use: rem <file-or-folder-name>\n");
        else cmd_rem(target);
    }

    else if (strcmp(command, "write") == 0)
    {
        print("Use: write <file-name>\n");
    }
    else if (strncmp(command, "write ", 6) == 0)
    {
        char *target = command + 6;
        while (*target == ' ') target++;
        if (*target == '\0') print("Use: write <file-name>\n");
        else cmd_write(target);
    }

    else if (strcmp(command, "edit") == 0)
    {
        print("Use: edit <file-name>\n");
    }
    else if (strncmp(command, "edit ", 5) == 0)
    {
        char *target = command + 5;
        while (*target == ' ') target++;
        if (*target == '\0') print("Use: edit <file-name>\n");
        else cmd_edit(target);
    }

    else if (strcmp(command, "echo") == 0)
    {
        putchar('\n');
    }
    else if (strncmp(command, "echo ", 5) == 0)
    {
        print(command + 5);
        putchar('\n');
    }

    else if (strcmp(command, "reboot") == 0)
    {
        while (inb(0x64) & 2) ;
        outb(0x64, 0xFE);
        while (1) ;
    }

    else if (command[0] != '\0')
    {
        print(command);
        print(": command not found\n");
    }
}


void shell(void)
{
    char command[128];

    while (1)
    {

        if (current_dir == 0) {
            print("nyte:/");
        } else {
            struct fs_entry entry;
            if (fs_read_entry(current_dir, &entry)) {
                print("nyte:/");
                print(entry.name);
            } else {
                print("nyte:/?");
            }
        }
        print("> ");

        int length = 0;

        while (1)
        {
            char c = getchar();

            if (!c)
                continue;

            if (c == '\n')
            {
                command[length] = '\0';
                putchar('\n');

                execute_command(command);

                break;
            }

            if (c == '\b')
            {
                if (length > 0)
                {
                    length--;
                    cursor--;

                    video[cursor * 2] = ' ';
                    video[cursor * 2 + 1] = 0x07;

                    update_cursor();
                }

                continue;
            }

            if (c == '\t')
                continue;

            if (length < 127)
            {
                command[length++] = c;
                putchar(c);
            }
        }
    }
}

/* ============================================================
 * BOOT SCREEN
 * ============================================================ */

static void splash_put_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    if (!framebuffer)
        return;

    uint32_t bytes_per_pixel = framebuffer_bpp / 8;

    volatile uint8_t *pixel =
        (volatile uint8_t *)(framebuffer +
        y * framebuffer_pitch +
        x * bytes_per_pixel);

    if (framebuffer_bpp == 32)
    {
        *(volatile uint32_t *)pixel = color;
    }
    else if (framebuffer_bpp == 24)
    {
        pixel[0] = color & 0xFF;
        pixel[1] = (color >> 8) & 0xFF;
        pixel[2] = (color >> 16) & 0xFF;
    }
}

static void splash_clear(uint32_t color)
{
    uint32_t width =
        framebuffer_pitch / (framebuffer_bpp / 8);

    uint32_t height = 600;

    for (uint32_t y = 0; y < height; y++)
    {
        for (uint32_t x = 0; x < width; x++)
        {
            splash_put_pixel(x, y, color);
        }
    }
}

static void splash_rect(
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint32_t color)
{
    for (uint32_t yy = y; yy < y + height; yy++)
    {
        for (uint32_t xx = x; xx < x + width; xx++)
        {
            splash_put_pixel(xx, yy, color);
        }
    }
}

#define SPLASH_LETTER_DELAY_MS 250
#define SPLASH_OS_DELAY_MS     1000

#define SPLASH_CHAR_WIDTH      12
#define SPLASH_CHAR_HEIGHT     15
#define SPLASH_TEXT_SCALE      3
#define SPLASH_CHAR_ADVANCE    14

static const uint16_t splash_font[26][15] = {
    /* A */ {
        0x1F0,0x3F8,0x718,0x618,0x618,
        0x618,0x7FE,0x7FE,0x618,0x618,
        0x618,0x618,0x618,0x618,0x618
    },

    /* B */ {
        0x7F0,0x7F8,0x618,0x618,0x618,
        0x7F0,0x7F8,0x618,0x618,0x618,
        0x618,0x618,0x7F8,0x7F0,0x000
    },

    /* C */ {
        0x1F0,0x3F8,0x71C,0x60C,0x600,
        0x600,0x600,0x600,0x600,0x600,
        0x60C,0x71C,0x3F8,0x1F0,0x000
    },

    /* D */ {
        0x7F0,0x7F8,0x61C,0x60C,0x60C,
        0x60C,0x60C,0x60C,0x60C,0x60C,
        0x60C,0x61C,0x7F8,0x7F0,0x000
    },

    /* E */ {
        0x7FC,0x600,0x600,0x600,0x600,
        0x600,0x600,0x7F8,0x600,0x600,
        0x600,0x600,0x600,0x7FC,0x000
    },

    /* F */ {
        0x7FE,0x7FE,0x600,0x600,0x600,
        0x600,0x600,0x7F8,0x7F8,0x600,
        0x600,0x600,0x600,0x600,0x600
    },

    /* G */ {
        0x1F0,0x3F8,0x71C,0x60C,0x600,
        0x600,0x67C,0x67C,0x60C,0x60C,
        0x60C,0x71C,0x3F8,0x1F0,0x000
    },

    /* H */ {
        0x60C,0x60C,0x60C,0x60C,0x60C,
        0x60C,0x7FE,0x7FE,0x60C,0x60C,
        0x60C,0x60C,0x60C,0x60C,0x000
    },

    /* I */ {
        0x7FE,0x7FE,0x0C0,0x0C0,0x0C0,
        0x0C0,0x0C0,0x0C0,0x0C0,0x0C0,
        0x0C0,0x0C0,0x0C0,0x7FE,0x7FE
    },

    /* J */ {
        0x03F,0x03F,0x00C,0x00C,0x00C,
        0x00C,0x00C,0x00C,0x60C,0x60C,
        0x60C,0x71C,0x3F8,0x1F0,0x000
    },

    /* K */ {
        0x60C,0x618,0x630,0x660,0x6C0,
        0x780,0x780,0x6C0,0x660,0x630,
        0x618,0x60C,0x60C,0x60C,0x000
    },

    /* L */ {
        0x600,0x600,0x600,0x600,0x600,
        0x600,0x600,0x600,0x600,0x600,
        0x600,0x600,0x600,0x7FE,0x7FE
    },

    /* M */ {
        0x60C,0x71C,0x7FC,0x7FC,0x6CC,
        0x6CC,0x60C,0x60C,0x60C,0x60C,
        0x60C,0x60C,0x60C,0x60C,0x000
    },

    /* N */ {
        0x60C,0x70C,0x78C,0x7CC,0x6CC,
        0x66C,0x63C,0x61C,0x60C,0x60C,
        0x60C,0x60C,0x60C,0x60C,0x000
    },

    /* O */ {
        0x1F0,0x3F8,0x71C,0x60C,0x60C,
        0x60C,0x60C,0x60C,0x60C,0x60C,
        0x60C,0x71C,0x3F8,0x1F0,0x000
    },

    /* P */ {
        0x7F0,0x7F8,0x60C,0x60C,0x60C,
        0x60C,0x7F8,0x7F0,0x600,0x600,
        0x600,0x600,0x600,0x600,0x000
    },

    /* Q */ {
        0x1F0,0x3F8,0x71C,0x60C,0x60C,
        0x60C,0x60C,0x60C,0x66C,0x63C,
        0x71C,0x3F8,0x1F0,0x018,0x00C
    },

    /* R */ {
        0x7F0,0x7F8,0x60C,0x60C,0x60C,
        0x60C,0x7F8,0x7F0,0x6C0,0x660,
        0x630,0x618,0x60C,0x60C,0x000
    },

    /* S */ {
        0x1F8,0x3FC,0x70C,0x600,0x600,
        0x700,0x3F8,0x07C,0x00C,0x00C,
        0x60C,0x70C,0x3FC,0x1F8,0x000
    },

    /* T */ {
        0x7FE,0x7FE,0x060,0x060,0x060,
        0x060,0x060,0x060,0x060,0x060,
        0x060,0x060,0x060,0x060,0x000
    },

    /* U */ {
        0x60C,0x60C,0x60C,0x60C,0x60C,
        0x60C,0x60C,0x60C,0x60C,0x60C,
        0x60C,0x71C,0x3F8,0x1F0,0x000
    },

    /* V */ {
        0x60C,0x60C,0x60C,0x60C,0x60C,
        0x60C,0x60C,0x318,0x318,0x198,
        0x198,0x0F0,0x0F0,0x000,0x000
    },

    /* W */ {
        0x60C,0x60C,0x60C,0x60C,0x60C,
        0x6CC,0x6CC,0x7FC,0x7FC,0x71C,
        0x71C,0x60C,0x60C,0x60C,0x000
    },

    /* X */ {
        0x60C,0x60C,0x318,0x318,0x198,
        0x0F0,0x0F0,0x198,0x318,0x318,
        0x60C,0x60C,0x60C,0x60C,0x000
    },

    /* Y */ {
        0xC0C,0xC0C,0x618,0x618,0x330,
        0x1E0,0x1E0,0x0C0,0x0C0,0x0C0,
        0x0C0,0x0C0,0x0C0,0x0C0,0x000
    },

    /* Z */ {
        0x7FE,0x7FE,0x01C,0x038,0x070,
        0x0E0,0x1C0,0x380,0x700,0x600,
        0x600,0x600,0x7FE,0x7FE,0x000
    }
};

static void splash_draw_char(
    uint32_t x,
    uint32_t y,
    char c,
    uint32_t color
)
{
    if (c < 'A' || c > 'Z')
        return;

    const uint16_t *glyph = splash_font[c - 'A'];

    for (uint32_t row = 0; row < SPLASH_CHAR_HEIGHT; row++)
    {
        for (uint32_t col = 0; col < SPLASH_CHAR_WIDTH; col++)
        {
            if (glyph[row] & (1 << (11 - col)))
            {
                splash_rect(
                    x + col * SPLASH_TEXT_SCALE,
                    y + row * SPLASH_TEXT_SCALE,
                    SPLASH_TEXT_SCALE,
                    SPLASH_TEXT_SCALE,
                    color
                );
            }
        }
    }
}

static void splash_fade_char(
    uint32_t x,
    uint32_t y,
    char c)
{
    const uint32_t steps = 20;

    const uint8_t target_r = 124;
    const uint8_t target_g = 58;
    const uint8_t target_b = 237;

    for (uint32_t step = 1; step <= steps; step++)
    {
        uint8_t r =
            (uint8_t)((target_r * step) / steps);

        uint8_t g =
            (uint8_t)((target_g * step) / steps);

        uint8_t b =
            (uint8_t)((target_b * step) / steps);

        uint32_t color =
            ((uint32_t)r << 16) |
            ((uint32_t)g << 8) |
            b;

        splash_draw_char(
            x,
            y,
            c,
            color
        );

        pit_wait_ms(10);
    }
}

/* ============================================================
 * Boot splash
 * ============================================================ */

static void boot_splash(void)
{
    if (!framebuffer)
        return;

    uint32_t width =
        framebuffer_pitch / (framebuffer_bpp / 8);

    uint32_t height = 600;

    splash_clear(0x00000000);

    uint32_t scale = SPLASH_TEXT_SCALE;

    uint32_t text_width =
        4 * SPLASH_CHAR_ADVANCE * scale;

    uint32_t text_x =
        (width - text_width) / 2;

    uint32_t text_y =
        (height / 2) - 40;

    splash_fade_char(
        text_x,
        text_y,
        'N'
    );

    pit_wait_ms(SPLASH_LETTER_DELAY_MS);

    splash_fade_char(
        text_x + SPLASH_CHAR_ADVANCE * scale,
        text_y,
        'Y'
    );

    pit_wait_ms(SPLASH_LETTER_DELAY_MS);

    splash_fade_char(
        text_x + 2 * SPLASH_CHAR_ADVANCE * scale,
        text_y,
        'T'
    );

    pit_wait_ms(SPLASH_LETTER_DELAY_MS);

    splash_fade_char(
        text_x + 3 * SPLASH_CHAR_ADVANCE * scale,
        text_y,
        'E'
    );

    pit_wait_ms(SPLASH_OS_DELAY_MS);

    uint32_t os_width =
        2 * SPLASH_CHAR_ADVANCE * scale;

    uint32_t os_x =
        (width - os_width) / 2;

    uint32_t os_y =
        text_y + 60;

    splash_fade_char(
        os_x,
        os_y,
        'O'
    );

    splash_fade_char(
        os_x + SPLASH_CHAR_ADVANCE * scale,
        os_y,
        'S'
    );

    uint32_t bar_width = 280;
    uint32_t bar_height = 4;

    uint32_t bar_x =
        (width - bar_width) / 2;

    uint32_t bar_y =
        os_y + 60;

    splash_rect(
        bar_x,
        bar_y,
        bar_width,
        bar_height,
        0x00202020
    );

    for (uint32_t progress = 0;
         progress <= bar_width;
         progress += 4)
    {
        splash_rect(
            bar_x,
            bar_y,
            progress,
            bar_height,
            0x007C3AED
        );

        pit_wait_ms(10);
    }
}

/* ============================================================
 * KERNEL MAIN
 * ============================================================ */

extern void shell_ui(void);
extern void installer_ui(void);

void kernel_main(void)
{
    uintptr_t ram_limit =
        (uintptr_t)(*(volatile uint32_t *)(BOOT_INFO_ADDR + 12)) * 1024;

    init_heap(ram_limit);
    kb_head = 0;
    kb_tail = 0;
    init_stack_tracker();

    framebuffer_init();

    if (!fs_initialize_storage())
        print("NyteFS could not be loaded into memory.\n");

    pit_init();

    boot_splash();

    clear_screen();

    init_idt();

    speaker_beep(220, 120);
    speaker_beep(262, 120);
    speaker_beep(330, 180);

    speaker_beep(392, 220);
    speaker_beep(330, 120);
    speaker_beep(262, 120);

    speaker_beep(330, 180);
    speaker_beep(440, 300);

    speaker_beep(392, 150);
    speaker_beep(330, 150);
    speaker_beep(262, 350);

    __asm__ volatile ("sti");

    if (!nyteos_is_installed())
        installer_ui();

    shell_ui();
    
    while (1)
    {
        __asm__ volatile ("hlt");
    }
}
