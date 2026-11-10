#ifndef XSCORE_AHCI_H
#define XSCORE_AHCI_H

#include <stdint.h>
#include <stddef.h>
#include <xscore/pci.h>

#define AHCI_MAX_PORTS 32U
#define AHCI_MAX_COMMANDS 32U
#define AHCI_SECTOR_SIZE 512U

/* FIS Types */
#define FIS_TYPE_REG_H2D    0x27    /* Register FIS - host to device */
#define FIS_TYPE_REG_D2H    0x34    /* Register FIS - device to host */
#define FIS_TYPE_DMA_ACT    0x39    /* DMA activate FIS - device to host */
#define FIS_TYPE_DMA_SETUP  0x41    /* DMA setup FIS - bidirectional */
#define FIS_TYPE_DATA       0x46    /* Data FIS - bidirectional */
#define FIS_TYPE_BIST       0x58    /* BIST activate FIS - bidirectional */
#define FIS_TYPE_PIO_SETUP  0x5F    /* PIO setup FIS - device to host */
#define FIS_TYPE_DEV_BITS   0xA1    /* Set device bits FIS - device to host */

/* ATA Commands */
#define ATA_CMD_READ_DMA        0xC8
#define ATA_CMD_READ_DMA_EXT    0x25
#define ATA_CMD_WRITE_DMA       0xCA
#define ATA_CMD_WRITE_DMA_EXT   0x35
#define ATA_CMD_FLUSH_CACHE     0xE7
#define ATA_CMD_FLUSH_CACHE_EXT 0xEA
#define ATA_CMD_IDENTIFY        0xEC
#define ATA_CMD_IDENTIFY_PACKET 0xA1

/* Port Signature */
#define SATA_SIG_ATA    0x00000101U /* SATA drive */
#define SATA_SIG_ATAPI  0xEB140101U /* SATAPI optical drive */
#define SATA_SIG_SEMB   0xC33C0101U /* Enclosure management bridge */
#define SATA_SIG_PM     0x96690101U /* Port multiplier */

/* Port Status & Control Bits */
#define HBA_PORT_CMD_ST     (1U << 0)   /* Start processing command list */
#define HBA_PORT_CMD_SUD    (1U << 1)   /* Spin-Up Device */
#define HBA_PORT_CMD_POD    (1U << 2)   /* Power On Device */
#define HBA_PORT_CMD_FRE    (1U << 4)   /* FIS Receive Enable */
#define HBA_PORT_CMD_FR     (1U << 14)  /* FIS Receive Running */
#define HBA_PORT_CMD_CR     (1U << 15)  /* Command List Running */
#define HBA_PORT_IS_TFES    (1U << 30)  /* Task File Error Status */

#define HBA_GHC_HR          (1U << 0)   /* HBA Reset */
#define HBA_GHC_IE          (1U << 1)   /* Interrupt Enable */
#define HBA_GHC_AE          (1U << 31)  /* AHCI Enable */

#pragma pack(push, 1)

/* Host to Device Register FIS */
struct fis_reg_h2d {
    uint8_t  fis_type;      /* FIS_TYPE_REG_H2D */
    uint8_t  pmport:4;      /* Port multiplier */
    uint8_t  reserved0:3;   /* Reserved */
    uint8_t  c:1;           /* 1: Command, 0: Control */
    uint8_t  command;       /* Command register */
    uint8_t  featurel;      /* Feature register low */

    uint8_t  lba0;          /* LBA low (7:0) */
    uint8_t  lba1;          /* LBA mid (15:8) */
    uint8_t  lba2;          /* LBA high (23:16) */
    uint8_t  device;        /* Device register */

    uint8_t  lba3;          /* LBA (31:24) */
    uint8_t  lba4;          /* LBA (39:32) */
    uint8_t  lba5;          /* LBA (47:40) */
    uint8_t  featureh;      /* Feature register high */

    uint8_t  countl;        /* Sector count low (7:0) */
    uint8_t  counth;        /* Sector count high (15:8) */
    uint8_t  icc;           /* Isochronous command completion */
    uint8_t  control;       /* Control register */

    uint8_t  reserved1[4];
};

/* Physical Region Descriptor Table (PRDT) entry */
struct hba_prdt_entry {
    uint32_t dba;           /* Data base address low (32-bit aligned) */
    uint32_t dbau;          /* Data base address upper 32 bits */
    uint32_t reserved0;
    uint32_t dbc:22;        /* Byte count (0 = 1 byte, 4M max) */
    uint32_t reserved1:9;
    uint32_t i:1;           /* Interrupt on completion */
};

/* Command Header */
struct hba_cmd_header {
    uint8_t  cfl:5;         /* Command FIS length in DWORDS (2-16) */
    uint8_t  a:1;           /* ATAPI */
    uint8_t  w:1;           /* Write (1: H2D, 0: D2H) */
    uint8_t  p:1;           /* Prefetchable */

