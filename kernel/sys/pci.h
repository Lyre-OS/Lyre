#ifndef __PCI_H__
#define __PCI_H__

#include <stdint.h>
#include <stddef.h>
#include <dev/dev.h>

struct pci_device {
    int64_t parent;
    uint8_t bus;
    uint8_t func;
    uint8_t device;
    uint16_t device_id;
    uint16_t vendor_id;
    uint8_t rev_id;
    uint8_t subclass;
    uint8_t device_class;
    uint8_t prog_if;
    int multifunction;
    uint8_t irq_pin;
    int has_prt;
    uint32_t gsi;
    uint16_t gsi_flags;
};

struct pci_bar_t {
    uintptr_t base;
    size_t size;

    int is_mmio;
    int is_prefetchable;
};

#define MSI_OPT 2
#define MSI_ADDR_LOW 0x4
#define MSI_DATA_32 0x8
#define MSI_DATA_64 0xC
#define MSI_64BIT_SUPPORTED (1 << 7)

union msi_address {
    struct {
        uint32_t _reserved0 : 2;
        uint32_t destination_mode : 1;
        uint32_t redirection_hint : 1;
        uint32_t _reserved1 : 8;
        uint32_t destination_id : 8;
        // must be 0xFEE
        uint32_t base_address : 12;
    };
    uint32_t raw;
} __attribute__((packed));

union msi_data {
    struct {
        uint32_t vector : 8;
        uint32_t delivery_mode : 3;
        uint32_t _reserved0 : 3;
        uint32_t level : 1;
        uint32_t trigger_mode : 1;
        uint32_t _reserved1 : 16;
    };
    uint32_t raw;
} __attribute__((packed));

#define MATCH_VENDOR    0
#define MATCH_CLASS     1

struct pci_driver {
    struct driver;
    uint8_t match_flags;
    bool (*init)(struct pci_device* dev);
    uint16_t device_id;
    uint16_t vendor_id;
    uint8_t device_subclass;
    uint8_t device_class;
    uint8_t device_prog_if;
};

#define PCI_CLASS_DRIVER(class, subclass, prog_if, init_fun)\
static struct pci_driver drv = {\
    .driver_type = DRIVER_PCI,\
    .match_flags = MATCH_CLASS,\
    .init = init_fun,\
    .device_class = class,\
    .device_subclass = subclass,\
    .device_class = prog_if\
};\
__attribute__((section(".drivers")))\
void* ptr = &drv;\

#define PCI_VENDOR_DRIVER(vendor, devid, init_fun)\
static struct pci_driver drv = {\
    .driver_type = DRIVER_PCI,\
    .match_flags = MATCH_VENDOR,\
    .init = init_fun,\
    .vendor_id = vendor,\
    .device_id = devid\
};\
__attribute__((section(".drivers")))\
void* ptr = &drv;\


uint8_t pci_read_device_byte(struct pci_device *device, uint32_t offset);
void pci_write_device_byte(struct pci_device *device, uint32_t offset, uint8_t value);
uint16_t pci_read_device_word(struct pci_device *device, uint32_t offset);
void pci_write_device_word(struct pci_device *device, uint32_t offset, uint16_t value);
uint32_t pci_read_device_dword(struct pci_device *device, uint32_t offset);
void pci_write_device_dword(struct pci_device *device, uint32_t offset, uint32_t value);

int pci_read_bar(struct pci_device *device, int bar, struct pci_bar_t *out);
void pci_enable_busmastering(struct pci_device *device);
void pci_enable_interrupts(struct pci_device *device);
int pci_register_msi(struct pci_device *device, uint8_t vector);

struct pci_device *pci_get_device(uint8_t class, uint8_t subclass, uint8_t prog_if, size_t index);
struct pci_device *pci_get_device_by_vendor(uint16_t vendor, uint16_t id, size_t index);

void pci_init(void);

#endif
