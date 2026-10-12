// SPDX-License-Identifier: GPL-2.0
/* Task 1: character driver with a shared buffer and four ioctl commands.
 * Adapted in structure from the student's LAB-02 driver, with a new ABI.
 */
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include "asgn1_ioctl.h"

#define ASGN1_NAME "asgn1"
#define ASGN1_CLASS "asgn1_class"
#define ASGN1_MAJOR 241
#define ASGN1_CAPACITY 1024
#define ASGN1_PREFIX "[KERNEL]"

/* Data layer: data_lock protects every buffer field, mode and statistic. */
static DEFINE_MUTEX(data_lock);
static char device_buffer[ASGN1_CAPACITY];
static size_t buffer_len;
static int mode = 1; /* 0: add prefix; 1: echo input unchanged. */
static int open_count;
static int write_count;
static int read_count;
static int last_write_size;
static struct class *asgn1_class;
static struct device *asgn1_device;
static const char version[ASGN1_VERSION_SIZE] =
    "asgn1_driver v1.0 - SE203347";

/* Count successful opens; inode/file identify the device and open instance.
 * Return: 0, or -ERESTARTSYS when interrupted while waiting for the mutex.
 */
static int asgn1_open(struct inode *inode, struct file *file)
{
    if (mutex_lock_interruptible(&data_lock))
        return -ERESTARTSYS;
    open_count++;
    mutex_unlock(&data_lock);
    return 0;
}

/* Finish one open instance. Parameters: inode and file. Return: 0. */
static int asgn1_release(struct inode *inode, struct file *file)
{
    return 0;
}

/* Copy bytes at *ppos to userspace, advancing *ppos on success.
 * Parameters: file, destination user, byte count, file offset.
 * Return: copied bytes, 0 at EOF, or negative errno.
 */
static ssize_t asgn1_read(struct file *file, char __user *user,
                          size_t count, loff_t *ppos)
{
    size_t n;

    if (*ppos < 0)
        return -EINVAL;
    if (count == 0)
        return 0;
    if (mutex_lock_interruptible(&data_lock))
        return -ERESTARTSYS;
    if (*ppos >= (loff_t)buffer_len) {
        mutex_unlock(&data_lock);
        return 0;
    }
    n = min(count, buffer_len - (size_t)*ppos);
    if (copy_to_user(user, device_buffer + (size_t)*ppos, n)) {
        mutex_unlock(&data_lock);
        return -EFAULT;
    }
    *ppos += n;
    read_count++; /* Count successful nonempty read calls; EOF does not count. */
    mutex_unlock(&data_lock);
    pr_info("asgn1: read %zu bytes\n", n);
    return n;
}

/* Replace the internal buffer with user data, adding a prefix in mode 0.
 * Parameters: file, user source, byte count, unused file offset.
 * Return: input byte count on success, or negative errno.
 */
static ssize_t asgn1_write(struct file *file, const char __user *user,
                           size_t count, loff_t *ppos)
{
    char *staged;
    size_t prefix_len, total;
    ssize_t ret;

    if (count == 0)
        return 0;
    if (count >= ASGN1_CAPACITY)
        return -EMSGSIZE;
    staged = kmalloc(count, GFP_KERNEL);
    if (!staged)
        return -ENOMEM;
    if (copy_from_user(staged, user, count)) {
        ret = -EFAULT;
        goto free_staged;
    }

    if (mutex_lock_interruptible(&data_lock)) {
        ret = -ERESTARTSYS;
        goto free_staged;
    }
    prefix_len = mode == 0 ? sizeof(ASGN1_PREFIX) - 1 : 0;
    total = prefix_len + count;
    if (total >= ASGN1_CAPACITY) {
        ret = -EMSGSIZE;
        goto unlock;
    }
    if (prefix_len)
        memcpy(device_buffer, ASGN1_PREFIX, prefix_len);
    memcpy(device_buffer + prefix_len, staged, count);
    device_buffer[total] = '\0';
    buffer_len = total;
    write_count++;
    last_write_size = count; /* Input bytes; prefix is not included. */
    pr_info("asgn1: write %zu bytes, mode=%d, stored=%zu\n",
            count, mode, total);
    ret = count;
unlock:
    mutex_unlock(&data_lock);
free_staged:
    kfree(staged);
    return ret;
}

