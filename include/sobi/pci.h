#ifndef SOBI_PCI_H
#define SOBI_PCI_H

#include <sobi/types.h>

#define PCI_BAR_COUNT 6
#define PCI_MAX_DEVICES 48

#define PCI_CFG_VENDOR  0x00
#define PCI_CFG_DEVICE  0x02
#define PCI_CFG_COMMAND 0x04
#define PCI_CFG_STATUS  0x06
#define PCI_CFG_CLASS   0x08
#define PCI_CFG_HEADER  0x0E
#define PCI_CFG_BAR0    0x10
#define PCI_CFG_SUBSYS  0x2C
#define PCI_CFG_CAP_PTR 0x34

#define PCI_CMD_IO     0x0001
#define PCI_CMD_MEM    0x0002
#define PCI_CMD_MASTER 0x0004

#define PCI_STATUS_CAP 0x10

#define PCI_CAP_ID_VENDOR 0x09

#define PCI_BAR_IO    0x01u
#define PCI_BAR_64    0x02u
#define PCI_BAR_PREF  0x04u
#define PCI_BAR_VALID 0x08u

struct pci_bar {
    uint64_t addr;
    uint64_t size;
    uint32_t flags;
};

struct pci_dev {
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
    uint8_t header;
    uint8_t revision;
    uint8_t prog_if;
    uint8_t subclass;
    uint8_t class_code;
    uint16_t vendor;
    uint16_t device;
    uint16_t command;
    uint16_t status;
    uint16_t subsystem_vendor;
    uint16_t subsystem_id;
    uint8_t cap_ptr;
    struct pci_bar bars[PCI_BAR_COUNT];
};

void pci_set_ecam(uint64_t phys, uint64_t size);
int pci_scan(void);
uint32_t pci_count(void);
struct pci_dev *pci_get(uint32_t index);
uint8_t pci_read8(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset);
uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset);
uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset);
void pci_write8(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset, uint8_t val);
void pci_write16(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset, uint16_t val);
void pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset, uint32_t val);
void pci_set_command(struct pci_dev *dev, uint16_t bits);

#endif
