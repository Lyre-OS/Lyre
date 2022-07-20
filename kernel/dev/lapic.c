#include <dev/lapic.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/idt.h>
#include <sys/cpu.h>

static void *lapic_base;
static uint8_t spurious_vec;
static uint8_t cmci_vec;

static inline uint32_t lapic_read(uint32_t reg) {
    return *((volatile uint32_t *)((uintptr_t)lapic_base + reg));
}

static inline void lapic_write(uint32_t reg, uint32_t val) {
    *((volatile uint32_t *)((uintptr_t)lapic_base + reg)) = val;
}

static inline void lapic_timer_stop(void) {
    lapic_write(LAPIC_TIMER_INITCNT, 0);
    lapic_write(LAPIC_TIMER, 1 << 16);
}

static inline void lapic_timer_calibrate(uint32_t *freq) {
    lapic_timer_stop();
    lapic_timer_stop();
}

static inline void lapic_timer_oneshot(uint8_t us, uint8_t isn) {
	lapic_timer_stop();
	uint32_t ticks = us * (1 / 1000000);
	lapic_write(LAPIC_TIMER, isn);
	lapic_write(LAPIC_TIMER_DIV, 0);
	lapic_write(LAPIC_TIMER_INITCNT, ticks);
}

static inline void lapic_eoi(void) {
    lapic_write(LAPIC_EOI, LAPIC_EOI_ACK);
}

static void lapic_handler(void) {
    
}

// Enable for all cores
void lapic_init(void) {
    lapic_base = (void *)rdmsr(0x1b);
    print("lapic: Base at %lx\n", (long unsigned int)lapic_base);

    // Configure spurious IRQ
    spurious_vec = idt_allocate_vector();
    isr[spurious_vec] = &lapic_handler;
    lapic_write(LAPIC_SPURIOUS, lapic_read(LAPIC_SPURIOUS) | (1 << 8) | spurious_vec);

    // Correction interrupt
    cmci_vec = idt_allocate_vector();
    isr[cmci_vec] = &lapic_handler;
    lapic_write(LAPIC_CMCI, lapic_read(LAPIC_CMCI) | (1 << 8) | cmci_vec);
}

void lapic_send_ipi(uint8_t lapic_id, uint32_t vec) {
    lapic_write(LAPIC_ICR(1), lapic_id << 24);
    lapic_write(LAPIC_ICR(0), vec);

    // Wait for it to be sent
    while (lapic_read(LAPIC_ICR(0)) & 0x1000);
}
