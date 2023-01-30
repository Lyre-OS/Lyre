#include <dev/dev.h>
#include <dev/pci.h>
#include <dev/storage/partition.h>
#include <fs/devtmpfs.h>
#include <lib/alloc.h>
#include <lib/print.h>
#include <lib/resource.h>
#include <mm/vmm.h>
#include <time/time.h>
#include <mm/pmm.h>

#define DVD_BLOCK_SIZE          4096
#define ATA_BLOCK_SIZE          512

#define AHCI_MAX_PORTS          32

//Physical Region Descriptor Table
#define AHCI_PRD_MAX_BYTES      (4 * 1024 * 1024) //Max bytes that can go in one PRD entry (4 MiB). Section 4.2.3.3 Fig. 17 (DBC) 
#define AHCI_PRDT_MAX_BLOCKS    (0xFFFF + 1)     //Max blocks you can transfer in one command. 
#define AHCI_PRDT_MAX_LEN       8                 //Max PRD entries in one command. This is equal to (MAX_BLOCKS * BLK_SIZE) . MAX_BYTES.

//Global Host Control (Controller) flags
#define AHCI_GHCf_HBA_RESET         0
#define AHCI_GHCf_INTERRUPT_ENABLE  1
#define AHCI_GHCf_AHCI_ENABLE       31
#define AHCI_GHCF_AHCI_ENABLE       (1 << AHCI_GHCf_AHCI_ENABLE)


#define AHCI_CAPSf_S64A     31  //Supports 64-bit Addressing

#define AHCI_CAPSEXTf_BOH   0   //Supports BIOS/OS Handoff
#define AHCI_CAPSEXTF_BOH   (1 << AHCI_CAPSEXTf_BOH)
#define AHCI_CAPSEXTf_NVMP  1   //NVMHCI Supported & Present

//BIOS/OS Handoff Control flags
#define AHCI_BOHCf_BOS      0   //BIOS-Owned Semaphore (BIOS owns controller)
#define AHCI_BOHCF_BOS      (1 << AHCI_BOHCf_BOS)
#define AHCI_BOHCf_OOS      1   //OS-Owned Semaphore (OS owns controller)
#define AHCI_BOHCF_OOS      (1 << AHCI_BOHCf_OOS)
#define AHCI_BOHCf_BB       4   //BIOS Busy (polling bit while BIOS cleans up things after ownership transfer)

//Command Header flags
#define AHCI_CH_DESCf_A     5   //'ATAPI' bit. Set when ATAPI command is being sent.
#define AHCI_CH_DESCF_A     (1 << AHCI_CH_DESCf_A)
#define AHCI_CH_DESCf_W     6   //'Write' bit. Set when data is being written.
#define AHCI_CH_DESCF_W     (1 << AHCI_CH_DESCf_W)

//Command FIS flags
#define AHCI_CF_DESCf_C     7   //'Command' bit. Set when FIS is an ATA command.
#define AHCI_CF_DESCF_C     (1 << AHCI_CF_DESCf_C)

//Port register flags
//Command and Status register flags
#define AHCI_PxCMDf_ST      0   //'Start'. Start processing commmand list. FRE must be set before.
#define AHCI_PxCMDf_SUD     1   //'Spin-Up Device'. For devices that support Staggered Spin-up. We attempt to set it for all ports.
#define AHCI_PxCMDf_POD     2   //'Power-On Device'. For devices that support Cold Presence. We attempt to set it for all ports.
#define AHCI_PxCMDf_FRE     4   //'FIS Receive Enable'. Allows the processing of FISes.
#define AHCI_PxCMDf_FR      14  //'FIS receive Running'. Status indicator for FRE.
#define AHCI_PxCMDf_CR      15  //'Command list Running'. Status indicator for ST.
#define AHCI_PxCMDf_ATAPI   24  //'Device is ATAPI'. When set, HBA turns on desktop LED when device is in use. For ATAPI devices.
#define AHCI_PxCMDF_ST      (1 << AHCI_PxCMDf_ST)
#define AHCI_PxCMDF_SUD     (1 << AHCI_PxCMDf_SUD)
#define AHCI_PxCMDF_POD     (1 << AHCI_PxCMDf_POD)
#define AHCI_PxCMDF_FRE     (1 << AHCI_PxCMDf_FRE)
#define AHCI_PxCMDF_FR      (1 << AHCI_PxCMDf_FR)
#define AHCI_PxCMDF_CR      (1 << AHCI_PxCMDf_CR)
#define AHCI_PxCMDF_ATAPI   (1 << AHCI_PxCMDf_ATAPI)

