#ifndef XSCORE_NVME_H
#define XSCORE_NVME_H

#include <stdint.h>
#include <xscore/pci.h>



#define XSCORE_NVME_MAX_NAMESPACES 8U

struct xscore_nvme_namespace_info {
    uint32_t nsid;
    uint64_t size_lbas;
    uint64_t capacity_lbas;
    uint32_t lba_size_bytes;
    uint8_t active;
};

struct xscore_nvme_controller_info {
    uint32_t version;
    uint32_t reported_namespace_count;
    uint32_t namespace_count;
    uint32_t max_transfer_bytes;
    uint16_t max_queue_entries;
    uint8_t doorbell_stride;
    uint8_t ready;
    char serial[21];
    char model[41];
    char firmware[9];
    struct xscore_nvme_namespace_info namespaces[XSCORE_NVME_MAX_NAMESPACES];
};

/* Initialize PCI NVMe controller and identify the controller/namespaces. */
int xscore_nvme_initialize(void);
int xscore_nvme_present(void);
const struct xscore_nvme_controller_info *xscore_nvme_get_info(void);

/* Synchronous namespace block I/O (one namespace LBA unit per block). */
int xscore_nvme_read_blocks(uint32_t nsid, uint64_t lba, uint32_t count, void *buffer);
int xscore_nvme_write_blocks(uint32_t nsid, uint64_t lba, uint32_t count, const void *buffer);
int xscore_nvme_flush(uint32_t nsid);
int xscore_nvme_trim(uint32_t nsid, uint64_t lba, uint32_t count);
int xscore_nvme_get_smart_log(uint32_t nsid, struct nvme_smart_log *log);

/* 512-byte block adapter for the existing akaFS buffer-cache backend. */
int xscore_nvme_block_present(void);
uint64_t xscore_nvme_sector_count(void);
int xscore_nvme_read_sector(uint64_t sector, void *buffer);
int xscore_nvme_write_sector(uint64_t sector, const void *buffer);

#endif /* XSCORE_NVME_H */
