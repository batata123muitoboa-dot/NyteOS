#include <stdint.h>
#include "pit.h"

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43

#define PIT_BASE_FREQ 1193182
#define PIT_HZ        1000

static inline void pit_outb(uint16_t port, uint8_t value)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static inline uint8_t pit_inb(uint16_t port)
{
    uint8_t value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

static uint16_t pit_divisor;

void pit_init(void)
{
    pit_divisor = PIT_BASE_FREQ / PIT_HZ;

    pit_outb(PIT_COMMAND, 0x34);

    pit_outb(PIT_CHANNEL0, pit_divisor & 0xFF);
    pit_outb(PIT_CHANNEL0, (pit_divisor >> 8) & 0xFF);
}

static uint16_t pit_read_counter(void)
{
    uint8_t low;
    uint8_t high;

    pit_outb(PIT_COMMAND, 0x00);

    low = pit_inb(PIT_CHANNEL0);
    high = pit_inb(PIT_CHANNEL0);

    return ((uint16_t)high << 8) | low;
}

void pit_wait_ms(uint32_t ms)
{
    if (ms == 0)
        return;

    uint32_t elapsed = 0;

    uint16_t previous = pit_read_counter();

    while (elapsed < ms)
    {
        uint16_t current = pit_read_counter();

        if (current > previous)
            elapsed++;

        previous = current;
    }
}
