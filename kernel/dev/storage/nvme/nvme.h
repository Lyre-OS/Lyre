#ifndef _DEV__STORAGE__NVME_H
#define _DEV__STORAGE__NVME_H

#include <lib/lock.h>
#include <stdint.h>

typedef struct {
    uint16_t maxpower;
    uint8_t unused1;
    uint8_t flags;
    uint32_t entrylat; // entry latency
    uint32_t exitlat; // exit latency
    uint8_t readtput;
    uint8_t readlat; // read latency
    uint8_t writetput;
    uint8_t writelat; // write latency
    uint16_t idlepower;
    uint8_t idlescale;
    uint8_t unused2;
    uint16_t power;
    uint8_t workscale;
    uint8_t unused3[9];
} nvme_powerstate_t;

// capabilities for the nvme controller
typedef struct {
    uint16_t vid;
    uint16_t ssvid;
    char sn[20];
    char mn[40];
    char fr[8];
    uint8_t rab;
    uint8_t ieee[3];
    uint8_t mic;
    uint8_t mdts;
    uint16_t ctrlid;
    uint32_t version;
    uint32_t unused1[43];
    uint16_t oacs;
    uint8_t acl;
    uint8_t aerl;
    uint8_t fw; // firmware
    uint8_t lpa;
    uint8_t elpe;
    uint8_t npss;
    uint8_t avscc;
    uint8_t apsta;
    uint16_t wctemp;
    uint16_t cctemp;
    uint16_t unused2[121];
    uint8_t sqes;
    uint8_t cqes;
    uint16_t unused3;
    uint32_t nn;
    uint16_t oncs;
    uint16_t fuses;
    uint8_t fna;
    uint8_t vwc;
    uint16_t awun;
    uint16_t awupf;
    uint8_t nvscc;
    uint8_t unused4;
    uint16_t acwu;
    uint16_t unused5;
    uint32_t sgls;
    uint32_t unused6[377];
    nvme_powerstate_t powerstate[32];
    uint8_t vs[1024];
} nvme_id_t;

typedef struct {
    uint16_t ms;
    uint8_t ds;
    uint8_t rp;
} nvme_lbaf_t;

typedef struct {
    uint64_t size;
    uint64_t capabilities;
    uint64_t nuse;
    uint8_t features;
    uint8_t nlbaf;
    uint8_t flbas;
    uint8_t mc;
    uint8_t dpc;
    uint8_t dps;
    uint8_t nmic;
    uint8_t rescap;
    uint8_t fpi;
    uint8_t unused1;
    uint16_t nawun;
    uint16_t nawupf;
    uint16_t nacwu;
    uint16_t nabsn;
    uint16_t nabo;
    uint16_t nabspf;
    uint16_t unused2;
    uint64_t nvmcap[2];
    uint64_t unusued3[5];
    uint8_t nguid[16];
    uint8_t eui64[8];
    nvme_lbaf_t lbaf[16];
    uint64_t unused3[24];
    uint8_t vs[3712];
} nvme_nsid_t;

#define NVME_OPFLUSH 0x00
#define NVME_OPWRITE 0x01
#define NVME_OPREAD 0x02
#define NVME_OPWRITEU 0x03
#define NVME_OPCMP 0x05
#define NVME_OPWZ 0x08
#define NVME_OPDSM 0x09
#define NVME_OPRESVREG 0x0d
#define NVME_OPRESVREP 0x0e
#define NVME_OPRESVACQ 0x11
#define NVME_OPRESVREL 0x15

#define NVME_OPDELSQ 0x00
#define NVME_OPCREATESQ 0x01
#define NVME_OPGETLOGPAGE 0x02
#define NVME_OPDELCQ 0x04
#define NVME_OPCREATECQ 0x05
#define NVME_OPIDENTIFY 0x06
#define NVME_OPABORT 0x08
#define NVME_OPSETFT 0x09
#define NVME_OPGETFT 0x0a
#define NVME_OPASYNCEV 0x0c
#define NVME_OPSECSEND 0x81
#define NVME_OPSECRECV 0x82

