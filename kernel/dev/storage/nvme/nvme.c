#include <dev/dev.h>
#include <dev/pci.h>
#include <dev/storage/nvme/nvme.h>
#include <fs/devtmpfs.h>
#include <lib/alloc.h>
#include <lib/print.h>
#include <lib/resource.h>
#include <mm/vmm.h>

struct nvme_device {
    struct resource;
    struct {
        volatile struct nvme_bar *bar;
        uint64_t stride;
        uint64_t queueslots;
        struct nvme_queue queue[2];
        uint64_t lbasize;
        uint64_t lbacount;
        int maxphysrpgs; // maximum number of PRPs (Physical Region Pages)
        uint64_t overwritten;
        struct cachedblock *cache;
        uint64_t cacheblocksize;
        uint64_t maxtransshift;
    } nvme_dev;
};

static size_t nvme_devcount = 0;

static void nvme_createqueue(struct nvme_device *dev, struct nvme_queue *queue, uint64_t slots, uint64_t id) {
    queue->submit = alloc(sizeof(struct nvme_cmd) * slots) - VMM_HIGHER_HALF; // command queue
    queue->submitdb = (uint32_t *)((uint64_t)dev->nvme_dev.bar + PAGE_SIZE + (2 * id * (4 << dev->nvme_dev.stride)));
    queue->sqhead = 0;
    queue->sqtail = 0;
    queue->completion = alloc(sizeof(struct nvme_cmdcomp) * slots) - VMM_HIGHER_HALF; // command result queue
    queue->completedb = (uint32_t *)((uint64_t)dev->nvme_dev.bar + PAGE_SIZE + ((2 * id + 1) * (4 << dev->nvme_dev.stride)));
    queue->cqvec = 0;
    queue->cqhead = 0;
    queue->cqphase = 1;
    queue->elements = slots;
    queue->qid = id;
    queue->cmdid = 0;
    queue->physregpgs = alloc(dev->nvme_dev.maxphysrpgs * slots * sizeof(uint64_t));
}

static ssize_t nvme_setqueuecount(struct nvme_device *dev, int count) {
    struct nvme_cmd cmd = { 0 };
    cmd.features.opcode = NVME_OPSETFT;
    cmd.features.prp1 = 0;
    cmd.features.fid = 0x07; // number of queues
    cmd.features.dword = (count - 1) | ((count - 1) << 16);
    uint16_t status = nvme_awaitsubmitcmd(&dev->nvme_dev.queue[0], cmd);
    if(status != 0) return -1;
    return 0;
}

static ssize_t nvme_createqueues(struct nvme_device *dev, uint16_t qid) {
    nvme_setqueuecount(dev, 4);
    nvme_createqueue(dev, &dev->nvme_dev.queue[1], dev->nvme_dev.queueslots, 1);
    struct nvme_cmd cmd1 = { 0 };
    cmd1.createcompq.opcode = NVME_OPCREATECQ;
    cmd1.createcompq.prp1 = (uint64_t)dev->nvme_dev.queue[1].completion; // any reference to something within the kernel must be subtracted by our higher half (this is omitted here as the queue initialisation code already handles this)
    cmd1.createcompq.cqid = qid;
    cmd1.createcompq.size = dev->nvme_dev.queueslots - 1;
    cmd1.createcompq.cqflags = (1 << 0); // queue phys
    cmd1.createcompq.irqvec = 0;
    uint16_t status = nvme_awaitsubmitcmd(&dev->nvme_dev.queue[0], cmd1);
    if(status != 0) return -1;

    struct nvme_cmd cmd2 = { 0 };
    cmd2.createsubq.opcode = NVME_OPCREATESQ;
    cmd2.createsubq.prp1 = (uint64_t)dev->nvme_dev.queue[1].submit;
    cmd2.createsubq.sqid = qid;
    cmd2.createsubq.cqid = qid;
    cmd2.createsubq.size = dev->nvme_dev.queueslots - 1;
    cmd2.createsubq.sqflags = (1 << 0) | (2 << 1); // queue phys + medium priority
    status = nvme_awaitsubmitcmd(&dev->nvme_dev.queue[0], cmd2);
    if(status != 0) return -1;
    return 0;
}