//Task File Data register flags
#define AHCI_PxTFDf_STS_ERR 0 // Error Bit of Status register

//Signature types
#define AHCI_PxSIG_ATA      0x00000101
#define AHCI_PxSIG_ATAPI    0xEB140101
#define AHCI_PxSIG_SEMB     0xC33C0101 //Enclosure Management Bridge... rare to encounter in wild.
#define AHCI_PxSIG_PM       0x96690101 //Port multiplier... not relevant to PC-type systems.

//Interrupt flags (same in PxIE and PxIS)
#define AHCI_PxIf_OFS   24  //Overflow Status
#define AHCI_PxIf_INFS  26  //SATA Interface Non-Fatal Error
#define AHCI_PxIf_IFS   27  //SATA Interface Fatal Error
#define AHCI_PxIf_HBDS  28  //Host Bus Data Error
#define AHCI_PxIf_HBFS  29  //Host Bus Fatal Error
#define AHCI_PxIf_TFE   30  //Task File Error, see ATAS_ERR.
#define AHCI_PxIF_TFE   (1 << AHCI_PxIf_TFE)
#define AHCI_PxIf_CPDS  31  //Cold Port Detect Status

//COMRESET flags
//SATA Control register flags
#define AHCI_PxSCTLf_DET_INIT       1
#define AHCI_PxSCTLF_DET_INIT       (1 << AHCI_PxSCTLf_DET_INIT)
//SATA Status register flags
#define AHCI_PxSSTSF_DET_PRESENT    3

//SATA Error register flags
#define AHCI_PxSERR_ERR_I   0   // Recovered Data Integrity Error
#define AHCI_PxSERR_ERR_M   1   // Recovered Communication Error
#define AHCI_PxSERR_ERR_T   8   // Transient Data Integrity Error
#define AHCI_PxSERR_ERR_C   9   // Persistent Communication Error
#define AHCI_PxSERR_ERR_P   10  // SATA Protocol Error
#define AHCI_PxSERR_ERR_E   11  // Internal Error
#define AHCI_PxSERR_DIAG_I  17  // Phy Internal Error
#define AHCI_PxSERR_DIAG_C  21  // Link Layer CRC Error
#define AHCI_PxSERR_DIAG_H  22  // Handshake Error
#define AHCI_PxSERR_DIAG_S  23  // Link Sequence Error
#define AHCI_PxSERR_DIAG_T  24  // Transport State Transition Error

#define AHCI_FISt_H2D    0x27

// ATA defines

//ATA_IDENTIFY command array indexes (array of U16s)
#define ATA_IDENT_SERIAL_NUM        10
#define ATA_IDENT_MODEL_NUM         27
#define ATA_IDENT_LBA48_CAPACITY    100

#define ATA_NOP                 0x00
#define ATA_DEV_RST             0x08
#define ATA_PACKET              0xA0
#define ATA_READ_NATIVE_MAX     0xF8
#define ATA_READ_NATIVE_MAX_EXT 0x27
#define ATA_SET_MAX             0xF9
#define ATA_SET_MAX_EXT         0x37
#define ATA_READ_MULTI          0xC4
#define ATA_READ_MULTI_EXT      0x29
#define ATA_READ_DMA_EXT        0x25
#define ATA_WRITE_MULTI         0xC5
#define ATA_WRITE_MULTI_EXT     0x39
#define ATA_WRITE_DMA_EXT       0x35
#define ATA_IDENTIFY            0xEC
#define ATA_IDENTIFY_PACKET     0xA1 // IDENTIFY PACKET DEVICE, mirror of ATA_IDENTIFY for ATAPI