// each command is 512 bytes, so we need to use a union along with reserving unused data portions
// somewhat complete NVMe command set
typedef struct {
    union {
        struct { uint8_t opcode; uint8_t flags; uint16_t cid; uint32_t nsid; uint32_t cdw1[2]; uint64_t metadata; uint64_t prp1; uint64_t prp2; uint32_t cdw2[6]; } common; // generic command
        struct { uint8_t opcode; uint8_t flags; uint16_t cid; uint32_t nsid; uint64_t unused; uint64_t metadata; uint64_t prp1; uint64_t prp2; uint64_t slba; uint16_t len; uint16_t control; uint32_t dsmgmt; uint32_t ref; uint16_t apptag; uint16_t appmask; } rw; // read or write
        struct { uint8_t opcode; uint8_t flags; uint16_t cid; uint32_t nsid; uint64_t unused1; uint64_t unused2; uint64_t prp1; uint64_t prp2; uint32_t cns; uint32_t unused3[5]; } identify; // identify
        struct { uint8_t opcode; uint8_t flags; uint16_t cid; uint32_t nsid; uint64_t unused1; uint64_t unused2; uint64_t prp1; uint64_t prp2; uint32_t fid; uint32_t dword; uint64_t unused[2]; } features;
        struct { uint8_t opcode; uint8_t flags; uint16_t cid; uint32_t unused1[5]; uint64_t prp1; uint64_t unused2; uint16_t cqid; uint16_t size; uint16_t cqflags; uint16_t irqvec; uint64_t unused3[2]; } createcompq;
        struct { uint8_t opcode; uint8_t flags; uint16_t cid; uint32_t unused1[5]; uint64_t prp1; uint64_t unused2; uint16_t sqid; uint16_t size; uint16_t sqflags; uint16_t cqid; uint64_t unused3[2]; } createsubq;
        struct { uint8_t opcode; uint8_t flags; uint16_t cid; uint32_t unused1[9]; uint16_t qid; uint16_t unused2; uint32_t unused3[5]; } deleteq;
        struct { uint8_t opcode; uint8_t flags; uint16_t cid; uint32_t unused1[9]; uint16_t sqid; uint16_t cqid; uint32_t unused2[5]; } abort;
    };
} nvme_cmd_t;

// command result
typedef struct {
    uint32_t res;
    uint32_t unused;
    uint16_t sqhead;
    uint16_t sqid;
    uint16_t cid;
    uint16_t status;
} nvme_cmdcomp_t;

// according to BAR0 registers (osdev wiki)
typedef struct {
    uint64_t capabilities; // 0x00-0x07
    uint32_t version; // 0x08-0x0B
    uint32_t intms; // 0x0C-0x0F (interrupt mask set)
    uint32_t intmc; // 0x10-0x13 (interrupt mask clear)
    uint32_t conf; // 0x14-0x17
    uint32_t unused1;
    uint32_t status; // 0x1C-0x1F
    uint32_t unused2;
    uint32_t aqa; // 0x24-0x27 (admin queue attrs)
    uint64_t asq; // 0x28-0x2F (admin submit queue)
    uint64_t acq; // 0x30-0x37 (admin completion queue)
} nvme_bar_t;

#define NVME_CAPMQES(cap)      ((cap) & 0xffff)
#define NVME_CAPTIMEOUT(cap)   (((cap) >> 24) & 0xff)
#define NVME_CAPSTRIDE(cap)    (((cap) >> 32) & 0xf)
#define NVME_CAPMPSMIN(cap)    (((cap) >> 48) & 0xf)
#define NVME_CAPMPSMAX(cap)    (((cap) >> 52) & 0xf)

typedef struct {
    volatile nvme_cmd_t *submit;
    volatile nvme_cmdcomp_t *completion;
    volatile uint32_t *submitdb;  
    volatile uint32_t *completedb;
    uint16_t elements; // elements in queue
    uint16_t cqvec;
    uint16_t sqhead;
    uint16_t sqtail;
    uint16_t cqhead;
    uint8_t cqphase;
    uint16_t qid; // queue id
    uint32_t cmdid; // command id
    uint64_t *physregpgs; // pointer to the PRPs
} nvme_queue_t;

#define NVME_WAITCACHE 0 // cache is not ready
#define NVME_READYCACHE 1 // cache is ready
#define NVME_DIRTYCACHE 2 // cache is damaged/dirty

// cache (helps speed up disk read/writes)
typedef struct {
    uint8_t *cache; // pointer to the cache we have for this block
    uint64_t block;
    uint64_t end;
    int status;
} nvme_cachedblock_t;

typedef struct {
    volatile nvme_bar_t *bar;
    uint64_t stride;
    uint64_t queueslots;
    nvme_queue_t queue[2];
    uint64_t lbasize;
    uint64_t lbacount;
    int maxphysrpgs; // maximum number of PRPs (Physical Region Pages)
    uint64_t overwritten;
    nvme_cachedblock_t *cache;
    uint64_t cacheblocksize;
    uint64_t maxtransshift;
} nvme_device_t;

void nvme_submitcmd(nvme_queue_t *queue, nvme_cmd_t cmd);
uint16_t nvme_awaitsubmitcmd(nvme_queue_t *queue, nvme_cmd_t cmd);

#endif
