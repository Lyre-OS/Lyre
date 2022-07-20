#ifndef _DEV__LAPIC_H
#define _DEV__LAPIC_H

#include <stdint.h>

#define LAPIC_ID 0x020
#define LAPIC_VERSION 0x030
#define LAPIC_TPR 0x080 // Task priority register
#define LAPIC_APR 0x090 // Arbritration priority register
#define LAPIC_PPR 0x0a0 // Process priority register
#define LAPIC_EOI 0x0b0 // End of interrupt
#define LAPIC_REMOTE_READ 0x0c0
#define LAPIC_LOGICAL_DEST 0x0d0
#define LAPIC_DEST_FORMAT 0x0e0
#define LAPIC_SPURIOUS 0x0f0
#define LAPIC_ISR(x) ((0x100 + (x * 0x10))) // In-service register 
#define LAPIC_TMR(x) ((0x180 + (x * 0x10))) // Trigger mode register
#define LAPIC_IRR(x) ((0x200 + (x * 0x10))) // Interrupt request register
#define LAPIC_ERROR 0x280 // Error status
#define LAPIC_CMCI 0x2f0 // LVT Corrected machine check interrupt
#define LAPIC_ICR(x) ((0x300 + (x * 0x10))) // Interrupt command register
#define LAPIC_LVT_TIMER 0x320
#define LAPIC_LVT_THERMAL 0x330
#define LAPIC_LVT_PERFORMANCE 0x340
#define LAPIC_LVT_LINT(x) (0x350 + (x * 0x10))
#define LAPIC_LVT_ERROR_REG 0x370
#define LAPIC_TIMER_INITCNT 0x380 // Initial count register
#define LAPIC_TIMER_CURCNT 0x390 // Current count register
#define LAPIC_TIMER_DIV 0x3e0

#define LAPIC_EOI_ACK 0x00

#define LAPIC_ICR_FIXED 0x000
#define LAPIC_ICR_LOWEST 0x100
#define LAPIC_ICR_SMI 0x200
#define LAPIC_ICR_NMI 0x400
#define LAPIC_ICR_INIT 0x500
#define LAPIC_ICR_STARTUP 0x600
#define LAPIC_ICR_EXTERNAL 0x700
#define LAPIC_ICR_PHYSICAL 0x000
#define LAPIC_ICR_LOGICAL 0x800
#define LAPIC_ICR_IDLE 0x000
#define LAPIC_ICR_SEND_PENDING 0x1000
#define LAPIC_ICR_DEASSERT 0x0000
#define LAPIC_ICR_ASSERT 0x4000

void lapic_init(void);
void lapic_eoi(void);
void lapic_timer_oneshot(uint32_t us, uint32_t isn);
void lapic_send_ipi(uint32_t lapic_id, uint32_t vec);
unsigned int lapic_timer_calibrate(void);

#endif
