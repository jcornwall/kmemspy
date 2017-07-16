#include "kmemspy.h"

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/highmem.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jay Cornwall");
MODULE_DESCRIPTION("Grants user mode read access to all memory");

static struct cdev kmemspy_cdev;
static struct class *kmemspy_class;

static int kmemspy_read_page_virt(void __user *argp)
{
	struct kmemspy_read_page_virt_args args;
	struct task_struct *task;
	struct mm_struct *mm;
	struct page *page;
	struct vm_area_struct *vma;
	pgprot_t page_prot;
	void *page_kaddr;

	if (copy_from_user(&args, argp, sizeof(args)))
		return -EFAULT;

	/* Find mm associated with pid. */
	rcu_read_lock();

	task = pid_task(find_vpid(args.pid), PIDTYPE_PID);
	if (!task) {
		rcu_read_unlock();
		return -EINVAL;
	}

	mm = get_task_mm(task);
	if (!mm) {
		rcu_read_unlock();
		return -EINVAL;
	}

	rcu_read_unlock();

	/* Reference system memory page associated with virtual pfn. */
	down_read(&mm->mmap_sem);

	if (get_user_pages_remote(NULL, mm, args.pfn_virt << PAGE_SHIFT, 1, 0,
				  &page, &vma, NULL) <= 0) {
		up_read(&mm->mmap_sem);
		mmput(mm);
		return -EINVAL;
	}

	if (!page_is_ram(page_to_pfn(page))) {
		up_read(&mm->mmap_sem);
		mmput(mm);
		put_page(page);
		return -EINVAL;
	}

	page_prot = vma->vm_page_prot;
	up_read(&mm->mmap_sem);
	mmput(mm);

	/* Copy contents of page to user. */
	page_kaddr = kmap(page);

	if (copy_to_user((void __user *)args.data_buf, page_kaddr, PAGE_SIZE)) {
		kunmap(page);
		put_page(page);
		return -EFAULT;
	}

	/* Form PTE and copy to user. */
	args.pte = page_to_phys(page) | page_prot.pgprot;

	if (copy_to_user(argp, &args, sizeof(args)))
		return -EFAULT;

	kunmap(page);
	put_page(page);

	return 0;
}

static int kmemspy_read_page_phys(void __user *argp)
{
	struct kmemspy_read_page_phys_args args;
	struct page *page;
	void *page_kaddr;

	if (copy_from_user(&args, argp, sizeof(args)))
		return -EFAULT;

	/* Copy contents of system memory page to user. */
	if (!page_is_ram(args.pfn_phys))
		return -EINVAL;

	page = pfn_to_page(args.pfn_phys);
	page_kaddr = kmap(page);

	if (copy_to_user((void __user *)args.data_buf, page_kaddr, PAGE_SIZE)) {
		kunmap(page);
		return -EFAULT;
	}

	kunmap(page);
	return 0;
}

static long kmemspy_ioctl(struct file *filp, unsigned int cmd,
			  unsigned long arg)
{
	void __user *argp = (void __user *)arg;

	if (_IOC_TYPE(cmd) != KMEMSPY_IOCTL_MAGIC)
		return -ENOTTY;

	switch (cmd) {
	case KMEMSPY_IOC_READ_PAGE_VIRT:
		return kmemspy_read_page_virt(argp);
	case KMEMSPY_IOC_READ_PAGE_PHYS:
		return kmemspy_read_page_phys(argp);
	default:
		return -ENOTTY;
	}
}

static const struct file_operations kmemspy_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = kmemspy_ioctl
};

static int __init kmemspy_init(void)
{
	int ret;
	dev_t dev_num;
	struct device *kmemspy_device;

	ret = alloc_chrdev_region(&dev_num, 0, 1, "kmemspy");
	if (ret < 0) {
		pr_err("kmemspy: Failed to register character device\n");
		goto fail_alloc_chrdev;
	}

	cdev_init(&kmemspy_cdev, &kmemspy_fops);
	kmemspy_cdev.owner = THIS_MODULE;

	ret = cdev_add(&kmemspy_cdev, dev_num, 1);
	if (ret < 0) {
		pr_err("kmemspy: Failed to register character device\n");
		goto fail_cdev_add;
	}

	kmemspy_class = class_create(THIS_MODULE, "kmemspy");
	if (IS_ERR(kmemspy_class)) {
		pr_err("kmemspy: Failed to register device class\n");
		ret = PTR_ERR(kmemspy_class);
		goto fail_class_create;
	}

	kmemspy_device = device_create(kmemspy_class, NULL, dev_num, NULL,
				       "kmemspy");
	if (IS_ERR(kmemspy_device)) {
		pr_err("kmemspy: Failed to create device\n");
		ret = PTR_ERR(kmemspy_device);
		goto fail_device_create;
	}

	pr_info("kmemspy: All memory is now accessible from user mode\n");
	return 0;

fail_device_create:
	class_destroy(kmemspy_class);
fail_class_create:
	cdev_del(&kmemspy_cdev);
fail_cdev_add:
	unregister_chrdev_region(dev_num, 1);
fail_alloc_chrdev:
	return ret;
}

static void __exit kmemspy_exit(void)
{
	dev_t dev_num = kmemspy_cdev.dev;

	device_destroy(kmemspy_class, dev_num);
	class_destroy(kmemspy_class);
	cdev_del(&kmemspy_cdev);
	unregister_chrdev_region(dev_num, 1);

	pr_info("kmemspy: Shutting down\n");
}

module_init(kmemspy_init);
module_exit(kmemspy_exit);