// submit a command
void nvme_submitcmd(struct nvme_queue *queue, struct nvme_cmd cmd) {
    uint16_t tail = queue->sqtail; // tail of the submit queue
    queue->submit[tail] = cmd; // add to tail (end of queue)
    tail++;
    if(tail == queue->elements) tail = 0;
    *(queue->submitdb) = tail; // set to tail
    queue->sqtail = tail; // update tail so now it'll point to the element after (nothing until we submit a new command)
}

// submit a command an wait for completion
uint16_t nvme_awaitsubmitcmd(struct nvme_queue *queue, struct nvme_cmd cmd) {
    uint16_t head = queue->cqhead;
    uint16_t phase = queue->cqphase;
    cmd.common.cid = queue->cmdid++;
    nvme_submitcmd(queue, cmd);
    uint16_t status = 0; 

    // TODO: Status (0) never meets phase (1)!
    while(true) {
        status = queue->completion[queue->cqhead].status;
        if((status & 0x01) == phase) break;
    }

    status >>= 1;
    ASSERT_MSG(!status, "nvme: cmd error %x", status);

    head++;
    if(head == queue->elements) {
        head = 0;
        queue->cqphase = !queue->cqphase; // flip phase
    }

    *(queue->completedb) = head;
    queue->cqhead = head;
    return status;
}

static ssize_t nvme_identify(struct nvme_device *dev, struct nvme_id *id) {
    uint64_t len = sizeof(struct nvme_id);
    struct nvme_cmd cmd = { 0 };
    cmd.identify.opcode = NVME_OPIDENTIFY;
    cmd.identify.nsid = 0;
    cmd.identify.cns = 1;
    cmd.identify.prp1 = (uint64_t)id - VMM_HIGHER_HALF;
    int64_t off = (uint64_t)id & (PAGE_SIZE - 1);
    len -= (PAGE_SIZE - off);
    if(len <= 0) cmd.identify.prp2 = 0;
    else {
        uint64_t addr = (uint64_t)id + (PAGE_SIZE - off);
        cmd.identify.prp2 = addr;
    }

    uint16_t status = nvme_awaitsubmitcmd(&dev->nvme_dev.queue[0], cmd);
    if(status != 0) return -1;

    int shift = 12 + NVME_CAPMPSMIN(dev->nvme_dev.bar->capabilities);
    uint64_t maxtransshift;
    if(id->mdts) maxtransshift = shift + id->mdts;
    else maxtransshift = 20;
    dev->nvme_dev.maxtransshift = maxtransshift;
    return 0;
}

ssize_t nvme_nsid(struct nvme_device *dev, int ns, struct nvme_nsid *nsid) {
    struct nvme_cmd cmd = { 0 };
    cmd.identify.opcode = NVME_OPIDENTIFY;
    cmd.identify.nsid = ns; // differentiate from normal identify by passing name space id
    cmd.identify.cns = 0;
    cmd.identify.prp1 = (uint64_t)nsid - VMM_HIGHER_HALF;
    uint16_t status = nvme_awaitsubmitcmd(&dev->nvme_dev.queue[0], cmd);
    if(status != 0) return -1;
    return 0;
}

