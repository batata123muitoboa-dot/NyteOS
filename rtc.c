
#include "rtc.h"

#define CMOS_ADDRESS 0x70
#define CMOS_DATA    0x71

#define RTC_SECONDS  0x00
#define RTC_MINUTES  0x02
#define RTC_HOURS    0x04
#define RTC_DAY      0x07
#define RTC_MONTH    0x08
#define RTC_YEAR     0x09
#define RTC_CENTURY  0x32

#define RTC_STATUS_A 0x0A
#define RTC_STATUS_B 0x0B

#define RTC_UIP       0x80
#define RTC_BINARY    0x04
#define RTC_24_HOUR   0x02
#define RTC_PM        0x80

#define RTC_TIMEOUT   1000000

typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint8_t year;
    uint8_t century;
} rtc_raw_time_t;

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

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

static uint8_t rtc_read(uint8_t reg)
{
    outb(CMOS_ADDRESS, reg & 0x7F);
    return inb(CMOS_DATA);
}

static uint8_t bcd_to_binary(uint8_t value)
{
    return (value & 0x0F) + ((value >> 4) * 10);
}

static int rtc_wait_update(void)
{
    for (uint32_t i = 0; i < RTC_TIMEOUT; i++) {
        if (!(rtc_read(RTC_STATUS_A) & RTC_UIP))
            return 1;
    }

    return 0;
}

static void rtc_read_raw(rtc_raw_time_t *t)
{
    t->second  = rtc_read(RTC_SECONDS);
    t->minute  = rtc_read(RTC_MINUTES);
    t->hour    = rtc_read(RTC_HOURS);
    t->day     = rtc_read(RTC_DAY);
    t->month   = rtc_read(RTC_MONTH);
    t->year    = rtc_read(RTC_YEAR);
    t->century = rtc_read(RTC_CENTURY);
}

static int rtc_raw_equal(
    const rtc_raw_time_t *a,
    const rtc_raw_time_t *b
)
{
    return
        a->second  == b->second  &&
        a->minute  == b->minute  &&
        a->hour    == b->hour    &&
        a->day     == b->day     &&
        a->month   == b->month   &&
        a->year    == b->year    &&
        a->century == b->century;
}

static int rtc_read_consistent(rtc_raw_time_t *result)
{
    rtc_raw_time_t first;
    rtc_raw_time_t second;

    for (int attempt = 0; attempt < 5; attempt++) {
        if (!rtc_wait_update())
            continue;

        rtc_read_raw(&first);

        if (!rtc_wait_update())
            continue;

        rtc_read_raw(&second);

        if (rtc_raw_equal(&first, &second)) {
            *result = second;
            return 1;
        }
    }

    return 0;
}

static uint16_t rtc_get_full_year(uint8_t year)
{
    return (uint16_t)(2000 + year);
}

void rtc_get_time(rtc_time_t *time)
{
    if (!time)
        return;

    rtc_raw_time_t raw;

    if (!rtc_read_consistent(&raw))
        return;

    uint8_t status_b = rtc_read(RTC_STATUS_B);

    int binary_mode = status_b & RTC_BINARY;
    int hour_24     = status_b & RTC_24_HOUR;

    uint8_t pm = raw.hour & RTC_PM;

    raw.hour &= 0x7F;

    if (!binary_mode) {
        raw.second = bcd_to_binary(raw.second);
        raw.minute = bcd_to_binary(raw.minute);
        raw.hour   = bcd_to_binary(raw.hour);
        raw.day    = bcd_to_binary(raw.day);
        raw.month  = bcd_to_binary(raw.month);
        raw.year   = bcd_to_binary(raw.year);
    }

    if (!hour_24) {
        raw.hour %= 12;

        if (pm)
            raw.hour += 12;
    }

    time->second = raw.second;
    time->minute = raw.minute;
    time->hour   = raw.hour;

    time->day   = raw.day;
    time->month = raw.month;

    time->year = rtc_get_full_year(raw.year);
}