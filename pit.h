#ifndef PIT_H
#define PIT_H

#include <stdint.h>

void pit_init(void);
void pit_wait_ms(uint32_t ms);

#endif
