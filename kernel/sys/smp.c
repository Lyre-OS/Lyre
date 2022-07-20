#include <stddef.h>
#include <sys/smp.h>
#include <dev/lapic.h>
#include <limine.h>

static volatile struct limine_smp_request smp_request = {
    .id = LIMINE_SMP_REQUEST,
    .revision = 0
};

void smp_init(void) {
    struct limine_smp_response *response = smp_request.response;
    if (response == NULL) {
        print("smp: Uniprocessor\n");
        return;
    }

    print("smp: %u cores\n", (unsigned int)response->cpu_count);
    lapic_init();
    for (size_t i = 0; i < response->cpu_count; i++) {
        struct limine_smp_info *cpu = response->cpus[i];

        if (cpu->lapic_id != response->bsp_lapic_id) {
            lapic_send_ipi(0, LAPIC_ICR_INIT | (0x03 << 18));
            lapic_send_ipi(0, LAPIC_ICR_STARTUP | (0x03 << 18));
        }
        print("cpu: #%u lapic_id=%u\n", (unsigned int)i, (unsigned int)cpu->lapic_id);
    }
}
