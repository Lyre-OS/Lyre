#include <stdint.h>
#include <stddef.h>
#include <dev/char/serial.h>
#include <dev/lapic.h>
#include <dev/dev.h>
#include <lib/elf.h>
#include <lib/print.h>
#include <lib/random.h>
#include <mm/pmm.h>
#include <mm/slab.h>
#include <mm/vmm.h>
#include <sys/gdt.h>
#include <sys/idt.h>
#include <sys/except.h>
#include <sys/int_events.h>
#include <fs/vfs/vfs.h>
#include <limine.h>
#include <fs/initramfs.h>
#include <fs/tmpfs.h>
#include <fs/devtmpfs.h>
#include <sched/sched.h>
#include <acpi/acpi.h>
#include <time/time.h>

void kmain_thread(void);

void *mock_thread = &mock_thread;

void _start(void) {
    serial_init();
    gdt_init();
    set_gs_base(&mock_thread);
    idt_init();
    except_init();
    int_events_init();
    pmm_init();
    slab_init();
    vmm_init();
    proc_init();

    kernel_process = sched_new_process(NULL, vmm_kernel_pagemap);

    cpu_init();
    acpi_init();
    time_init();

    sched_new_kernel_thread(kmain_thread, NULL, true);
    sched_await();
}

void kmain_thread(void) {
    random_init();
    vfs_init();
    tmpfs_init();
    devtmpfs_init();
    vfs_mount(vfs_root, NULL, "/", "tmpfs");
    vfs_create(vfs_root, "/dev", 0755 | S_IFDIR);
    vfs_mount(vfs_root, NULL, "/dev", "devtmpfs");
    dev_init();
    initramfs_init();

    struct pagemap *init_vm = vmm_new_pagemap();
    struct auxval init_auxv, ld_auxv;
    const char *ld_path;

    struct vfs_node *init_node = vfs_get_node(vfs_root, "/usr/sbin/init", true);
    elf_load(init_vm, init_node->resource, 0x0, &init_auxv, &ld_path);

    struct vfs_node *ld = vfs_get_node(vfs_root, ld_path, true);
    elf_load(init_vm, ld->resource, 0x40000000, &ld_auxv, NULL);

    const char *argv[] = {"/usr/sbin/init", NULL};
    const char *envp[] = {NULL};

    struct process *init_proc = sched_new_process(NULL, init_vm);
    struct vfs_node *console_node = vfs_get_node(vfs_root, "/dev/console", true);

    fdnum_create_from_resource(init_proc, console_node->resource, 0, 0, true);
    fdnum_create_from_resource(init_proc, console_node->resource, 0, 1, true);
    fdnum_create_from_resource(init_proc, console_node->resource, 0, 2, true);

    vfs_pathname(init_node, init_proc->name, sizeof(init_proc->name) - 1);
    sched_new_user_thread(init_proc, (void *)ld_auxv.at_entry, NULL, NULL, argv, envp, &init_auxv, true);

    sched_dequeue_and_die();
}
