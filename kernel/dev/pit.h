#ifndef _DEV__PIT_H
#define _DEV__PIT_H

#include <stdint.h>

#define PIT_CHANNEL(x) (0x40 + (x))
#define PIT_CR 0x43

#define PIT_CMD_MODE0 0
#define PIT_CMD_MODE1 2
#define PIT_CMD_MODE2 4

#define PIT_SCALE 1193180

void pit_init(void);
uint16_t pit_get_count(void);
void pit_set_count(uint16_t cnt);
void pit_set_timer(int hz);

#endif