static ssize_t nvme_rwlba(struct nvme_device *dev, void *buf, uint64_t start, uint64_t count, uint8_t write) {
    kernel_print("nvme: rw count: %llu\n", count);
    if(start + count >= dev->nvme_dev.lbacount) count -= (start + count) - dev->nvme_dev.lbacount;
    int pageoff = (uint64_t)buf & (PAGE_SIZE - 1);
    int shoulduseprp = 0;
    int shoulduseprplist = 0;
    uint32_t cid = dev->nvme_dev.queue[1].cmdid;
    if((count * dev->nvme_dev.lbasize) > PAGE_SIZE) {
        if((count * dev->nvme_dev.lbasize) > (PAGE_SIZE * 2)) {
            int prpcount = ((count - 1) * dev->nvme_dev.lbasize) / PAGE_SIZE;
            ASSERT_MSG(!(prpcount > dev->nvme_dev.maxphysrpgs), "nvme: exceeded phyiscal region pages");
            for(int i = 0; i < prpcount; i++) dev->nvme_dev.queue[1].physregpgs[i + cid * dev->nvme_dev.maxphysrpgs] = ((uint64_t)(buf - VMM_HIGHER_HALF - pageoff) + PAGE_SIZE + i * PAGE_SIZE);
            shoulduseprp = 0;
            shoulduseprplist = 1;
        } shoulduseprp = 1;
    }
    struct nvme_cmd cmd = { 0 };
    cmd.rw.opcode = write ? NVME_OPWRITE : NVME_OPREAD;
    cmd.rw.flags = 0;
    cmd.rw.nsid = 1;
    cmd.rw.control = 0;
    cmd.rw.dsmgmt = 0;
    cmd.rw.ref = 0;
    cmd.rw.apptag = 0;
    cmd.rw.appmask = 0;
    cmd.rw.metadata = 0;
    cmd.rw.slba = start;
    cmd.rw.len = count - 1;
    if(shoulduseprplist) {
        cmd.rw.prp1 = (uint64_t)((uint64_t)buf - VMM_HIGHER_HALF);
        cmd.rw.prp2 = (uint64_t)(&dev->nvme_dev.queue[1].physregpgs[cid * dev->nvme_dev.maxphysrpgs]) - VMM_HIGHER_HALF;
        kernel_print("nvme: used listing %llx, %llx\n", cmd.rw.prp1, cmd.rw.prp2);
    } else if(shoulduseprp) cmd.rw.prp2 = (uint64_t)((uint64_t)buf + PAGE_SIZE - VMM_HIGHER_HALF);
    else cmd.rw.prp1 = (uint64_t)((uint64_t)buf - VMM_HIGHER_HALF);

    uint16_t status = nvme_awaitsubmitcmd(&dev->nvme_dev.queue[1], cmd);
    ASSERT_MSG(!status, "nvme: failed to read/write with status %x\n", status);
    return 0;
}

static ssize_t nvme_findblock(struct nvme_device *dev, uint64_t block) {
    for(uint64_t i = 0; i < 512; i++) {
        if((dev->nvme_dev.cache[i].block == block) && (dev->nvme_dev.cache[i].status)) return i;
    }
    return -1;
}

static ssize_t nvme_cacheblock(struct nvme_device *dev, uint64_t block) {
    int ret, target;

    for(target = 0; target < 512; target++) if(!dev->nvme_dev.cache[target].status) goto found; // find a free cache block

    if(dev->nvme_dev.overwritten == 512) dev->nvme_dev.overwritten = 0;
    target = dev->nvme_dev.overwritten++;

    if(dev->nvme_dev.cache[target].status == NVME_DIRTYCACHE) {
        ret = nvme_rwlba(dev, dev->nvme_dev.cache[target].cache, (dev->nvme_dev.cacheblocksize / dev->nvme_dev.lbasize) * dev->nvme_dev.cache[target].block, dev->nvme_dev.cacheblocksize / dev->nvme_dev.lbasize, 1);
        if(ret == -1) return -1;
    }

    goto notfound;

found:
    dev->nvme_dev.cache[target].cache = alloc(dev->nvme_dev.cacheblocksize); // store cache
notfound:
    ret = nvme_rwlba(dev, dev->nvme_dev.cache[target].cache, (dev->nvme_dev.cacheblocksize / dev->nvme_dev.lbasize) * block, dev->nvme_dev.cacheblocksize / dev->nvme_dev.lbasize, 0);
    if(ret == -1) return ret;

    dev->nvme_dev.cache[target].block = block;
    dev->nvme_dev.cache[target].status = NVME_READYCACHE;

    return target;
}

