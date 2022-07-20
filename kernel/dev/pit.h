#ifndef _DEV__PIT_H
#define _DEV__PIT_H

#include <stdint.h>

void pit_init(void);
uint16_t pit_get_count(void);
void pit_set_count(uint16_t cnt);
void pit_set_timer(int hz);

#endif