/* Dispatch control commands; arg is a userspace address except for RESET.
 * Parameters: open file, encoded command, userspace argument address.
 * Return: 0 on success, -ENOTTY for unknown command, or negative errno.
 */
static long asgn1_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    void __user *user = (void __user *)arg;
    struct asgn1_stats stats;
    int requested_mode;

    switch (cmd) {
    case ASGN1_RESET_BUFFER:
        pr_info("asgn1: ioctl RESET_BUFFER\n");
        if (mutex_lock_interruptible(&data_lock))
            return -ERESTARTSYS;
        memset(device_buffer, 0, sizeof(device_buffer));
        buffer_len = 0;
        write_count = 0;
        read_count = 0;
        last_write_size = 0;
        /* open_count and mode describe the module/session, so we keep them. */
        mutex_unlock(&data_lock);
        return 0;

    case ASGN1_GET_STATS:
        pr_info("asgn1: ioctl GET_STATS\n");
        memset(&stats, 0, sizeof(stats));
        if (mutex_lock_interruptible(&data_lock))
            return -ERESTARTSYS;
        stats.open_count = open_count;
        stats.write_count = write_count;
        stats.read_count = read_count;
        stats.buffer_len = buffer_len;
        stats.last_write_size = last_write_size;
        mutex_unlock(&data_lock);
        return copy_to_user(user, &stats, sizeof(stats)) ? -EFAULT : 0;

    case ASGN1_SET_MODE:
        pr_info("asgn1: ioctl SET_MODE\n");
        if (copy_from_user(&requested_mode, user, sizeof(requested_mode)))
            return -EFAULT;
        if (requested_mode != 0 && requested_mode != 1)
            return -EINVAL;
        if (mutex_lock_interruptible(&data_lock))
            return -ERESTARTSYS;
        mode = requested_mode;
        mutex_unlock(&data_lock);
        return 0;

    case ASGN1_GET_VERSION:
        pr_info("asgn1: ioctl GET_VERSION\n");
        /* The array is 64 bytes and NUL-terminated. Never copy beyond it. */
        return copy_to_user(user, version, sizeof(version)) ? -EFAULT : 0;

    default:
        pr_info("asgn1: ioctl unknown command=0x%x\n", cmd);
        return -ENOTTY;
    }
}

static const struct file_operations asgn1_fops = {
    .owner = THIS_MODULE,
    .open = asgn1_open,
    .release = asgn1_release,
    .read = asgn1_read,
    .write = asgn1_write,
    .unlocked_ioctl = asgn1_ioctl,
};

/* Register the major, class and device; roll back on partial failure.
 * Return: 0 on success or negative errno.
 */
static int __init asgn1_init(void)
{
    int ret;

    ret = register_chrdev(ASGN1_MAJOR, ASGN1_NAME, &asgn1_fops);
    if (ret < 0)
        return ret;
    asgn1_class = class_create(THIS_MODULE, ASGN1_CLASS); /* Linux 5.15 API */
    if (IS_ERR(asgn1_class)) {
        ret = PTR_ERR(asgn1_class);
        goto unregister;
    }
    asgn1_device = device_create(asgn1_class, NULL,
                                 MKDEV(ASGN1_MAJOR, 0), NULL, ASGN1_NAME);
    if (IS_ERR(asgn1_device)) {
        ret = PTR_ERR(asgn1_device);
        goto destroy_class;
    }
    pr_info("asgn1: loaded major=%d mode=%d\n", ASGN1_MAJOR, mode);
    return 0;

destroy_class:
    class_destroy(asgn1_class);
unregister:
    unregister_chrdev(ASGN1_MAJOR, ASGN1_NAME);
    return ret;
}

/* Release every resource created by init. Parameters: none. Return: void. */
static void __exit asgn1_exit(void)
{
    device_destroy(asgn1_class, MKDEV(ASGN1_MAJOR, 0));
    class_destroy(asgn1_class);
    unregister_chrdev(ASGN1_MAJOR, ASGN1_NAME);
    pr_info("asgn1: unloaded\n");
}

module_init(asgn1_init);
module_exit(asgn1_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nguyen Huy Hoang, SE203347");
MODULE_DESCRIPTION("Assignment 01 character driver with ioctl interface");
MODULE_VERSION("1.0");
