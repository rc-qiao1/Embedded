#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/poll.h>

#define DEVICE_NAME "cdev_demo"
#define BUFFER_SIZE 256

/* 环形 FIFO */
struct ring_buffer {
    unsigned char data[BUFFER_SIZE];
    size_t head;       /* 下一次读取的位置 */
    size_t tail;       /* 下一次写入的位置 */
    size_t count;      /* 当前有效数据量 */
};

static struct ring_buffer fifo;

static dev_t dev;
static struct cdev demo_cdev;

/* 保护 FIFO 的全部共享状态 */
static DEFINE_MUTEX(fifo_mutex);

/* 管理等待数据的消费者 */
static DECLARE_WAIT_QUEUE_HEAD(read_queue);


/*
 * 检查 FIFO 是否有数据。
 *
 * wait_event_interruptible() 会反复调用这个条件函数。
 * 使用 Mutex，保证检查 count 时与读写操作同步。
 */
static bool fifo_has_data(void)
{
    bool available;

    mutex_lock(&fifo_mutex);
    available = (fifo.count > 0);
    mutex_unlock(&fifo_mutex);

    return available;
}


/* 用户打开设备时调用 */
static int demo_open(struct inode *inode, struct file *file)
{
    int ret;

    (void)inode;

    /* 这是一个流式设备，不支持文件偏移操作 */
    ret = nonseekable_open(inode, file);
    if (ret < 0)
        return ret;

    pr_info("cdev_demo: device opened\n");
    return 0;
}


/* 用户向设备写入数据：生产者 */
static ssize_t demo_write(struct file *file,
                          const char __user *buf,
                          size_t count,
                          loff_t *ppos)
{
    unsigned char temp[BUFFER_SIZE];
    size_t total = 0;
    size_t chunk;
    size_t i;

    (void)file;
    (void)ppos;

    if (count == 0)
        return 0;

    /*
     * 分块处理任意长度的写入。
     * 每次最多从用户空间复制 BUFFER_SIZE 字节。
     */
    while (total < count) {

        chunk = min_t(size_t, count - total, BUFFER_SIZE);

        /* 先复制到内核临时缓冲区，不持有 FIFO 锁 */
        if (copy_from_user(temp, buf + total, chunk)) {
            pr_err("cdev_demo: copy_from_user failed\n");

            /* 已经成功写入部分数据时，返回部分长度 */
            return total > 0 ? total : -EFAULT;
        }

        mutex_lock(&fifo_mutex);

        for (i = 0; i < chunk; i++) {

            /* FIFO 满：丢弃最旧数据 */
            if (fifo.count == BUFFER_SIZE) {
                fifo.head = (fifo.head + 1) % BUFFER_SIZE;
            } else {
                fifo.count++;
            }

            /* 写入新数据 */
            fifo.data[fifo.tail] = temp[i];

            /* 更新写入位置 */
            fifo.tail = (fifo.tail + 1) % BUFFER_SIZE;
        }

        pr_info("cdev_demo: received %zu bytes, fifo count=%zu\n",
                chunk, fifo.count);

        mutex_unlock(&fifo_mutex);

        /*
         * 数据已经写入，count 已经更新，
         * 此时再唤醒等待数据的消费者。
         */
        wake_up_interruptible(&read_queue);

        total += chunk;
    }

    return total;
}


