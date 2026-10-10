#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mutex.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Embedded Linux Lab");
MODULE_DESCRIPTION("LAB-02: Simple Character Device Driver");
MODULE_VERSION("1.0");

#define DEVICE_NAME  "lab2"
#define CLASS_NAME   "lab2_class"
#define BUFFER_SIZE  1024
#define DRIVER_MAJOR 240

static int major_number = DRIVER_MAJOR;
static char device_buffer[BUFFER_SIZE];
static int buffer_len;
static int open_count;
static struct class *lab2_class;
static struct device *lab2_device;

/* Bao ve device_buffer va cac bien thong ke khi nhieu process truy cap. */
static DEFINE_MUTEX(lab2_mutex);
static struct proc_dir_entry *proc_entry;

static int lab2_proc_show(struct seq_file *m, void *v)
{
    mutex_lock(&lab2_mutex);
    seq_printf(m, "Driver: %s\n", DEVICE_NAME);
    seq_printf(m, "Major: %d\n", major_number);
    seq_printf(m, "Buffer size: %d\n", BUFFER_SIZE);
    seq_printf(m, "Data length: %d\n", buffer_len);
    seq_printf(m, "Open count: %d\n", open_count);
    seq_printf(m, "Last data: %s\n", device_buffer);
    mutex_unlock(&lab2_mutex);
    return 0;
}
static int lab2_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, lab2_proc_show, NULL);
}

static const struct proc_ops lab2_proc_fops = {
    .proc_open    = lab2_proc_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};
static ssize_t buffer_len_show(struct device *dev,
                               struct device_attribute *attr, char *buf)
{
    int len;

    mutex_lock(&lab2_mutex);
    len = buffer_len;
    mutex_unlock(&lab2_mutex);
    return sysfs_emit(buf, "%d\n", len);
}
static DEVICE_ATTR_RO(buffer_len);

static ssize_t open_count_show(struct device *dev,
                               struct device_attribute *attr, char *buf)
{
    int count;

    mutex_lock(&lab2_mutex);
    count = open_count;
    mutex_unlock(&lab2_mutex);
    return sysfs_emit(buf, "%d\n", count);
}
static DEVICE_ATTR_RO(open_count);
static ssize_t last_data_show(struct device *dev,
                              struct device_attribute *attr, char *buf)
{
    ssize_t n;

    mutex_lock(&lab2_mutex);
    n = sysfs_emit(buf, "%.*s\n", buffer_len, device_buffer);
    mutex_unlock(&lab2_mutex);
    return n;
}
static DEVICE_ATTR_RO(last_data);

static struct attribute *lab2_attrs[] = {
    &dev_attr_buffer_len.attr,
    &dev_attr_open_count.attr,
    &dev_attr_last_data.attr,
    NULL,
};

static const struct attribute_group lab2_attr_group = {
    .attrs = lab2_attrs,
};

/* Goi khi userspace mo /dev/lab2; dem tong so lan mo. */
static int lab2_open(struct inode *inode, struct file *file)
{
    int count;

    mutex_lock(&lab2_mutex);
    count = ++open_count;
    mutex_unlock(&lab2_mutex);

    pr_info("lab2_driver: device opened (count=%d)\n", count);
    return 0;
}

/* Goi khi userspace dong file descriptor cua thiet bi. */
static int lab2_release(struct inode *inode, struct file *file)
{
    pr_info("lab2_driver: device closed\n");
    return 0;
}

/* Dua du lieu tu kernel buffer sang user buffer. */
static ssize_t lab2_read(struct file *file, char __user *buf,
                         size_t len, loff_t *offset)
{
    size_t available;

    if (*offset < 0)
        return -EINVAL;

    mutex_lock(&lab2_mutex);
    if (*offset >= buffer_len) {
        mutex_unlock(&lab2_mutex);
        return 0; /* EOF */
    }

    available = buffer_len - *offset;
    if (len > available)
        len = available;

    /* User pointer can canh gioi kernel/user: khong dung memcpy(). */
    if (copy_to_user(buf, device_buffer + *offset, len)) {
        mutex_unlock(&lab2_mutex);
        return -EFAULT;
    }

    *offset += len;
    mutex_unlock(&lab2_mutex);
    pr_info("lab2_driver: sent %zu bytes to user\n", len);
    return len;
}

/* Nhan du lieu tu user buffer vao kernel buffer. */
static ssize_t lab2_write(struct file *file, const char __user *buf,
                          size_t len, loff_t *offset)
{
    if (len >= BUFFER_SIZE)
        len = BUFFER_SIZE - 1;

    mutex_lock(&lab2_mutex);
    /* copy_from_user() kiem tra va xu ly user pointer an toan. */
    if (copy_from_user(device_buffer, buf, len)) {
        mutex_unlock(&lab2_mutex);
        return -EFAULT;
    }

    buffer_len = len;
    device_buffer[buffer_len] = '\0';
    mutex_unlock(&lab2_mutex);

    pr_info("lab2_driver: received %zu bytes\n", len);
    return len;
}

static const struct file_operations lab2_fops = {
    .owner   = THIS_MODULE,
    .open    = lab2_open,
    .release = lab2_release,
    .read    = lab2_read,
    .write   = lab2_write,
};

/* Goi khi insmod: dang ky driver va tao device trong sysfs. */
static int __init lab2_init(void)
{
    int ret;

    pr_info("lab2_driver: initializing module\n");

    ret = register_chrdev(major_number, DEVICE_NAME, &lab2_fops);
    if (ret < 0) {
        pr_err("lab2_driver: register_chrdev failed: %d\n", ret);
        return ret;
    }

    lab2_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(lab2_class)) {
        ret = PTR_ERR(lab2_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        return ret;
    }

    lab2_device = device_create(lab2_class, NULL,
                                MKDEV(major_number, 0), NULL, DEVICE_NAME);
    if (IS_ERR(lab2_device)) {
        ret = PTR_ERR(lab2_device);
        class_destroy(lab2_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        return ret;
    }
    proc_entry = proc_create("lab2_info", 0444, NULL, &lab2_proc_fops);
    if (!proc_entry) {
        device_destroy(lab2_class, MKDEV(major_number, 0));
        class_destroy(lab2_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        return -ENOMEM;
    }
    ret = sysfs_create_group(&lab2_device->kobj, &lab2_attr_group);
    if (ret) {
        proc_remove(proc_entry);
        device_destroy(lab2_class, MKDEV(major_number, 0));
        class_destroy(lab2_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        return ret;
    }

    pr_info("lab2_driver: loaded, major=%d\n", major_number);
    return 0;
}

/* Goi khi rmmod: thu hoi device, class va major number. */
static void __exit lab2_exit(void)
{
    sysfs_remove_group(&lab2_device->kobj, &lab2_attr_group);
    proc_remove(proc_entry);
    device_destroy(lab2_class, MKDEV(major_number, 0));
    class_destroy(lab2_class);
    unregister_chrdev(major_number, DEVICE_NAME);
    pr_info("lab2_driver: module unloaded\n");
}

module_init(lab2_init);
module_exit(lab2_exit);
