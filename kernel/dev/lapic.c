#include <dev/lapic.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/idt.h>
#include <sys/cpu.h>
#include <lib/print.h>
#include <dev/pit.h>

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

static void *lapic_base;
static uint8_t spurious_vec;
static uint8_t cmci_vec;
static uint8_t timer_vec;

// TODO: make global cpu context?
static uint32_t lapic_freq;

static inline uint32_t lapic_read(uint32_t reg) {
    return *((volatile uint32_t *)(lapic_base + reg));
}

static inline void lapic_write(uint32_t reg, uint32_t val) {
    *((volatile uint32_t *)(lapic_base + reg)) = val;
}

static inline void lapic_timer_stop(void) {
    lapic_write(LAPIC_TIMER_INITCNT, 0);
    lapic_write(LAPIC_LVT_TIMER, 1 << 16);
}

static void lapic_handler(void) {
    print("lapic: Hello interrupt\n");
    lapic_eoi();
}

// Enable for all cores
void lapic_init(void) {
    pit_init();

    lapic_base = (void *)((uintptr_t)rdmsr(0x1b) & 0xfffff000);

    // Configure spurious IRQ
    spurious_vec = 0xFF;
    isr[spurious_vec] = &lapic_handler;
    lapic_write(LAPIC_SPURIOUS, lapic_read(LAPIC_SPURIOUS) | (1 << 8) | spurious_vec);

    // Correction interrupt
    cmci_vec = idt_allocate_vector();
    isr[cmci_vec] = &lapic_handler;
    lapic_write(LAPIC_CMCI, lapic_read(LAPIC_CMCI) | (1 << 8) | cmci_vec);

    // Timer interrupt
    timer_vec = idt_allocate_vector();
    isr[timer_vec] = &lapic_handler;
    lapic_write(LAPIC_LVT_TIMER, lapic_read(LAPIC_LVT_TIMER) | (1 << 8) | timer_vec);

    print("lapic: Initialized, base=%lx, timer=%i, spurious irq=%i, cmci irq=%i\n", lapic_base, timer_vec, spurious_vec, cmci_vec);
}

void lapic_eoi(void) {
    lapic_write(LAPIC_EOI, LAPIC_EOI_ACK);
}

void lapic_timer_oneshot(uint32_t us, uint32_t vec) {
    lapic_timer_stop();

    uint32_t ticks = us * (lapic_freq / 1000000);
    lapic_write(LAPIC_LVT_TIMER, vec);
    lapic_write(LAPIC_TIMER_DIV, 0);
    lapic_write(LAPIC_TIMER_INITCNT, ticks);
}

void lapic_send_ipi(uint32_t lapic_id, uint32_t vec) {
    lapic_write(LAPIC_ICR(1), lapic_id << 24);
    lapic_write(LAPIC_ICR(0), vec);
    // Wait for it to be sent
    while ((lapic_read(LAPIC_ICR(0)) & LAPIC_ICR_SEND_PENDING) != 0) {
        asm volatile ("pause");
    }
}

void lapic_timer_calibrate(void) {
    lapic_timer_stop();

    // Initialize PIT
    lapic_write(LAPIC_LVT_TIMER, (1 << 16) | 0xff);
    lapic_write(LAPIC_TIMER_DIV, 0);
    pit_set_count(0xffff); // Reset PIT

    unsigned int init_tick = pit_get_count();
    int samples = 0xfffff;
    lapic_write(LAPIC_TIMER_INITCNT, (uint32_t)samples);
    while (lapic_read(LAPIC_TIMER_CURCNT) != 0);
    unsigned int final_tick = pit_get_count();

    unsigned int total_ticks = init_tick - final_tick;
    lapic_freq = (samples / total_ticks) * PIT_SCALE;
    lapic_timer_stop();
}
