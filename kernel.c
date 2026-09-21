// kernel

#include <stdint.h>
#include <stddef.h>
#include "rtc.h"
#include "pit.h"
#include "heap.h"

extern void keyboard_stub(void);

#define BOOT_INFO_ADDR 0x8800
#define VIDEO_MEMORY 0xB8000

volatile uint32_t framebuffer;
volatile uint32_t framebuffer_pitch;
volatile uint32_t framebuffer_bpp;

uint32_t fs_base_lba = 0;

#define FS_START_SECTOR 18
#define FS_ENTRY_SECTOR 19
#define FS_BITMAP_SECTOR 35
#define FS_DATA_SECTOR 36

#define FS_MAX_ENTRIES 128
#define FS_ENTRY_SIZE 64
#define FS_SECTOR_SIZE 512

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

volatile char key_buffer[128];
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

volatile char pending_key = 0;
volatile int key_pending = 0;

void keyboard_handler_main(void)
{
    unsigned char scancode = inb(0x60);

    if (scancode & 0x80)
    {
        outb(0x20, 0x20);
        return;
    }

    static const char keymap[128] = {
        0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
        '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
        0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
        0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
        '*', 0, ' '
    };

    if (scancode < 128)
    {
        char c = keymap[scancode];

        if (c)
        {
            pending_key = c;
            key_pending = 1;
        }
    }

    outb(0x20, 0x20);
}

void default_interrupt_handler(void) {
    print("Unhandled Interrupt!\n");
}


static struct idt_ptr idtp_val;

void init_idt(void) {
    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, 0, 0, 0);
    }

    pic_remap();

    idt_set_gate(0x21, (uint32_t)keyboard_stub, 0x08, 0x8E);

    idtp_val.limit = sizeof(idt) - 1;
    idtp_val.base = (uint32_t)&idt;

    __asm__ volatile("lidt (%0)" : : "r"(&idtp_val));
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

static inline void io_wait(void)
{
    for (int i = 0; i < 16; i++)
        inb(0x80);
}

static void ata_select(uint16_t io, uint8_t drive)
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