// read `count` bytes at `loc` into `buf`
int nvme_read(struct resource *this_, void *buf, uint64_t loc, uint64_t count) {
    kernel_print("reading! (%llx, %llx)\n", loc, count);
    spinlock_acquire(&this_->lock);
    
    for(uint64_t progress = 0; progress < count;) {
        uint64_t sector = (loc + progress) / ((struct nvme_device *)this_)->nvme_dev.cacheblocksize;
        int slot = nvme_findblock((struct nvme_device *)this_, sector);
        if(slot == -1) {
            slot = nvme_cacheblock((struct nvme_device *)this_, sector);
            if(slot == -1) {
                spinlock_release(&this_->lock);
                return -1;
            }
        }

        uint64_t chunk = count - progress;
        uint64_t off = (loc + progress) % ((struct nvme_device *)this_)->nvme_dev.cacheblocksize;
        if(chunk > ((struct nvme_device *)this_)->nvme_dev.cacheblocksize - off) chunk = ((struct nvme_device *)this_)->nvme_dev.cacheblocksize - off;
        memcpy(buf + progress, &((struct nvme_device *)this_)->nvme_dev.cache[slot].cache[off], chunk); // copy data chunk into buffer
        progress += chunk;
    }

    spinlock_release(&this_->lock);
    return count;
}

int nvme_write(struct resource *this_, void *buf, uint64_t loc, uint64_t count) {
    spinlock_acquire(&this_->lock);

    for(uint64_t progress = 0; progress < count;) {
        uint64_t sector = (loc + progress) / ((struct nvme_device *)this_)->nvme_dev.cacheblocksize;
        int slot = nvme_findblock((struct nvme_device *)this_, sector);
        if(slot == -1) {
            slot = nvme_cacheblock((struct nvme_device *)this_, sector);
            if(slot == -1) {
                spinlock_release(&this_->lock);
                return -1;
            }
        }

        uint64_t chunk = count - progress;
        uint64_t off = (loc + progress) % ((struct nvme_device *)this_)->nvme_dev.cacheblocksize;
        if(chunk > ((struct nvme_device *)this_)->nvme_dev.cacheblocksize - off) chunk = ((struct nvme_device *)this_)->nvme_dev.cacheblocksize - off;

        memcpy(&((struct nvme_device *)this_)->nvme_dev.cache[slot].cache[off], buf + progress, chunk);
        ((struct nvme_device *)this_)->nvme_dev.cache[slot].status = NVME_DIRTYCACHE;
        progress += chunk;
    }

    spinlock_release(&this_->lock);
    return count;
}

