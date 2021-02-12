
#include <sys/port_io.h>
#include <stdint.h>
#include <sys/pci.h>
#include <lib/alloc.h>
#include <lib/dynarray.h>
#include <dev/dev.h>

#define MAX_FUNCTION 8
#define MAX_DEVICE 32
#define MAX_BUS 256

DYNARRAY_NEW(struct pci_device*, pci_devices);

size_t available_devices;

#define BYTE  0
#define WORD  1
#define DWORD 2

static void pci_check_bus(uint8_t, int64_t);

static void get_address(struct pci_device *device, uint32_t offset) {
    uint32_t address = (device->bus << 16) | (device->device << 11) | (device->func << 8)
        | (offset & ~((uint32_t)(3))) | 0x80000000;
    outd(0xcf8, address);
}

uint8_t pci_read_device_byte(struct pci_device *device, uint32_t offset) {
    get_address(device, offset);
    return inb(0xcfc + (offset & 3));
}

void pci_write_device_byte(struct pci_device *device, uint32_t offset, uint8_t value) {
    get_address(device, offset);
    outb(0xcfc + (offset & 3), value);
}

uint16_t pci_read_device_word(struct pci_device *device, uint32_t offset) {
    get_address(device, offset);
    return inw(0xcfc + (offset & 3));
}

void pci_write_device_word(struct pci_device *device, uint32_t offset, uint16_t value) {
    get_address(device, offset);
    outw(0xcfc + (offset & 3), value);
}

uint32_t pci_read_device_dword(struct pci_device *device, uint32_t offset) {
    get_address(device, offset);
    return ind(0xcfc + (offset & 3));
}

void pci_write_device_dword(struct pci_device *device, uint32_t offset, uint32_t value) {
    get_address(device, offset);
    outd(0xcfc + (offset & 3), value);
}

int pci_read_bar(struct pci_device *device, int bar, struct pci_bar_t *out) {
    if (bar > 5)
        return -1;

    size_t reg_index = 0x10 + bar * 4;
    uint64_t bar_low = pci_read_device_dword(device, reg_index), bar_size_low;
    uint64_t bar_high = 0, bar_size_high = 0xFFFFFFFF;

    if (!bar_low)
        return -1;

    uintptr_t base;
    size_t size;

    int is_mmio = !(bar_low & 1);
    int is_prefetchable = is_mmio && bar_low & (1 << 3);
    int is_64bit = is_mmio && ((bar_low >> 1) & 0b11) == 0b10;

    if (is_64bit)
        bar_high = pci_read_device_dword(device, reg_index + 4);

    base = ((bar_high << 32) | bar_low) & ~(is_mmio ? (0b1111) : (0b11));

    pci_write_device_dword(device, reg_index, 0xFFFFFFFF);
    bar_size_low = pci_read_device_dword(device, reg_index);
    pci_write_device_dword(device, reg_index, bar_low);

    if (is_64bit) {
        pci_write_device_dword(device, reg_index + 4, 0xFFFFFFFF);
        bar_size_high = pci_read_device_dword(device, reg_index + 4);
        pci_write_device_dword(device, reg_index + 4, bar_high);
    }

    size = ((bar_size_high << 32) | bar_size_low) & ~(is_mmio ? (0b1111) : (0b11));
    size = ~size + 1;

    if (out) {
        *out = (struct pci_bar_t){base, size, is_mmio, is_prefetchable};
    }

    return 0;
}

void pci_enable_busmastering(struct pci_device *device) {
    if (!(pci_read_device_dword(device, 0x4) & (1 << 2))) {
        pci_write_device_dword(device, 0x4, pci_read_device_dword(device, 0x4) | (1 << 2));
    }
}

struct pci_device *pci_get_device(uint8_t class, uint8_t subclass, uint8_t prog_if, size_t index) {
    size_t dindex = 0;
    for (size_t i = 0; i < available_devices; i++) {
        struct pci_device *dev = pci_devices.storage[i];
        if (dev->device_class == class && dev->subclass == subclass && dev->prog_if == prog_if) {
            if (dindex != index) {
                dindex++;
            } else {
                return dev;
            }
        }
    }
    return 0;
}

struct pci_device *pci_get_device_by_vendor(uint16_t vendor, uint16_t id, size_t index) {
    size_t dindex = 0;
    for (size_t i = 0; i < available_devices; i++) {
        struct pci_device *dev = pci_devices.storage[i];
        if (dev->vendor_id == vendor && dev->device_id == id) {
            if (dindex != index) {
                dindex++;
            } else {
                return dev;
            }
        }
    }
    return 0;
}

