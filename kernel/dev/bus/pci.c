#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <dev/bus/pci.h>
#include <sys/port.h>
#include <lib/misc.h>
#include <lib/print.h>

#define PCI_MAX_FUNCTION 8
#define PCI_MAX_DEVICE 32
#define PCI_MAX_BUS 256

pci_device_list_t pci_devices = VECTOR_INIT;

static inline void pci_check_bus(uint8_t bus, struct pci_device *parent);

static inline void pci_device_get_address(struct pci_device *dev,
                                          uint32_t offset) {
    uint32_t address = ((uint32_t)dev->bus << 16) | ((uint32_t)dev->slot << 11)
        | ((uint32_t)dev->func << 8) | (offset & (~3)) | 0x80000000;
    outd(0xcf8, address);
}

static inline uint32_t pci_device_read(struct pci_device *dev, uint32_t offset) {
    pci_device_get_address(dev, offset);
    return ind(0xcfc);
}

static inline void pci_device_write(struct pci_device *dev, uint32_t offset,
                                    uint32_t value) {
    pci_device_get_address(dev, offset);
    outd(0xcfc, value);
}

bool pci_device_is_bar_present(struct pci_device *dev, uint8_t bar) {
    ASSERT(bar <= 5);
    uint8_t reg_index = 0x10 + bar * 4;
    return pci_device_read(dev, reg_index) != 0;
}

struct pci_bar pci_device_get_bar(struct pci_device *dev, uint8_t bar) {
    ASSERT(bar <= 5);

    struct pci_bar bar_info = {0};
    uint8_t reg_index = 0x10 + bar * 4;
    uint32_t bar_low = pci_device_read(dev, reg_index);
    uint32_t bar_size_low = pci_device_read(dev, reg_index);

    bar_info.is_mmio = (bar_low & 1) == 0;
    bar_info.is_prefetchable = bar_info.is_mmio && ((bar_low & (1 << 3)) != 0);
    bool is_64_bits = bar_info.is_mmio && ((bar_low >> 1) & 0b11) == 0b10;
    uint32_t bar_high = is_64_bits ? pci_device_read(dev, reg_index + 4) : 0;

    uint64_t mask = ~((uint64_t)(bar_info.is_mmio ? 0b1111 : 0b11));
    bar_info.base = (void *)((((uint64_t)bar_high << 32) | bar_low) & mask);

    pci_device_write(dev, reg_index, 0xffffffff);
    bar_size_low = pci_device_read(dev, reg_index);
    pci_device_write(dev, reg_index, bar_size_low);

    uint32_t bar_size_high = 0xffffffff;
    if (is_64_bits) {
        pci_device_write(dev, reg_index + 4, 0xffffffff);
        bar_size_high = pci_device_read(dev, reg_index + 4);
        pci_device_write(dev, reg_index + 4, bar_high);
    }

    bar_info.size = (((uint64_t)bar_size_high << 32) | bar_size_low) & mask;
    bar_info.size = ~bar_info.size + 1;
    return bar_info;
}

static inline void pci_device_read_info(struct pci_device *dev) {
    dev->device_id = (uint16_t)(pci_device_read(dev, 0x00) >> 16);
    dev->vendor_id = (uint16_t)(pci_device_read(dev, 0x00));
    dev->revision_id = (uint8_t)(pci_device_read(dev, 0x08));
    dev->subclass = (uint8_t)(pci_device_read(dev, 0x08) >> 16);
    dev->class = (uint8_t)(pci_device_read(dev, 0x08) >> 24);
    dev->prog_if = (uint8_t)(pci_device_read(dev, 0x08) >> 8);
    dev->multifunction = ((pci_device_read(dev, 0x0c) & 0x800000) != 0);
    dev->irq_pin = (uint8_t)(pci_device_read(dev, 0x3c) >> 8);
}

static inline void pci_check_func(uint8_t bus, uint8_t slot, uint8_t func,
                                    struct pci_device *parent) {
    struct pci_device device = {0};
    device.bus = bus;
    device.slot = slot;
    device.func = func;
    device.parent = parent;
    
    pci_device_read_info(&device);
    if (device.device_id == 0xffff && device.vendor_id == 0xffff) {
        return;
    }

    if (device.class == 0x06 && device.subclass == 0x04) {
        pci_check_bus((uint8_t)(pci_device_read(&device, 0x18) >> 8), &device);
    } else {
        struct pci_device *save_device = ALLOC(struct pci_device);
        VECTOR_PUSH_BACK(pci_devices, save_device);

        print("pci: Found device %4x:%4x bus=%x,slot=%x,func=%x\n",
            device.vendor_id, device.device_id, bus, slot, func);
        for (uint8_t bar = 0; bar < 6; bar++) {
            if (pci_device_is_bar_present(&device, bar)) {
                struct pci_bar bar_info = pci_device_get_bar(&device, bar);
                print("pci: \tbar#%u base=%08x,size=%x\n", bar,
                    bar_info.base, bar_info.size);
            }
        }
    }
}

static inline void pci_check_bus(uint8_t bus, struct pci_device *parent) {
    for (uint8_t dev = 0; dev < PCI_MAX_DEVICE; dev++) {
        for (uint8_t func = 0; func < PCI_MAX_FUNCTION; func++) {
            pci_check_func(bus, dev, func, parent);
        }
    }
}

void pci_initialise(void) {
    print("pci: Building device scan\n");
    struct pci_device root_bus = {0}; // Bus, slot and func set to 0

    if ((pci_device_read(&root_bus, 0x0c) & 0x800000) == 0) {
        pci_check_bus(0, NULL);
    } else {
        for (uint8_t func = 0; func < PCI_MAX_FUNCTION; func++) {
            struct pci_device host_bridge = {0};
            host_bridge.func = func;

            if (pci_device_read(&host_bridge, 0) == 0xffffffff) {
                continue;
            }
            pci_check_bus(0, NULL);
        }
    }
}