static void nvme_initcontroller(struct pci_device *device) { 
    struct nvme_device *device_res = resource_create(sizeof(struct nvme_device));
    struct pci_bar bar = pci_get_bar(device, 0);

    ASSERT_MSG(bar.is_mmio, "PCI bar is not memory mapped!");
    ASSERT((PCI_READD(device, 0x10) & 0b111) == 0b100);

    volatile struct nvme_bar *base = (struct nvme_bar *)(bar.base);
    device_res->nvme_dev.bar = base;
    pci_set_privl(device, PCI_PRIV_MMIO | PCI_PRIV_BUSMASTER);
    
    uint32_t conf = device_res->nvme_dev.bar->conf;
    if(conf & (1 << 0)) { // controller enabled?
        conf &= ~(1 << 0); // disable controller
        device_res->nvme_dev.bar->conf = conf;
    }

    while(((device_res->nvme_dev.bar->status) & (1 << 0))); // await controller ready
    kernel_print("nvme: NVMe controller disabled\n");
    
    device_res->nvme_dev.stride = NVME_CAPSTRIDE(device_res->nvme_dev.bar->capabilities);
    device_res->nvme_dev.queueslots = NVME_CAPMQES(device_res->nvme_dev.bar->capabilities); 
    nvme_createqueue(device_res, &device_res->nvme_dev.queue[0], device_res->nvme_dev.queueslots, 0); // intialise first queue

    uint32_t aqa = device_res->nvme_dev.queueslots - 1;
    aqa |= aqa << 16;
    aqa |= aqa << 16;
    device_res->nvme_dev.bar->aqa = aqa;
    conf = (0 << 4) | (0 << 11) | (0 << 14) | (6 << 16) | (4 << 20) | (1 << 0); // reinitialise config (along with enabling the controller again)
    device_res->nvme_dev.bar->asq = (uint64_t)device_res->nvme_dev.queue[0].submit;
    device_res->nvme_dev.bar->acq = (uint64_t)device_res->nvme_dev.queue[0].completion;
    device_res->nvme_dev.bar->conf = conf;
    while(true) {
        uint32_t status = device_res->nvme_dev.bar->status;
        if(status & (1 << 0)) break; // ready
        ASSERT_MSG(!(status & (1 << 1)), "nvme: controller status is fatal");
    }
    kernel_print("nvme: reset controller\n");

    struct nvme_id *id = (struct nvme_id *)alloc(sizeof(struct nvme_id));
    ASSERT_MSG(!nvme_identify(device_res, id), "nvme: failed to idenfity NVMe");
    
    struct nvme_nsid *nsid = (struct nvme_nsid *)alloc(sizeof(struct nvme_nsid));
    ASSERT_MSG(!nvme_nsid(device_res, 1, nsid), "nvme: failed to obtain namespace info");

    uint64_t lbashift = nsid->lbaf[nsid->flbas & 0x0f].ds;
    uint64_t maxlbas = 1 << (device_res->nvme_dev.maxtransshift - lbashift);
    device_res->nvme_dev.cacheblocksize = (maxlbas * (1 << lbashift));
    device_res->nvme_dev.maxphysrpgs = (maxlbas * (1 << lbashift)) / PAGE_SIZE;

    ASSERT_MSG(!nvme_createqueues(device_res, 1), "nvme: failed to create IO queues");

    uint64_t formattedlba = nsid->flbas & 0x0f;
    device_res->nvme_dev.cache = alloc(sizeof(struct cachedblock) * 512); // set up our cache
    device_res->nvme_dev.lbasize = 1 << nsid->lbaf[formattedlba].ds;
    device_res->nvme_dev.lbacount = nsid->size; 

    device_res->can_mmap = false; // TODO: mmap
    device_res->read = (void *)nvme_read;
    device_res->write = (void *)nvme_write;
    device_res->ioctl = NULL;

    device_res->stat.st_size = nsid->size * device_res->nvme_dev.lbasize; // total size
    device_res->stat.st_blocks = nsid->size; // blocks are just part of this
    device_res->stat.st_blksize = device_res->nvme_dev.lbasize; // block sizes are the lba size
    device_res->stat.st_rdev = resource_create_dev_id();
    device_res->stat.st_mode = 0666 | S_IFCHR;

    char devname[32];
    snprint(devname, sizeof(devname) - 1, "nvme%lu", nvme_devcount);
    devtmpfs_add_device((struct resource *)device_res, devname); 
    nvme_devcount++; // keep track for device name purposes
}

static struct pci_driver nvme_driver = { .name = "nvme", .match = PCI_MATCH_CLASS | PCI_MATCH_SUBCLASS | PCI_MATCH_PROG_IF, .init = nvme_initcontroller, .pci_class = 0x01, .subclass = 0x08, .prog_if = 0x02, .vendor = 0, .device = 0 };

EXPORT_PCI_DRIVER(nvme_driver);