#define ATAPI_FORMAT_UNIT           0x0400
#define ATAPI_START_STOP_UNIT       0x1B00
#define ATAPI_READ_CAPACITY         0x2500
#define ATAPI_SEEK                  0x2B00
#define ATAPI_SYNC_CACHE            0x3500
#define ATAPI_MODE_SELECT           0x5500
#define ATAPI_CLOSE_TRACK_SESSION   0x5B00
#define ATAPI_BLANK                 0xA100
#define ATAPI_READ                  0xA800
#define ATAPI_WRITE                 0x2A00
#define ATAPI_SET_CD_SPEED          0xBB00

#define ATAS_ERR    0x01
#define ATAS_DRQ    0x08
#define ATAS_DF     0x20
#define ATAS_DRDY   0x40
#define ATAS_BSY    0x80

#define ATAR0_DATA  0
#define ATAR0_FEAT  1
#define ATAR0_NSECT 2
#define ATAR0_SECT  3
#define ATAR0_LCYL  4
#define ATAR0_HCYL  5
#define ATAR0_SEL   6
#define ATAR0_STAT  7
#define ATAR0_CMD   7
#define ATAR1_CTRL  2

// AHCI structs

struct ahci_port
{
    volatile uint32_t cmd_list_base;
    volatile uint32_t cmd_list_base_upper;
    uint32_t fis_base;
    uint32_t fis_base_upper;
    volatile uint32_t interrupt_status;
    volatile uint32_t interrupt_enable;
    volatile uint32_t command;
    uint32_t reserved1;
    volatile uint32_t task_file_data;
    volatile uint32_t signature;
    volatile uint32_t sata_status;
    volatile uint32_t sata_ctrl;
    volatile uint32_t sata_error;
    volatile uint32_t sata_active;
    volatile uint32_t cmd_issue;
    volatile uint32_t sata_notif;
    volatile uint32_t fis_switch_ctrl;
    volatile uint32_t device_sleep;
    uint8_t reserved2[40];
    uint8_t vendor[16];
} __attribute__((packed));

struct ahci_hba
{
    uint32_t caps;
    volatile uint32_t ghc;
    volatile uint32_t interrupt_status;
    uint32_t ports_implemented;
    uint32_t version;
    volatile uint32_t ccc_ctrl;
    volatile uint32_t ccc_ports;
    uint32_t em_location;
    uint32_t em_ctrl;
    uint32_t caps_ext;
    volatile uint32_t bohc;
    uint8_t reserved[116];
    uint8_t vendor[96];
    struct ahci_port ports[32];
} __attribute__((packed));

struct ahci_device
{
    struct resource;
    struct ahci_hba *hba;
    uint64_t cmd_slot_count;
};

struct ahci_portdevice
{
    struct resource;
    struct ahci_port *port;
    uint16_t *dev_id_record;
    uint64_t max_blk;
    size_t id;
    void *prd_buf;
    size_t prd_buf_size;
    struct ahci_device *controller_device;
};

/*
Bits of desc uint32_t:
    4:0 - Command FIS Length, in U32s
      5 - ATAPI bit (A) for ATAPI command
      6 - Write bit (W) for write commands AHCI_CH_DESCf_W 1=write 0=read
      7 - Prefetchable (P)
      8 - Reset (R)
      9 - Built-in Self Test (BIST)
     10 - Clear busy upon R_OK
     11 - Reserved
  15:12 - Port Multiplier Port (PMP)
  31:16 - Physical Region Descriptor Table Length (PRDTL)
*/
struct ahci_portcmdheader
{ // the ahci port command list consists of these headers
    volatile uint16_t desc;
    uint16_t prdt_len;
    uint32_t prd_byte_count;
    uint32_t cmd_table_base;
    uint32_t cmd_table_base_upper;
    uint32_t reserved[4];
} __attribute__((packed));

struct ahci_fisreceived
{
    uint8_t dma_fis[28];
    uint8_t reserved1[4];
    uint8_t pio_fis[20];
    uint8_t reserved2[12];
    uint8_t r_fis[20];
    uint8_t reserved3[4];
    uint8_t devbits_fis[8];
    uint8_t unknown_fis[64];
    uint8_t reserved4[96];
} __attribute__((packed));

