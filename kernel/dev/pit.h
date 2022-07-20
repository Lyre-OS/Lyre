#ifndef _DEV__PIT_H
#define _DEV__PIT_H

#include <stdint.h>

#define PIT_DIVIDEND 1193180

uint16_t pit_get_current_count(void);
void pit_set_reload_value(uint16_t new_count);
void pit_set_frequency(uint64_t frequency);

#endif