int ata_read_sector(unsigned int lba, unsigned char *buffer)
{
    ata_select(ata_io, current_drive);

    if (!ata_wait_bsy())
        return 0;

    outb(ata_io + 6, current_drive | ((lba >> 24) & 0x0F));
    outb(ata_io + 2, 1);
    outb(ata_io + 3, lba & 0xFF);
    outb(ata_io + 4, (lba >> 8) & 0xFF);
    outb(ata_io + 5, (lba >> 16) & 0xFF);
    outb(ata_io + 7, ATA_READ);

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

int ata_write_sector(unsigned int lba, unsigned char *buffer)
{
    ata_select(ata_io, current_drive);

    if (!ata_wait_bsy())
        return 0;

    outb(ata_io + 6, current_drive | ((lba >> 24) & 0x0F));
    outb(ata_io + 2, 1);
    outb(ata_io + 3, lba & 0xFF);
    outb(ata_io + 4, (lba >> 8) & 0xFF);
    outb(ata_io + 5, (lba >> 16) & 0xFF);
    outb(ata_io + 7, ATA_WRITE);

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

void print_number(int n);

unsigned int ata_get_drive_size_mb(uint16_t io, uint8_t drive)
{
    uint16_t identify[256];

    outb(io + 6, drive);
    io_wait();

    uint8_t status = inb(io + 7);

    if (status == 0 || status == 0xFF)
        return 0;

    outb(io + 2, 0);
    outb(io + 3, 0);
    outb(io + 4, 0);
    outb(io + 5, 0);
    outb(io + 7, ATA_IDENTIFY);

    for (int i = 0; i < 100000; i++)
    {
        status = inb(io + 7);

        if (status & ATA_ERR)
            return 0;

        if (!(status & ATA_BSY))
            break;
    }

    if (status & ATA_BSY)
        return 0;

    uint8_t mid = inb(io + 4);
    uint8_t high = inb(io + 5);

    if (mid != 0 || high != 0)
        return 0;

    if (!(status & ATA_DRQ))
        return 0;

    for (int i = 0; i < 256; i++)
        identify[i] = inw(io);

    uint32_t sectors =
        ((uint32_t)identify[61] << 16) |
        identify[60];

    if (sectors == 0)
        return 0;

    return (unsigned int)(
        ((uint64_t)sectors * 512ULL) /
        (1024ULL * 1024ULL)
    );
}

/* ============================================================
   NyteFS - Leitura, Escrita e Remoção
   ============================================================ */

struct fs_entry
{
    char name[32];

    unsigned int size;
    unsigned int start_sector;
    unsigned int sector_count;
    unsigned int parent;

    unsigned char type;
    unsigned char used;

    unsigned char reserved[18];
};

#define FS_FILE 1
#define FS_DIR  2

unsigned char fs_sector[512];
unsigned char fs_bitmap[512];

int fs_read_entry(int index, struct fs_entry *entry)
{
    if (index < 0 || index >= FS_MAX_ENTRIES)
        return 0;

    int entries_per_sector =
        FS_SECTOR_SIZE / FS_ENTRY_SIZE;

    unsigned int sector =
        fs_base_lba +
        FS_ENTRY_SECTOR +
        (index / entries_per_sector);

    unsigned int offset =
        (index % entries_per_sector) *
        FS_ENTRY_SIZE;

    if (!ata_read_sector(sector, fs_sector))
        return 0;

    for (int i = 0; i < FS_ENTRY_SIZE; i++)
    {
        ((unsigned char *)entry)[i] =
            fs_sector[offset + i];
    }

    return 1;
}

int fs_write_entry(int index, struct fs_entry *entry)
{
    if (index < 0 || index >= FS_MAX_ENTRIES)
        return 0;

    int entries_per_sector = FS_SECTOR_SIZE / FS_ENTRY_SIZE;
    unsigned int sector = fs_base_lba + FS_ENTRY_SECTOR + (index / entries_per_sector);
    unsigned int offset = (index % entries_per_sector) * FS_ENTRY_SIZE;

    if (!ata_read_sector(sector, fs_sector))
        return 0;

    for (int i = 0; i < FS_ENTRY_SIZE; i++)
    {
        fs_sector[offset + i] = ((unsigned char *)entry)[i];
    }

    return ata_write_sector(sector, fs_sector);
}

int fs_find_free_entry(void)
{
    struct fs_entry entry;
    for (int i = 0; i < FS_MAX_ENTRIES; i++)
    {
        if (!fs_read_entry(i, &entry))
            return -1;
        if (!entry.used)
            return i;
    }
    return -1;
}

int fs_find_free_data_sector(void)
{
    if (!ata_read_sector(FS_BITMAP_SECTOR, fs_bitmap))
        return -1;

    for (int i = 0; i < 512; i++)
    {
        if (fs_bitmap[i] == 0)
        {
            fs_bitmap[i] = 1;
            ata_write_sector(FS_BITMAP_SECTOR, fs_bitmap);
            return FS_DATA_SECTOR + i;
        }
    }
    return -1;
}


int fs_find(const char *name, unsigned int parent)
{
    struct fs_entry entry;

    for (int i = 0; i < FS_MAX_ENTRIES; i++)
    {
        if (!fs_read_entry(i, &entry))
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

    for (int i = 0; i < FS_MAX_ENTRIES; i++)
    {
        if (!fs_read_entry(i, &entry))
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
    while (kb_head == kb_tail) {
        __asm__ volatile("hlt"); 
    }

    char c = key_buffer[kb_tail];
    kb_tail = (kb_tail + 1) % 128;
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

        for (int i = 0; i < 32; i++) entry.name[i] = 0;
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
            entry.start_sector = fs_find_free_data_sector();
    }

    clear_screen();
    print("=== NyteOS Editor [Editing: ");
    print(name);
    print("] ===\n");
    print("Type your text. Press ESC or enter CTRL+C equivalent to save and exit.\n");
    print("------------------------------------------------------------------\n");

    char buffer[512];
    for (int i = 0; i < 512; i++) buffer[i] = 0;
    int len = 0;

    if (!is_new && entry.size > 0 && entry.size < 512)
    {
        if (ata_read_sector(entry.start_sector, fs_sector))
        {
            for (unsigned int i = 0; i < entry.size; i++)
            {
                buffer[i] = fs_sector[i];
                putchar(buffer[i]);
            }
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

        if (len < 511)
        {
            buffer[len++] = c;
            putchar(c);
        }
    }

    for (int i = 0; i < 512; i++)
        fs_sector[i] = (i < len) ? buffer[i] : 0;

    entry.size = len;

    if (ata_write_sector(entry.start_sector, fs_sector) && fs_write_entry(idx, &entry))
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

    if (ata_read_sector(entry.start_sector, fs_sector))
    {
        for (unsigned int i = 0; i < entry.size && i < 512; i++)
        {
            putchar(fs_sector[i]);
        }
        putchar('\n');
    }
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

        for (int i = 0; i < 32; i++) entry.name[i] = 0;
        int i = 0;
        while (name[i] && i < 31) { entry.name[i] = name[i]; i++; }

        entry.parent = current_dir;
        entry.type = FS_FILE;
        entry.used = 1;
        entry.start_sector = fs_find_free_data_sector();
        entry.sector_count = 1;
    }
    else
    {
        fs_read_entry(idx, &entry);
        if (entry.start_sector == 0)
            entry.start_sector = fs_find_free_data_sector();
    }

    print("Type the file content and press ENTER:\n> ");

    char buffer[512];
    int len = 0;

    while (1)
    {
        char c = getchar();
        if (!c) continue;
        if (c == '\n')
        {
            buffer[len] = '\0';
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
        if (len < 511)
        {
            buffer[len++] = c;
            putchar(c);
        }
    }

    for (int i = 0; i < 512; i++)
        fs_sector[i] = (i < len) ? buffer[i] : 0;

    entry.size = len;

    if (ata_write_sector(entry.start_sector, fs_sector) && fs_write_entry(idx, &entry))
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
    unsigned char identify_buffer[512];
    unsigned int total_sectors = 0;

    outb(0x1F6, 0xA0);

    outb(0x1F7, 0xEC);

    unsigned char status = inb(0x1F7);
    if (status != 0)
    {
        while (inb(0x1F7) & 0x80);

        unsigned short *ptr = (unsigned short *)identify_buffer;
        for (int i = 0; i < 256; i++)
        {
            unsigned char lo = inb(0x1F0);
            unsigned char hi = inb(0x1F0);
            ptr[i] = (hi << 8) | lo;
        }

        total_sectors = ((unsigned int)ptr[61] << 16) | ptr[60];
    }

    if (total_sectors == 0)
    {
        unsigned char dummy[512];
        unsigned int s = 1;
        while (ata_read_sector(s, dummy))
        {
            total_sectors = s;
            s++;
            if (s > 10000) break;
        }
        total_sectors++;
    }

    return (total_sectors * 512) / 1024;
}

void cmd_status(void)
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

    int ram_bytes = 0;
    ram_bytes += sizeof(fs_sector); 
    ram_bytes += sizeof(fs_bitmap);
    ram_bytes += sizeof(idt);
    ram_bytes += sizeof(key_buffer);
    ram_bytes += (used_sectors * 512); 

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
        print("NyteOS v0.2\n");
        print("32-bit Operating System.\n");
        print("Filesystem: NyteFS\n");
    }

    else if (strcmp(command, "version") == 0)
    {
        print("NyteOS v0.2\n");
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

void kernel_main(void)
{
    init_heap();
    kb_head = 0;
    kb_tail = 0;
    init_stack_tracker();

    framebuffer_init();

    pit_init();

    boot_splash();

    clear_screen();

    init_idt();

    __asm__ volatile ("sti");

    shell_ui();

    while (1)
    {
        __asm__ volatile ("hlt");
    }
}