struct ahci_fish2d // FIS Host To Device
{
    uint8_t type;
    uint8_t desc; // bit #7 (AHCI_CF_DESCf_C) 1=command, 0=control.
    uint8_t command;
    uint8_t feature_low;
    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;
    uint8_t device;
    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t feature_high;
    uint16_t count;
    uint8_t icc;
    uint8_t ctrl;
    uint32_t reserved;
} __attribute__((packed));

struct ahci_prdtentry
{
    uint32_t data_base;
    uint32_t data_base_upper;
    uint32_t reserved;
    uint32_t data_byte_count; //bit 31 is "Interrupt on Completion". bits 21:0 are the actual count.
} __attribute__((packed));

struct ahci_portcmdtable
{
    volatile uint8_t cmd_fis[64];
    volatile uint8_t acmd[16];
    uint8_t reserved[48];
    struct ahci_prdtentry prdt[8];
} __attribute__((packed));

static size_t ahci_devcount = 0;

static void ahci_sleepms(uint32_t ms)
{
    struct timespec duration;
    struct timer *timer;
    struct event *event;

    if (ms > 999)
    {
        duration.tv_sec = ms / 1000;
        duration.tv_nsec = (long)((ms - ((ms / 1000) * 1000)) * 1000000);
    }
    else
    {
        duration.tv_nsec = (long)(ms * 1000000); 
    }

    timer = timer_new(duration);
    event = &timer->event;

    event_await(&event, 1, true);

    free(timer);
}

static void ahci_portcmdstop(struct ahci_port *port)
{
    port->command &= ~(AHCI_PxCMDF_ST);
    port->command &= ~(AHCI_PxCMDF_FRE);

    do {} while (port->command & AHCI_PxCMDF_CR || port->command & AHCI_PxCMDF_FR);
}

static void ahci_portcmdstart(struct ahci_port *port)
{
    do {} while (port->command & AHCI_PxCMDF_CR);
    
    port->command |= AHCI_PxCMDF_FRE;
    port->command |= AHCI_PxCMDF_ST;
}


static bool ahci_portwait(struct ahci_port *port, uint32_t ms)
{
    uint32_t i = 50;
    uint32_t ms_slices = ms / i;
    
    do
    {
        if (!(port->task_file_data & (ATAS_DRQ | ATAS_BSY)))
        {
            return true;
        }
        ahci_sleepms(i);
    } while (--ms_slices > 1);

    return false;
}

static void ahci_portcmdwait(struct ahci_port *port, int slot)
{
    while (1)
    {
        if (!(port->cmd_issue & (1 << slot))) // command processed
            break;
        if (port->interrupt_status & AHCI_PxIF_TFE)
        {
            kernel_print("ahci: ERROR: portcmdwait failed!\n");
            return;
        }
    }
    if (port->interrupt_status & AHCI_PxIF_TFE)
    {
        kernel_print("ahci: ERROR: portcmdwait failed!\n");
        return;
    }
}

static void ahci_portreset(struct ahci_port *port)
{
    ahci_portcmdstop(port);
    
    port->interrupt_status = port->interrupt_status; //Acknowledge all interrupt statuses.

    if (!ahci_portwait(port, 1000))
    {
        port->sata_ctrl = AHCI_PxSCTLF_DET_INIT;
        ahci_sleepms(2);
        port->sata_ctrl = 0;
    }

    do {} while ((port->sata_status & 0xF) != AHCI_PxSSTSF_DET_PRESENT);
    
    port->sata_error = ~0; //Write all 1s to sata error register.
}

uint32_t ahci_pmmalloc(size_t bytes)
{
    size_t byte_pages = MAX((size_t)1, bytes / PAGE_SIZE);
    return (uint32_t)pmm_alloc_below(byte_pages, 1024*1024); // 1024*1024 = 4 * 1024 * 1024 * 1024 / PAGE_SIZE = 4GB
}

static int ahci_portcmdslotget(struct ahci_device *device, struct ahci_port *port)
{
    uint32_t slots = port->sata_active | port->cmd_issue;
    for (int i = 0; i < (int)device->cmd_slot_count; i++)
    {
        if (!(slots & 1))
            return i;
        slots >>= 1;
    }
    kernel_print("ahci: ERROR: port has no empty cmd slots\n");
    return -1;
}

