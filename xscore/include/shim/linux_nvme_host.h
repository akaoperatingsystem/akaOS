#ifndef _LINUX_NVME_HOST_SHIM_H
#define _LINUX_NVME_HOST_SHIM_H

#include "linux_nvme.h"

// XSCore specific abstractions for the NVMe driver
typedef void* workqueue_struct;
typedef void* mutex;
typedef void* cdev;
typedef void* blk_mq_tag_set;

#define HZ 100

// Map common Linux kernel synchronization and workqueue APIs to XSCore equivalents
// For now, these are stubs to allow compilation
#define INIT_WORK(work, fn)
#define schedule_work(work)
#define mutex_lock(lock)
#define mutex_unlock(lock)

#endif
