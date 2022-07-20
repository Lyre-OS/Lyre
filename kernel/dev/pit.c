#include <dev/pit.h>
#include <lib/print.h>
#include <sys/cpu.h>

uint16_t pit_get_count(void) {
    uint16_t cnt = 0;
    cli();
    outb(PIT_CR, 0x00);

    cnt = inb(PIT_CHANNEL(0));
    cnt |= inb(PIT_CHANNEL(0)) << 8;
    sti();
    return cnt;
}

void pit_set_count(uint16_t cnt) {
    cli();
    outb(PIT_CHANNEL(0), cnt & 0xff);
    outb(PIT_CHANNEL(0), (cnt >> 8) & 0xff);
    sti();
}

void pit_set_timer(int hz) {
    print("pit: Setting hz=%i\n", hz);
    int div = PIT_SCALE / hz;
    outb(PIT_CR, 0x36);
    outb(PIT_CHANNEL(0), div & 0xff);
    outb(PIT_CHANNEL(1), (div >> 8) & 0xff);
}

void pit_init(void) {
    outb(0x20, 0x11);
    outb(0xa0, 0x11);
    outb(0x21, 0x20);
    outb(0xa1, 0x28);
    outb(0x21, 0x04);
    outb(0xa1, 0x02);
    outb(0x21, 0x01);
    outb(0xa1, 0x01);
    outb(0x21, 0x00);
    outb(0xa1, 0x00);
    pit_set_timer(0x100);
}
