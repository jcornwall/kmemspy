#ifndef _UAPI_LINUX_KMEMSPY_H
#define _UAPI_LINUX_KMEMSPY_H

#include <linux/ioctl.h>
#include <linux/types.h>

struct kmemspy_read_page_virt_args {
	__u64 pfn_virt;	/* in */
	__u64 pte;	/* out */
	__u64 data_buf;	/* in */
	__u32 pid;	/* in */
};

struct kmemspy_read_page_phys_args {
	__u64 pfn_phys;	/* in */
	__u64 data_buf;	/* in */
};

#define KMEMSPY_IOCTL_MAGIC 'M'
#define KMEMSPY_IOC_READ_PAGE_VIRT _IOWR(KMEMSPY_IOCTL_MAGIC, 1, \
					 struct kmemspy_read_page_virt_args)
#define KMEMSPY_IOC_READ_PAGE_PHYS _IOW(KMEMSPY_IOCTL_MAGIC, 2, \
					 struct kmemspy_read_page_phys_args)

#endif /* _UAPI_LINUX_KMEMSPY_H */