    uint8_t  r:1;           /* Reset */
    uint8_t  b:1;           /* BIST */
    uint8_t  c:1;           /* Clear busy upon R_OK */
    uint8_t  reserved0:1;
    uint8_t  pmp:4;         /* Port multiplier port */

    uint16_t prdtl;         /* Physical region descriptor table length */
    volatile uint32_t prdbc;/* Physical region descriptor byte count transferred */

    uint32_t ctba;          /* Command table descriptor base address */
    uint32_t ctbau;         /* Command table descriptor base address upper 32 bits */

    uint32_t reserved1[4];
};

/* Command Table */
struct hba_cmd_tbl {
    uint8_t  cfis[64];      /* Command FIS */
    uint8_t  acmd[16];      /* ATAPI command (12 or 16 bytes) */
    uint8_t  reserved[48];  /* Reserved */
    struct hba_prdt_entry prdt_entry[1]; /* Physical region descriptor table entries (1..65535) */
};

/* HBA Port Registers */
struct hba_port {
    uint32_t clb;           /* 0x00, command list base address, 1K-byte aligned */
    uint32_t clbu;          /* 0x04, command list base address upper 32 bits */
    uint32_t fb;            /* 0x08, FIS base address, 256-byte aligned */
    uint32_t fbu;           /* 0x0C, FIS base address upper 32 bits */
    uint32_t is;            /* 0x10, interrupt status */
    uint32_t ie;            /* 0x14, interrupt enable */
    uint32_t cmd;           /* 0x18, command and status */
    uint32_t reserved0;     /* 0x1C, Reserved */
    uint32_t tfd;           /* 0x20, task file data */
    uint32_t sig;           /* 0x24, signature */
    uint32_t ssts;          /* 0x28, SATA status (SCR0:SStatus) */
    uint32_t sctl;          /* 0x2C, SATA control (SCR2:SControl) */
    uint32_t serr;          /* 0x30, SATA error (SCR1:SError) */
    uint32_t sact;          /* 0x34, SATA active (SCR3:SActive) */
    uint32_t ci;            /* 0x38, command issue */
    uint32_t sntf;          /* 0x3C, SATA notification (SCR4:SNotification) */
    uint32_t fbs;           /* 0x40, FIS-based switch control */
    uint32_t reserved1[11]; /* 0x44 ~ 0x6F, Reserved */
    uint32_t vendor[4];     /* 0x70 ~ 0x7F, vendor specific */
};

/* HBA Memory Registers (Generic Host Control) */
struct hba_mem {
    uint32_t cap;           /* 0x00 - 0x2B, Generic Host Control */
    uint32_t ghc;           /* 0x04, Global Host Control */
    uint32_t is;            /* 0x08, Interrupt Status */
    uint32_t pi;            /* 0x0C, Ports Implemented */
    uint32_t vs;            /* 0x10, Version */
    uint32_t ccc_ctl;       /* 0x14, Command Completion Coalescing Control */
    uint32_t ccc_pts;       /* 0x18, Command Completion Coalescing Ports */
    uint32_t em_loc;        /* 0x1C, Enclosure Management Location */
    uint32_t em_ctl;        /* 0x20, Enclosure Management Control */
    uint32_t cap2;          /* 0x24, Host Capabilities Extended */
    uint32_t bohc;          /* 0x28, BIOS/OS Handoff Control and Status */
    uint8_t  reserved[0x0A0 - 0x02C];
    uint8_t  vendor[0x100 - 0x0A0];
    struct hba_port ports[1]; /* 1 to 32 port structures */
};

#pragma pack(pop)

/* AHCI Device Representation in XSCore */
struct xscore_ahci_device {
    uint8_t  port_num;
    uint8_t  active;
    uint8_t  atapi;
    uint32_t sector_size;
    uint64_t sector_count;
    char     model[41];
    char     serial[21];
    char     firmware[9];
    uint64_t clb_phys;
    uint64_t fb_phys;
    uint64_t ctba_phys;
    struct hba_cmd_header *cmd_headers;
    struct hba_cmd_tbl    *cmd_table;
    uint8_t               *received_fis;
};

/* Public AHCI HAL API */
int xscore_ahci_initialize(void);
int xscore_ahci_present(void);
uint32_t xscore_ahci_device_count(void);
const struct xscore_ahci_device *xscore_ahci_get_device(uint32_t index);

int xscore_ahci_read_sectors(uint8_t port_num, uint64_t start_lba, uint32_t count, void *buffer);
int xscore_ahci_write_sectors(uint8_t port_num, uint64_t start_lba, uint32_t count, const void *buffer);
int xscore_ahci_flush(uint8_t port_num);

#endif /* XSCORE_AHCI_H */