static struct ahci_portcmdheader *ahci_portactiveheaderget(struct ahci_port *port, int slot)
{
    struct ahci_portcmdheader *cmd_header = (struct ahci_portcmdheader *)port->cmd_list_base;
    return cmd_header + slot;
}

static uint64_t ahci_lba48capget(uint16_t *id_record)
{
    uint64_t *id;
    
    id = (uint64_t *)id_record;
    return id[ATA_IDENT_LBA48_CAPACITY / 4] - 1;
}

uint32_t ahci_endian32(uint32_t val)
{
    uint32_t a = (val >> 24) & 0xFF;
    uint32_t b = (val << 8) & 0xFF0000;
    uint32_t c = (val >> 8) & 0xFF00;
    uint32_t d = (val << 24) & 0xFF000000;

    return a | b | c | d;
}

static uint32_t ahci_atapicapget(struct ahci_device *device, struct ahci_portdevice *port_device)
{
    struct ahci_portcmdtable *cmd_table;
    struct ahci_fish2d *cmd_fis;
    int cmd_slot = ahci_portcmdslotget(device, port_device->port);
    struct ahci_portcmdheader *cmd_header = ahci_portactiveheaderget(port_device->port, cmd_slot);
    uint32_t *buf;
    uint32_t res;

    buf = (uint32_t *)ahci_pmmalloc(8);
    memset(buf, 0, 8);

    cmd_header->desc |= AHCI_CH_DESCF_A;

    cmd_table = (struct ahci_portcmdtable*)cmd_header->cmd_table_base;
    memset(cmd_table, 0, sizeof(struct ahci_portcmdtable));

    //Set up single PRD
    cmd_table->prdt[0].data_base = (uint32_t)buf;
    cmd_table->prdt[0].data_byte_count = DVD_BLOCK_SIZE - 1; // zero-based value
    cmd_header->prdt_len = 1; 

    //setup cmd fis
    cmd_fis = (struct ahci_fish2d*)cmd_table->cmd_fis;
    cmd_fis->type = AHCI_FISt_H2D;
    cmd_fis->desc |= AHCI_CF_DESCF_C; //Set Command bit in H2D FIS.
    cmd_fis->command = ATA_PACKET;
    cmd_table->acmd[0] = ATAPI_READ_CAPACITY >> 8;

    ahci_portwait(port_device->port, 2000);

    port_device->port->cmd_issue |= (1 << cmd_slot);
    ahci_portcmdwait(port_device->port, cmd_slot);

    // FIXME: for some reason, this isn't updating the result .... 

    res = ahci_endian32(buf[0]);
    return res;
}

static void *ahci_bufferalign(struct ahci_portdevice *port_device, void *buf, size_t buf_size, bool is_write)
{
    if (port_device->prd_buf)
        pmm_free(port_device->prd_buf, MAX((size_t)1, port_device->prd_buf_size / 4096));
    port_device->prd_buf_size = buf_size;
    port_device->prd_buf = ahci_pmmalloc(buf_size);
    if (is_write)
        memcpy(port_device->prd_buf, buf, buf_size);
    return port_device->prd_buf;
}