/* 用户从设备读取数据：消费者 */
static ssize_t demo_read(struct file *file,
                         char __user *buf,
                         size_t count,
                         loff_t *ppos)
{
    int ret;
    size_t read_len;
    size_t first_len;
    size_t second_len;
    size_t copied_first;
    size_t copied_second = 0;
    size_t not_copied;
    size_t total_copied;

    (void)ppos;

    if (count == 0)
        return 0;

    /*
     * 等待 FIFO 有数据。
     * 非阻塞模式不等待，直接检查 FIFO。
     */
    for (;;) {

        if (file->f_flags & O_NONBLOCK) {

            mutex_lock(&fifo_mutex);

            if (fifo.count == 0) {
                mutex_unlock(&fifo_mutex);
                return -EAGAIN;
            }

            /* 有数据，继续读取，此时仍持有 Mutex */
            break;
        }

        /*
         * 阻塞模式：
         * 条件不成立时进入等待队列。
         * 被唤醒或收到信号后，wait_event_interruptible 返回。
         */
        ret = wait_event_interruptible(read_queue,
                                       fifo_has_data());

        if (ret)
            return ret;

        /*
         * 被唤醒不代表一定有数据。
         * 重新获取锁并检查，防止其他消费者已经消费数据。
         */
        mutex_lock(&fifo_mutex);

        if (fifo.count > 0)
            break;

        mutex_unlock(&fifo_mutex);
    }

    /*
     * 到这里已经获得 fifo_mutex，
     * 并且确认 FIFO 中至少有一个字节。
     */

    read_len = min(count, fifo.count);

    /* 计算环形缓冲区的两段数据 */
    first_len = min(read_len, BUFFER_SIZE - fifo.head);
    second_len = read_len - first_len;

    /* 复制第一段 */
    not_copied = copy_to_user(buf,
                              fifo.data + fifo.head,
                              first_len);

    copied_first = first_len - not_copied;

    /*
     * 第一段没有完全复制成功：
     * 只消费已经成功复制的部分。
     */
    if (not_copied != 0) {

        if (copied_first > 0) {
            fifo.head = (fifo.head + copied_first) % BUFFER_SIZE;
            fifo.count -= copied_first;
        }

        mutex_unlock(&fifo_mutex);

        return copied_first > 0 ? copied_first : -EFAULT;
    }

    /* 复制第二段（发生环形回绕时才有） */
    if (second_len > 0) {

        not_copied = copy_to_user(buf + first_len,
                                  fifo.data,
                                  second_len);

        copied_second = second_len - not_copied;
    }

    /* 只消费已经成功复制的数据 */
    total_copied = copied_first + copied_second;

    if (total_copied > 0) {
        fifo.head = (fifo.head + total_copied) % BUFFER_SIZE;
        fifo.count -= total_copied;
    }

    pr_info("cdev_demo: read %zu bytes, fifo count=%zu\n",
            total_copied, fifo.count);

    mutex_unlock(&fifo_mutex);

    if (total_copied == 0)
        return -EFAULT;

    return total_copied;
}


/* poll：向内核报告设备当前是否可读 */
static __poll_t demo_poll(struct file *file, poll_table *wait)
{
    __poll_t mask = 0;

    /*
     * 先注册等待队列，再检查数据状态。
     * 避免检查与注册之间发生丢失唤醒。
     */
    poll_wait(file, &read_queue, wait);

    mutex_lock(&fifo_mutex);

    if (fifo.count > 0)
        mask = EPOLLIN | EPOLLRDNORM;

    mutex_unlock(&fifo_mutex);

    return mask;
}


/* 文件操作函数表 */
static const struct file_operations demo_fops = {
    .owner = THIS_MODULE,
    .open = demo_open,
    .read = demo_read,
    .write = demo_write,
    .poll = demo_poll,
    .llseek = no_llseek,
};


/* 驱动模块初始化 */
static int __init demo_init(void)
{
    int ret;

    ret = alloc_chrdev_region(&dev, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("cdev_demo: alloc_chrdev_region failed\n");
        return ret;
    }

    cdev_init(&demo_cdev, &demo_fops);

    ret = cdev_add(&demo_cdev, dev, 1);
    if (ret < 0) {
        unregister_chrdev_region(dev, 1);
        return ret;
    }

    pr_info("cdev_demo: registered, major=%d minor=%d\n",
            MAJOR(dev), MINOR(dev));

    return 0;
}


/* 驱动模块退出 */
static void __exit demo_exit(void)
{
    cdev_del(&demo_cdev);
    unregister_chrdev_region(dev, 1);

    pr_info("cdev_demo: module unloaded\n");
}


module_init(demo_init);
module_exit(demo_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rcqiao");
MODULE_DESCRIPTION("Character device with ring FIFO, blocking I/O and poll");
