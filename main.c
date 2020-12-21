#include <sys/gdt.h>
#include <sys/idt.h>
//#include <sys/pci.h>
#include <sys/apic.h>
#include <sys/hpet.h>
#include <sys/cpu.h>
#include <mm/pmm.h>
#include <limine/stivale/stivale.h>
#include <lib/dmesg.h>
#include <lib/print.h>
#include <lib/alarm.h>
#include <acpi/acpi.h>
#include <fs/vfs.h>
#include <fs/tmpfs.h>

void main(struct stivale_struct *stivale_struct) {
    gdt_init();
    idt_init();
    pmm_init((void *)stivale_struct->memory_map_addr,
             stivale_struct->memory_map_entries);
    dmesg_enable();
    print("Lyre says hello world!\n");

    acpi_init((void *)stivale_struct->rsdp);
    apic_init();
    hpet_init();
    cpu_init();

    alarm_init();

    //pci_init();

    vfs_dump_nodes(NULL, "");
    vfs_install_fs(&tmpfs);
    vfs_mount("", "/", "tmpfs");
    struct handle *h = vfs_open("/test.txt", O_RDWR | O_CREAT, 0644);
    if (h == NULL)
        print("a\n");
    vfs_dump_nodes(NULL, "");

    for (;;) {
        asm volatile ("hlt":::"memory");
    }
}