static ssize_t ahci_atablksread(struct resource *_this, struct f_description *description, void *buf, off_t loc, size_t count)
{
    spinlock_acquire(&_this->lock);

    struct ahci_portdevice *this  = (struct ahci_portdevice *)_this;
    struct ahci_port *port = this->port;
    struct ahci_portcmdtable *cmd_table;
    struct ahci_fish2d *cmd_fis;
    int cmd_slot = ahci_portcmdslotget(this->controller_device, port);
    struct ahci_portcmdheader *cmd_header = ahci_portactiveheaderget(port, cmd_slot);
    size_t buf_size;
    size_t buf_size_tmp;
    size_t prdt_len;
    uint8_t *internal_buf;
    uint8_t *internal_buf_tmp;
    size_t byte_count;

    if (count < 1)
        return 0;
    if (count > AHCI_PRDT_MAX_BLOCKS)
    {
        kernel_print("ahci: ERROR: count > max (%d, %d)", count, AHCI_PRDT_MAX_BLOCKS);
        return -1;
    }

    buf_size = count * ATA_BLOCK_SIZE;
    buf_size_tmp = count * ATA_BLOCK_SIZE;
    prdt_len = (buf_size - 1) / AHCI_PRD_MAX_BYTES + 1;

    cmd_header->prdt_len = prdt_len;
    internal_buf = ahci_bufferalign(this, buf, buf_size, false);

    cmd_table = (struct ahci_portcmdtable*)cmd_header->cmd_table_base;
    memset(cmd_table, 0, sizeof(struct ahci_portcmdtable));

    for (size_t i = 0; i < prdt_len; i++)
    {
        if (buf_size_tmp > AHCI_PRD_MAX_BYTES)
            byte_count = AHCI_PRD_MAX_BYTES;
        else
            byte_count = buf_size_tmp;

        cmd_table->prdt[i].data_base = internal_buf_tmp;
        cmd_table->prdt[i].data_byte_count = byte_count - 1;
        buf_size_tmp -= byte_count;
        internal_buf_tmp += byte_count;
    }

    cmd_fis = (struct ahci_fish2d*)cmd_table->cmd_fis;
    cmd_fis->type = AHCI_FISt_H2D;
    cmd_fis->desc |= AHCI_CF_DESCF_C; //Set Command bit in H2D FIS.
    cmd_fis->command = ATA_READ_DMA_EXT;

    cmd_fis->lba0 = loc & 0xFF;
    cmd_fis->lba1 = (loc >> 8) & 0xFF;
    cmd_fis->lba2 = (loc >> 16) & 0xFF;
    cmd_fis->device = 1 << 6;
    cmd_fis->lba3 = (loc >> 24) & 0xFF;
    cmd_fis->lba4 = (loc >> 32) & 0xFF;
    cmd_fis->lba5 = (loc >> 40) & 0xFF;
    cmd_fis->count = count;

    ahci_portwait(port, 2000);

    port->cmd_issue |= (1 << cmd_slot);
    ahci_portcmdwait(port, cmd_slot);    

    memcpy(buf, internal_buf, buf_size);

    spinlock_release(&_this->lock);

    return cmd_header->prd_byte_count;
}

static ssize_t ahci_ataread(struct resource *_this, struct f_description *description, void *buf, off_t loc, size_t count)
{
    ssize_t byte_count = 0;
    ssize_t val = 0;

    if (count <= 0)
        return 0;
    if (count <= AHCI_PRDT_MAX_BLOCKS)
        return ahci_atablksread(_this, description, buf, loc, count);
    else
    {
        while (count > AHCI_PRDT_MAX_BLOCKS)
        {
            val = ahci_atablksread(_this, description, buf, loc, AHCI_PRDT_MAX_BLOCKS);
            if (val < 0)
            {
                kernel_print("ahci: ERROR: ataread bad inner read\n");
                return -1;
            }
            byte_count += val;
            count -= AHCI_PRDT_MAX_BLOCKS;
            loc += AHCI_PRDT_MAX_BLOCKS;
            buf += AHCI_PRDT_MAX_BLOCKS * ATA_BLOCK_SIZE;
        }
    }

    return byte_count;
}

