#include <sobi/pci.h>
#include <sobi/io.h>
#include <sobi/log.h>
#include <sobi/string.h>

#define PCI_ADDR 0xCF8
#define PCI_DATA 0xCFC

static struct pci_dev devices[PCI_MAX_DEVICES];
static uint32_t ndevices;
static uint64_t ecam_phys;
static uint64_t ecam_size;

static uint32_t cfg_addr(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset)
{
    return 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)(dev & 31) << 11) |
           ((uint32_t)(func & 7) << 8) | (offset & 0xFCu);
}

void pci_set_ecam(uint64_t phys, uint64_t size)
{
    ecam_phys = phys;
    ecam_size = size;
}

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset)
{
    outl(PCI_ADDR, cfg_addr(bus, dev, func, offset));
    return inl(PCI_DATA);
}

uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset)
{
    return (uint16_t)(pci_read32(bus, dev, func, offset) >> ((offset & 2) * 8));
}

uint8_t pci_read8(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset)
{
    return (uint8_t)(pci_read32(bus, dev, func, offset) >> ((offset & 3) * 8));
}

void pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset, uint32_t val)
{
    outl(PCI_ADDR, cfg_addr(bus, dev, func, offset));
    outl(PCI_DATA, val);
}

void pci_write16(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset, uint16_t val)
{
    uint32_t shift = (offset & 2) * 8;
    uint32_t v = pci_read32(bus, dev, func, offset);
    v = (v & ~(0xFFFFu << shift)) | ((uint32_t)val << shift);
    pci_write32(bus, dev, func, offset, v);
}

void pci_write8(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset, uint8_t val)
{
    uint32_t shift = (offset & 3) * 8;
    uint32_t v = pci_read32(bus, dev, func, offset);
    v = (v & ~(0xFFu << shift)) | ((uint32_t)val << shift);
    pci_write32(bus, dev, func, offset, v);
}

void pci_set_command(struct pci_dev *d, uint16_t bits)
{
    d->command = (uint16_t)(pci_read16(d->bus, d->dev, d->func, PCI_CFG_COMMAND) | bits);
    pci_write16(d->bus, d->dev, d->func, PCI_CFG_COMMAND, d->command);
}

static void probe_bars(struct pci_dev *d)
{
    uint16_t cmd = pci_read16(d->bus, d->dev, d->func, PCI_CFG_COMMAND);
    pci_write16(d->bus, d->dev, d->func, PCI_CFG_COMMAND, cmd & ~(PCI_CMD_IO | PCI_CMD_MEM));
    for (int i = 0; i < PCI_BAR_COUNT; i++) {
        uint16_t off = (uint16_t)(PCI_CFG_BAR0 + i * 4);
        uint32_t orig = pci_read32(d->bus, d->dev, d->func, off);
        pci_write32(d->bus, d->dev, d->func, off, 0xFFFFFFFFu);
        uint32_t mask = pci_read32(d->bus, d->dev, d->func, off);
        pci_write32(d->bus, d->dev, d->func, off, orig);
        struct pci_bar *bar = &d->bars[i];
        if (mask == 0 || mask == 0xFFFFFFFFu)
            continue;
        if (orig & 1) {
            bar->flags = PCI_BAR_IO | PCI_BAR_VALID;
            bar->addr = orig & ~3u;
            bar->size = (uint16_t)(~(mask & ~3u) + 1);
            continue;
        }
        bar->flags = PCI_BAR_VALID | ((orig & 8) ? PCI_BAR_PREF : 0);
        uint64_t addr = orig & ~0xFu;
        uint64_t size_mask = mask & ~0xFu;
        if (((orig >> 1) & 3) == 2 && i + 1 < PCI_BAR_COUNT) {
            uint16_t hi_off = (uint16_t)(off + 4);
            uint32_t hi = pci_read32(d->bus, d->dev, d->func, hi_off);
            pci_write32(d->bus, d->dev, d->func, hi_off, 0xFFFFFFFFu);
            uint32_t hi_mask = pci_read32(d->bus, d->dev, d->func, hi_off);
            pci_write32(d->bus, d->dev, d->func, hi_off, hi);
            addr |= (uint64_t)hi << 32;
            size_mask |= (uint64_t)hi_mask << 32;
            bar->flags |= PCI_BAR_64;
            bar->addr = addr;
            bar->size = ~size_mask + 1;
            i++;
            continue;
        }
        bar->addr = addr;
        bar->size = (uint32_t)(~(uint32_t)size_mask + 1);
    }
    pci_write16(d->bus, d->dev, d->func, PCI_CFG_COMMAND, cmd);
}

static void add_device(uint8_t bus, uint8_t dev, uint8_t func)
{
    if (ndevices >= PCI_MAX_DEVICES)
        return;
    struct pci_dev *d = &devices[ndevices++];
    memset(d, 0, sizeof(*d));
    d->bus = bus;
    d->dev = dev;
    d->func = func;
    d->vendor = pci_read16(bus, dev, func, PCI_CFG_VENDOR);
    d->device = pci_read16(bus, dev, func, PCI_CFG_DEVICE);
    d->command = pci_read16(bus, dev, func, PCI_CFG_COMMAND);
    d->status = pci_read16(bus, dev, func, PCI_CFG_STATUS);
    uint32_t cls = pci_read32(bus, dev, func, PCI_CFG_CLASS);
    d->revision = (uint8_t)cls;
    d->prog_if = (uint8_t)(cls >> 8);
    d->subclass = (uint8_t)(cls >> 16);
    d->class_code = (uint8_t)(cls >> 24);
    d->header = pci_read8(bus, dev, func, PCI_CFG_HEADER) & 0x7F;
    if (d->header == 0) {
        uint32_t sub = pci_read32(bus, dev, func, PCI_CFG_SUBSYS);
        d->subsystem_vendor = (uint16_t)sub;
        d->subsystem_id = (uint16_t)(sub >> 16);
        if (d->status & PCI_STATUS_CAP)
            d->cap_ptr = pci_read8(bus, dev, func, PCI_CFG_CAP_PTR) & 0xFC;
        probe_bars(d);
    }
}

int pci_scan(void)
{
    ndevices = 0;
    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            if (pci_read16((uint8_t)bus, dev, 0, PCI_CFG_VENDOR) == 0xFFFF)
                continue;
            uint8_t nfunc = (pci_read8((uint8_t)bus, dev, 0, PCI_CFG_HEADER) & 0x80) ? 8 : 1;
            for (uint8_t func = 0; func < nfunc; func++) {
                if (pci_read16((uint8_t)bus, dev, func, PCI_CFG_VENDOR) != 0xFFFF)
                    add_device((uint8_t)bus, dev, func);
            }
        }
    }
    pr_info("pci", "%u devices", ndevices);
    return (int)ndevices;
}

uint32_t pci_count(void)
{
    return ndevices;
}

struct pci_dev *pci_get(uint32_t index)
{
    return index < ndevices ? &devices[index] : NULL;
}
