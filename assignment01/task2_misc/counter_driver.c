// SPDX-License-Identifier: GPL-2.0
/* Task 2: independent misc device, atomic counter and procfs statistics. */
#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>

/* State layer. state_lock protects compound read/increment/write transitions
 * and the operation totals; atomic_t makes the counter's primitive access safe.
 */
static atomic_t counter = ATOMIC_INIT(0);
static DEFINE_MUTEX(state_lock);
static unsigned int total_reads;
static unsigned int total_writes;
static struct proc_dir_entry *counter_proc;

/* A snapshot belongs to one open file, so short reads cannot increment twice. */
struct counter_file_state {
    char line[32];
    size_t length;
    bool prepared;
};

/* Allocate a per-open snapshot holder. inode/file identify this instance.
 * Return: 0 on success or -ENOMEM.
 */
static int counter_open(struct inode *inode, struct file *file)
{
    struct counter_file_state *state = kzalloc(sizeof(*state), GFP_KERNEL);

    if (!state)
        return -ENOMEM;
    file->private_data = state;
    return 0;
}

/* Release the per-open holder. Parameters: inode/file. Return: 0. */
static int counter_release(struct inode *inode, struct file *file)
{
    kfree(file->private_data);
    return 0;
}

/* Get the current value once per open, then serve decimal text through *ppos.
 * Parameters: file, user destination, requested count, file offset.
 * Return: bytes copied, 0 at EOF, or negative errno.
 */
static ssize_t counter_read(struct file *file, char __user *user,
                            size_t count, loff_t *ppos)
{
    struct counter_file_state *state = file->private_data;
    ssize_t copied;
    int value;

    if (*ppos < 0)
        return -EINVAL;
    if (count == 0)
        return 0;
    if (!state->prepared) {
        if (mutex_lock_interruptible(&state_lock))
            return -ERESTARTSYS;
        value = atomic_read(&counter);
        if (value == INT_MAX) {
            mutex_unlock(&state_lock);
            return -EOVERFLOW;
        }
        state->length = scnprintf(state->line, sizeof(state->line),
                                  "%d\n", value);
        copied = simple_read_from_buffer(user, count, ppos,
                                         state->line, state->length);
        if (copied > 0) {
            state->prepared = true;
            atomic_inc(&counter);
            total_reads++;
            pr_info("counter_driver: read value=%d next=%d\n",
                    value, atomic_read(&counter));
        }
        mutex_unlock(&state_lock);
        return copied;
    }
    return simple_read_from_buffer(user, count, ppos,
                                   state->line, state->length);
}

/* Parse an ASCII nonnegative integer and set the shared counter.
 * Parameters: file, user source, byte count, unused file offset.
 * Return: input byte count on success or -EINVAL/-EFAULT/negative errno.
 */
static ssize_t counter_write(struct file *file, const char __user *user,
                             size_t count, loff_t *ppos)
{
    char input[32];
    int value;

    if (count == 0 || count >= sizeof(input))
        return -EINVAL;
    if (copy_from_user(input, user, count))
        return -EFAULT;
    input[count] = '\0';
    if (kstrtoint(input, 10, &value) || value < 0)
        return -EINVAL;
    if (mutex_lock_interruptible(&state_lock))
        return -ERESTARTSYS;
    atomic_set(&counter, value);
    total_writes++;
    pr_info("counter_driver: write set=%d\n", value);
    mutex_unlock(&state_lock);
    return count;
}

static const struct file_operations counter_fops = {
    .owner = THIS_MODULE,
    .open = counter_open,
    .release = counter_release,
    .read = counter_read,
    .write = counter_write,
};

static struct miscdevice counter_misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "counter",
    .fops = &counter_fops,
    .mode = 0600,
};

/* Format a consistent view for /proc/counter_info; seq_file handles EOF.
 * Parameters: seq_file target, unused iterator pointer. Return: 0/errno.
 */
static int counter_proc_show(struct seq_file *m, void *v)
{
    if (mutex_lock_interruptible(&state_lock))
        return -ERESTARTSYS;
    seq_printf(m, "counter: %d\n", atomic_read(&counter));
    seq_printf(m, "total_reads: %u\n", total_reads);
    seq_printf(m, "total_writes: %u\n", total_writes);
    mutex_unlock(&state_lock);
    return 0;
}

/* Open the one-record seq_file. Parameters: inode/file. Return: 0/errno. */
static int counter_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, counter_proc_show, NULL);
}

static const struct proc_ops counter_proc_ops = {
    .proc_open = counter_proc_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

/* Register the misc device and proc entry, rolling back on failure.
 * Parameters: none. Return: 0 or negative errno.
 */
static int __init counter_init(void)
{
    int ret = misc_register(&counter_misc);

    if (ret)
        return ret;
    counter_proc = proc_create("counter_info", 0444, NULL,
                               &counter_proc_ops);
    if (!counter_proc) {
        misc_deregister(&counter_misc);
        return -ENOMEM;
    }
    pr_info("counter_driver: loaded major=%d minor=%d\n",
            MISC_MAJOR, counter_misc.minor);
    return 0;
}

/* Remove proc entry and device. Parameters: none. Return: void. */
static void __exit counter_exit(void)
{
    proc_remove(counter_proc);
    misc_deregister(&counter_misc);
    pr_info("counter_driver: unloaded\n");
}

module_init(counter_init);
module_exit(counter_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nguyen Huy Hoang, SE203347");
MODULE_DESCRIPTION("Assignment 01 misc counter device with procfs statistics");
MODULE_VERSION("1.0");