static void ahci_portidentify(struct ahci_device *device, struct ahci_port *port, size_t id)
{
    struct ahci_portdevice *port_device = resource_create(sizeof(struct ahci_portdevice));
    struct ahci_portcmdtable *cmd_table;
    struct ahci_fish2d *cmd_fis;
    int cmd_slot = ahci_portcmdslotget(device, port);
    struct ahci_portcmdheader *cmd_header = ahci_portactiveheaderget(port, cmd_slot);
    uint16_t *dev_id_record;

    if (cmd_slot < 0)
        return;

    port_device->port = port;
    port_device->id = id;
    port_device->controller_device = device;
    port_device->prd_buf = NULL;

    dev_id_record = (uint16_t *)ahci_pmmalloc(512);
    port->interrupt_status = port->interrupt_status;
    
    cmd_table = (struct ahci_portcmdtable*)cmd_header->cmd_table_base;
    memset(cmd_table, 0, sizeof(struct ahci_portcmdtable));

    cmd_table->prdt[0].data_base = (uint32_t)dev_id_record;
    cmd_table->prdt[0].data_base_upper = 0;
    cmd_table->prdt[0].data_byte_count = 512 - 1; // zero-based value
    cmd_header->prdt_len = 1; //1 PRD, as described above, which contains the address to put the ID record.

    //setup cmd fis
    cmd_fis = (struct ahci_fish2d*)cmd_table->cmd_fis;
    cmd_fis->type = AHCI_FISt_H2D;
    cmd_fis->desc |= AHCI_CF_DESCF_C; //Set Command bit in H2D FIS.
    if (port->signature == AHCI_PxSIG_ATAPI)
        cmd_fis->command = ATA_IDENTIFY_PACKET;
    else
        cmd_fis->command = ATA_IDENTIFY;

    cmd_fis->device = 0;

    ahci_portwait(port, 2000);

    port->cmd_issue |= (1 << cmd_slot);
    ahci_portcmdwait(port, cmd_slot);

    port_device->dev_id_record = dev_id_record;

    if (port->signature == AHCI_PxSIG_ATA)
    {
        port_device->max_blk = ahci_lba48capget(dev_id_record);
        kernel_print("ahci: ATA HDD/SSD lba48 max block %d disk size %d MiB\n", port_device->max_blk, port_device->max_blk * ATA_BLOCK_SIZE / 1024 / 1024);
    }
    else if (port->signature == AHCI_PxSIG_ATAPI)
    {
//        FIXME: atapi capacity get isn't writing back to the result buffer, figure out why and uncomment this once it returns correct values

//        port_device->max_blk = ahci_atapicapget(device, port_device);
//        kernel_print("ahci: ATAPI CD/DVD max block %d disc size %d MiB\n", port_device->max_blk, port_device->max_blk * DVD_BLOCK_SIZE / 1024 / 1024);
    }


    char devname[32];
    snprint(devname, sizeof(devname) - 1, "ahci%lup%lu", ahci_devcount, port_device->id);
    port_device->can_mmap = false;
    port_device->stat.st_mode = 0666 | S_IFBLK;
    port_device->stat.st_rdev = resource_create_dev_id();
    port_device->read = ahci_ataread;//ahci_read; // FIXME create ahci_read wrapper for ata/atapi reads
//    port_device->write = NULL;
    devtmpfs_add_device((struct resource *)port_device, devname);
//    partition_enum((struct resource *)port_device, devname, ATA_BLOCK_SIZE, "%sp%u");
}

static void ahci_portinit(struct ahci_device *device, struct ahci_port *port, size_t id)
{
    struct ahci_portcmdheader *cmd_header;
    struct ahci_portcmdheader *cmd_header_base;

    ahci_portreset(port);
    ahci_portcmdstart(port);
    // Spin up, power on device: If the capability isn't suppport the bits will be read-only and this won't do anything.
    port->command |= AHCI_PxCMDF_POD | AHCI_PxCMDF_SUD;
    ahci_sleepms(100);
    ahci_portcmdstop(port);
    // alloc & set cmd list base & cmd list base upper (must be 1024-byte aligned)
    port->cmd_list_base = ahci_pmmalloc((uint64_t)(sizeof(struct ahci_portcmdheader)) * device->cmd_slot_count);
    kernel_print("ahci: port->cmd_list_base=%X\n", port->cmd_list_base);
    port->cmd_list_base_upper = 0;
    // alloc & set fis base & fis base upper (must be 256-byte aligned)
    port->fis_base = ahci_pmmalloc(sizeof(struct ahci_fisreceived));
    kernel_print("ahci: port->fis_base=%X\n", port->fis_base);
    port->fis_base_upper = 0;

    // alloc and set command headers member vars
    cmd_header_base = (struct ahci_portcmdheader *)port->cmd_list_base;
    for (uint64_t i = 0; i < device->cmd_slot_count; i++)
    {
        cmd_header = &cmd_header_base[i];
        //Write Command FIS Length (CFL, a fixed size) in bits 4:0 of the desc. Takes size in uint32_t's.
        cmd_header->desc = sizeof(struct ahci_fish2d) / sizeof(uint32_t);

        //'128-byte' align as per SATA spec, minus 1 since length is 1-based.
        cmd_header->cmd_table_base = ahci_pmmalloc(((uint64_t)(sizeof(struct ahci_portcmdtable)) + (uint64_t)(sizeof(struct ahci_prdtentry)) * (AHCI_PRDT_MAX_LEN - 1)));
        cmd_header->cmd_table_base_upper = 0;
//        kernel_print("ahci: port cmd_slot %d cmd_header->cmd_table_base=%X \n", i, cmd_header->cmd_table_base);
    }

    ahci_portcmdstart(port);
    ahci_portidentify(device, port, id);
}

