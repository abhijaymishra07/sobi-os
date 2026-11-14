#ifndef SOBI_VIRTIO_H
#define SOBI_VIRTIO_H

#include <sobi/pci.h>

#define VIRTIO_VENDOR           0x1AF4
#define VIRTIO_ID_BLOCK         0x1001
#define VIRTIO_ID_BLOCK_MODERN  0x1042

#define VIRTIO_PCI_CAP_COMMON_CFG 1
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2
#define VIRTIO_PCI_CAP_ISR_CFG    3
#define VIRTIO_PCI_CAP_DEVICE_CFG 4
#define VIRTIO_PCI_CAP_PCI_CFG    5

#define VIRTIO_STATUS_ACK        1
#define VIRTIO_STATUS_DRIVER     2
#define VIRTIO_STATUS_DRIVER_OK  4
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTIO_STATUS_FAILED     128

#define VIRTIO_F_VERSION_1 32
#define VIRTIO_BLK_F_RO    5
#define VIRTIO_BLK_F_FLUSH 9

#define VIRTQ_DESC_F_NEXT  1
#define VIRTQ_DESC_F_WRITE 2
#define VIRTQ_AVAIL_F_NO_INTERRUPT 1

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
};

struct virtio_seg {
    uint64_t addr;
    uint32_t len;
    uint16_t device_write;
};

struct virtio_dev {
    struct pci_dev *pci;
    volatile uint8_t *common;
    volatile uint8_t *notify_base;
    volatile uint8_t *isr;
    volatile uint8_t *device_cfg;
    uint32_t notify_off_multiplier;
    uint32_t device_feat[2];
    uint32_t driver_feat[2];
    uint8_t status;
    bool ready;
};

struct virtq {
    uint16_t size;
    uint16_t free_head;
    uint16_t num_free;
    uint16_t last_used;
    uint16_t queue_index;
    struct virtq_desc *desc;
    volatile uint16_t *avail_flags;
    volatile uint16_t *avail_idx;
    volatile uint16_t *avail_ring;
    volatile uint16_t *used_flags;
    volatile uint16_t *used_idx;
    struct virtq_used_elem *used_ring;
    volatile uint16_t *notify;
};

int virtio_bind(struct virtio_dev *v, struct pci_dev *pci);
int virtio_negotiate(struct virtio_dev *v, uint32_t want0, uint32_t want1);
void virtio_driver_ok(struct virtio_dev *v);
uint64_t virtio_cfg_read64(struct virtio_dev *v, uint32_t offset);
int virtq_setup(struct virtio_dev *v, uint16_t index, struct virtq *q);
int virtq_xfer(struct virtio_dev *v, struct virtq *q, const struct virtio_seg *segs, uint16_t nsegs);

bool virtio_blk_match(const struct pci_dev *dev);
int virtio_blk_attach(struct pci_dev *dev);

#endif
