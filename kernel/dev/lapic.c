#include <dev/lapic.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/idt.h>
#include <sys/cpu.h>
#include <lib/print.h>
#include <dev/pit.h>

#define LAPIC_EOI 0x0b0 // End of interrupt
#define LAPIC_SPURIOUS 0x0f0
#define LAPIC_CMCI 0x2f0 // LVT Corrected machine check interrupt
#define LAPIC_ICR(X) ((0x300 + ((X) * 0x10))) // Interrupt command register
#define LAPIC_LVT_TIMER 0x320
#define LAPIC_TIMER_INITCNT 0x380 // Initial count register
#define LAPIC_TIMER_CURCNT 0x390 // Current count register
#define LAPIC_TIMER_DIV 0x3e0
#define LAPIC_EOI_ACK 0x00
#define LAPIC_ICR_SEND_PENDING (1 << 12)

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

    int init_tick = pit_get_count();
    int samples = 0xfffff;
    lapic_write(LAPIC_TIMER_INITCNT, (uint32_t)samples);
    while (lapic_read(LAPIC_TIMER_CURCNT) != 0) {
        asm volatile ("pause");
    }
    int final_tick = pit_get_count();

    int total_ticks = init_tick - final_tick;
    lapic_freq = (samples / total_ticks) * PIT_SCALE;
    lapic_timer_stop();
}
