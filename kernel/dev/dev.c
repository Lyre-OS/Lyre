#include <stddef.h>
#include <stdint.h>
#include <dev/dev.h>
#include <dev/e9.h>
#include <fs/devtmpfs.h>
#include <lib/lock.h>
#include <dev/drm/drm.h>
#include <sys/pci.h>

static dev_t device_id_counter = 1;

dev_t dev_new_id(void) {
    static lock_t lock = {0};
    SPINLOCK_ACQUIRE(lock);
    dev_t new_id = dev_makedev(device_id_counter++, 0);
    LOCK_RELEASE(lock);
    return new_id;
}

bool dev_add_new(struct resource *device, const char *dev_name) {
    return devtmpfs_add_device(device, dev_name);
}

bool dev_init(struct stivale2_struct_tag_framebuffer *framebuffer_tag) {
    e9_init();
    init_drm(framebuffer_tag);
    pci_init();

    while (1) {}
    return true;
}
