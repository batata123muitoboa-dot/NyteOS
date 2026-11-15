#ifndef SPEAKER_H
#define SPEAKER_H

#include <stdint.h>

void speaker_on(uint32_t frequency);
void speaker_off(void);
void speaker_frequency(uint32_t frequency);
void speaker_beep(uint32_t frequency, uint32_t duration);

#endif