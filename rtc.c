#include "rtc.h"

#define CMOS_ADDRESS 0x70
#define CMOS_DATA    0x71

#define RTC_SECONDS  0x00
#define RTC_MINUTES  0x02
#define RTC_HOURS    0x04
#define RTC_DAY      0x07
#define RTC_MONTH    0x08
#define RTC_YEAR     0x09

#define RTC_STATUS_A 0x0A
#define RTC_STATUS_B 0x0B

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
    outb(CMOS_ADDRESS, reg | 0x80);

    return inb(CMOS_DATA);
}

static uint8_t bcd_to_binary(uint8_t value)
{
    return (value & 0x0F) + ((value >> 4) * 10);
}

static void rtc_wait_update(void)
{
    while (rtc_read(RTC_STATUS_A) & 0x80)
        ;
}

void rtc_get_time(rtc_time_t *time)
{
    uint8_t status_b;

    rtc_wait_update();

    time->second = rtc_read(RTC_SECONDS);
    time->minute = rtc_read(RTC_MINUTES);
    time->hour   = rtc_read(RTC_HOURS);

    time->day   = rtc_read(RTC_DAY);
    time->month = rtc_read(RTC_MONTH);
    time->year  = rtc_read(RTC_YEAR);

    status_b = rtc_read(RTC_STATUS_B);

    if (!(status_b & 0x04)) {
        time->second = bcd_to_binary(time->second);
        time->minute = bcd_to_binary(time->minute);
        time->hour   = bcd_to_binary(time->hour);

        time->day   = bcd_to_binary(time->day);
        time->month = bcd_to_binary(time->month);
        time->year  = bcd_to_binary(time->year);
    }

    time->year += 2000;
}