static void ahci_initcontroller(struct pci_device *device)
{
    kernel_print("ahci: initializing AHCI controller %d\n", ahci_devcount);

    struct ahci_device *controller_res = resource_create(sizeof(struct ahci_device));
    struct pci_bar bar = pci_get_bar(device, 5);
    struct ahci_port *port;
    size_t id;

    pci_set_privl(device, PCI_PRIV_MMIO | PCI_PRIV_BUSMASTER);
    ASSERT_MSG(bar.is_mmio, "PCI bar is not memory mapped!");
//    ASSERT((PCI_READD(device, 0x10) & 0b111) == 0b100);

    controller_res->hba = (struct ahci_hba *)(bar.base & ~0x1F);

    controller_res->hba->ghc |= AHCI_GHCF_AHCI_ENABLE;
    kernel_print("ahci: setting ghc.ae\n");

    if (controller_res->hba->caps_ext & AHCI_CAPSEXTF_BOH)
    {
        controller_res->hba->bohc |= AHCI_BOHCF_OOS;
        kernel_print("ahci: transferring ownership from BIOS\n");

        do {} while (controller_res->hba->bohc & AHCI_BOHCF_BOS);
        ahci_sleepms(25);
        if (controller_res->hba->bohc & AHCI_BOHCf_BB) // if Bios Busy is still set after 25 mS, wait 2 seconds.
        {
            kernel_print("ahci: BIOS Busy flag still set after 25ms, waiting 2000ms\n");
            ahci_sleepms(2000);
        }
    }

    controller_res->cmd_slot_count = (controller_res->hba->caps & 0x1F00) >> 8;
    kernel_print("ahci: cmd_slot_count=%X \n", controller_res->cmd_slot_count);

    for (int i = 0; i < AHCI_MAX_PORTS; i++)
    {
        if (controller_res->hba->ports_implemented & (1 << i))
        {
            port = &controller_res->hba->ports[i];
            kernel_print("ahci: port %d signature 0x%08X ", i, port->signature);
            if (port->signature == AHCI_PxSIG_ATA || port->signature == AHCI_PxSIG_ATAPI)
            {
                if (port->signature == AHCI_PxSIG_ATAPI)
                {
                    port->command |= AHCI_PxCMDF_ATAPI;
                    kernel_print("ATAPI drive\n");
                }
                else if (port->signature == AHCI_PxSIG_ATA)
                    kernel_print("ATA drive\n");

                if (port->command & (AHCI_PxCMDF_ST | AHCI_PxCMDF_CR | AHCI_PxCMDF_FR | AHCI_PxCMDF_FRE))
                {
                    kernel_print("ahci: port %d not idle, stopping port\n", i);
                    ahci_portcmdstop(port);
                }

                id = (size_t)i;
                ahci_portinit(controller_res, port, id);
            }
            else
                kernel_print("\n");
        }
    }

    controller_res->can_mmap = false;
    controller_res->stat.st_mode = 0666 | S_IFCHR;
    controller_res->stat.st_rdev = resource_create_dev_id();
    controller_res->read = NULL;
    controller_res->write = NULL;
    controller_res->ioctl = resource_default_ioctl;
    char devname[32];
    snprint(devname, 32, "ahci%lu", ahci_devcount);
    devtmpfs_add_device((struct resource *)controller_res, devname);
    ahci_devcount++;
}

static struct pci_driver ahci_driver = {
    .name = "ahci",
    .match = PCI_MATCH_CLASS | PCI_MATCH_SUBCLASS | PCI_MATCH_PROG_IF,
    .init = ahci_initcontroller,
    .pci_class = 0x01,
    .subclass = 0x06,
    .prog_if = 0x01,
    .vendor = 0,
    .device = 0
};

EXPORT_PCI_DRIVER(ahci_driver);