static void pci_check_function(uint8_t bus, uint8_t slot, uint8_t func, int64_t parent) {
    struct pci_device* device = alloc(sizeof(struct pci_device));
    device->bus = bus;
    device->func = func;
    device->device = slot;

    uint32_t config_0 = pci_read_device_dword(device, 0);

    if (config_0 == 0xffffffff) {
        return;
    }

    uint32_t config_8 = pci_read_device_dword(device, 0x8);
    uint32_t config_c = pci_read_device_dword(device, 0xc);
    uint32_t config_3c = pci_read_device_dword(device, 0x3c);

    device->parent = parent;
    device->device_id = (uint16_t)(config_0 >> 16);
    device->vendor_id = (uint16_t)config_0;
    device->rev_id = (uint8_t)config_8;
    device->subclass = (uint8_t)(config_8 >> 16);
    device->device_class = (uint8_t)(config_8 >> 24);
    device->prog_if = (uint8_t)(config_8 >> 8);
    device->irq_pin = (uint8_t)(config_3c >> 8);

    if (config_c & 0x800000)
        device->multifunction = 1;
    else
        device->multifunction = 0;

    size_t id = DYNARRAY_INSERT(pci_devices, device);
    available_devices++;

    if (device->device_class == 0x06 && device->subclass == 0x04) {
        // pci to pci bridge
        struct pci_device *device = pci_devices.storage[id];

        // find devices attached to this bridge
        uint32_t config_18 = pci_read_device_dword(device, 0x18);
        pci_check_bus((config_18 >> 8) & 0xFF, id);
    }
}

static void pci_check_bus(uint8_t bus, int64_t parent) {
    for (size_t dev = 0; dev < MAX_DEVICE; dev++) {
        for (size_t func = 0; func < MAX_FUNCTION; func++) {
            pci_check_function(bus, dev, func, parent);
        }
    }
}

static void pci_init_root_bus(void) {
    struct pci_device device = {0};
    uint32_t config_c = pci_read_device_dword(&device, 0xc);
    uint32_t config_0;

    if (!(config_c & 0x800000)) {
        pci_check_bus(0, -1);
    } else {
        for (size_t func = 0; func < 8; func++) {
            device.func = func;
            config_0 = pci_read_device_dword(&device, 0);
            if (config_0 == 0xffffffff)
                continue;

            pci_check_bus(func, -1);
        }
    }
}

int pci_register_msi(struct pci_device *device, uint8_t vector) {
    uint8_t off = 0;

    uint32_t config_4 = pci_read_device_dword(device, 0x4);
    uint8_t  config_34 = pci_read_device_byte(device, 0x34);

    if((config_4 >> 16) & (1 << 4)) {
        uint8_t cap_off = config_34;

        while(cap_off) {
            uint8_t cap_id = pci_read_device_byte(device, cap_off);
            uint8_t cap_next = pci_read_device_byte(device, cap_off + 1);

            switch(cap_id) {
                case 0x05: {
                    off = cap_off;
                    break;
                }
            }
            cap_off = cap_next;
        }
    }

    if(off == 0) {
        return 0;
    }

    uint16_t msi_opts = pci_read_device_word(device, off + MSI_OPT);
    if(msi_opts & MSI_64BIT_SUPPORTED) {
        union msi_data data = {0};
        union msi_address addr = {0};
        addr.raw = pci_read_device_word(device, off + MSI_ADDR_LOW);
        data.raw = pci_read_device_word(device, off + MSI_DATA_64);
        data.vector = vector;
        //Fixed delivery mode
        data.delivery_mode = 0;
        addr.base_address = 0xFEE;
        addr.destination_id = 0;
        pci_write_device_dword(device, off + MSI_ADDR_LOW, addr.raw);
        pci_write_device_dword(device, off + MSI_DATA_64, data.raw);
    } else {
        union msi_data data = {0};
        union msi_address addr = {0};
        addr.raw = pci_read_device_word(device, off + MSI_ADDR_LOW);
        data.raw = pci_read_device_word(device, off + MSI_DATA_32);
        data.vector = vector;
        //Fixed delivery mode
        data.delivery_mode = 0;
        addr.base_address = 0xFEE;
        addr.destination_id = 0;
        pci_write_device_dword(device, off + MSI_ADDR_LOW, addr.raw);
        pci_write_device_dword(device, off + MSI_DATA_32, data.raw);
    }
    msi_opts |= 1;
    pci_write_device_word(device, off + MSI_OPT, msi_opts);
    return 1;
}

void pci_driver_dispatch() {
    FOR_DRIVER_TYPE(1, struct pci_driver, {
               struct pci_device* dev;
               int i = 0;
               if (driver->match_flags == MATCH_CLASS) {
                    while (dev = pci_get_device(driver->device_class, driver->device_subclass, driver->device_prog_if, i)) {
                        i++;
                        driver->init(dev);
                    }
               } else if (driver->match_flags == MATCH_VENDOR) {
                    while (dev = pci_get_device_by_vendor(driver->vendor_id, driver->device_id, i)) {
                        i++;
                        driver->init(dev);
                    }
               }
            });
}

void pci_init(void) {
    pci_init_root_bus();

    for (size_t i = 0; i < available_devices; i++) {
          struct pci_device *dev = pci_devices.storage[i];

          print("pci:\t%x:%x:%x %x:%x\n", dev->bus, dev->device, dev->func, dev->vendor_id, dev->device_id);

    }
    pci_driver_dispatch();
}

void pci_enable_interrupts(struct pci_device *device) {
    if (pci_read_device_dword(device, 0x4) & (1 << 10)) {
        pci_write_device_dword(device, 0x4, pci_read_device_dword(device, 0x4) & ~(1 << 10));
    }
}
