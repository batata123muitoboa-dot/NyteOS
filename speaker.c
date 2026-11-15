#include <stdint.h>
#include "speaker.h"
#include "pit.h"

#define PIT_BASE_FREQ 1193182

#define PIT_CHANNEL2 0x42
#define PIT_COMMAND  0x43
#define SPEAKER_PORT 0x61

static inline void speaker_outb(uint16_t port, uint8_t value)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static inline uint8_t speaker_inb(uint16_t port)
{
    uint8_t value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

void speaker_on(uint32_t frequency)
{
    uint32_t divisor;
    uint8_t value;

    if (frequency < 19 || frequency > 20000)
        return;

    divisor = PIT_BASE_FREQ / frequency;

    if (divisor == 0 || divisor > 65535)
        return;

    speaker_outb(PIT_COMMAND, 0xB6);

    speaker_outb(PIT_CHANNEL2, divisor & 0xFF);
    speaker_outb(PIT_CHANNEL2, (divisor >> 8) & 0xFF);

    value = speaker_inb(SPEAKER_PORT);
    value |= 0x03;
    speaker_outb(SPEAKER_PORT, value);
}

void speaker_off(void)
{
    uint8_t value;

    value = speaker_inb(SPEAKER_PORT);
    value &= 0xFC;
    speaker_outb(SPEAKER_PORT, value);
}

void speaker_frequency(uint32_t frequency)
{
    speaker_on(frequency);
}

void speaker_beep(uint32_t frequency, uint32_t duration)
{
    speaker_on(frequency);
    pit_wait_ms(duration);
    speaker_off();
}