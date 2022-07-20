#include <stdint.h>
#include <dev/pit.h>
#include <sys/port.h>

uint16_t pit_get_current_count(void) {
    outb(0x43, 0x00);
    uint8_t lo = inb(0x40);
    uint8_t hi = inb(0x40) << 8;
    return ((uint16_t)hi << 8) | lo;
}

void pit_set_reload_value(uint16_t new_count) {
    outb(0x43, 0x34);
    outb(0x40, (uint8_t)new_count);
    outb(0x40, (uint8_t)(new_count >> 8));
}

void pit_set_frequency(uint64_t frequency) {
    uint64_t new_divisor = PIT_DIVIDEND / frequency;
    if (PIT_DIVIDEND % frequency > frequency / 2) {
        new_divisor++;
    }
    pit_set_reload_value((uint16_t)new_divisor);
}
