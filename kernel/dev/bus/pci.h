#ifndef _DEV__BUS__PCI_H

#include <stdint.h>
#include <stddef.h>
#include <lib/vector.h>

#define PCI_MAX_FUNCTION 8
#define PCI_MAX_DEVICE 32
#define PCI_MAX_BUS 256

struct pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    struct pci_device *parent;
    uint16_t device_id;
    uint16_t vendor_id;
    uint16_t revision_id;
    uint8_t class;
    uint8_t subclass;
    uint8_t prog_if;
    bool multifunction;
    uint8_t irq_pin;
    uint16_t msi_offset;
    uint16_t msix_offset;
    bool msi_support;
    bool msix_support;
    uint8_t msix_table_bitmap;
    uint16_t msix_table_size;
};

struct pci_bar {
    void *base;
    size_t size;
    bool is_mmio;
    bool is_prefetchable;
};

typedef VECTOR_TYPE(struct pci_device *) pci_device_list_t;

extern pci_device_list_t pci_devices;
struct pci_bar pci_device_get_bar(struct pci_device *dev, uint8_t bar);
void pci_initialise(void);

#endif
