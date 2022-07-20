#include <dev/pit.h>
#include <lib/print.h>
#include <sys/cpu.h>
#include <sys/port.h>

#define PIT_CHANNEL(X) (0x40 + (X))
#define PIT_CR 0x43

#define PIT_CMD_MODE0 0
#define PIT_CMD_MODE1 2
#define PIT_CMD_MODE2 4

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

uint16_t pit_get_count(void) {
    uint16_t cnt = 0;
    asm ("cli");
    outb(PIT_CR, 0x00);

    cnt = inb(PIT_CHANNEL(0));
    cnt |= inb(PIT_CHANNEL(0)) << 8;
    asm ("sti");
    return cnt;
}

void pit_set_count(uint16_t cnt) {
    asm ("cli");
    outb(PIT_CHANNEL(0), cnt & 0xff);
    outb(PIT_CHANNEL(0), (cnt >> 8) & 0xff);
    asm ("sti");
}

void pit_set_timer(int hz) {
    print("pit: Setting hz=%i\n", hz);
    int div = PIT_SCALE / hz;
    outb(PIT_CR, 0x36);
    outb(PIT_CHANNEL(0), div & 0xff);
    outb(PIT_CHANNEL(1), (div >> 8) & 0xff);
}
