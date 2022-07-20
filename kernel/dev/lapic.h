#ifndef _DEV__LAPIC_H
#define _DEV__LAPIC_H

#include <stdint.h>

void lapic_init(void);
void lapic_eoi(void);
void lapic_timer_oneshot(uint32_t us, uint32_t vec);
void lapic_send_ipi(uint32_t lapic_id, uint32_t vec);
void lapic_timer_calibrate(void);

#endif
