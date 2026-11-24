
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>

#define DEVICE_NAME "cdev_demo"

static dev_t dev;
static struct cdev demo_cdev;

/* 用户打开设备时调用 */
static int demo_open(struct inode *inode, struct file *file)
{
    pr_info("cdev_demo: device opened\n");
    return 0;
}

/* 用户向设备写入数据时调用 */
static ssize_t demo_write(struct file *file,
                          const char __user *buf,
                          size_t count,
                          loff_t *ppos)
{
    pr_info("cdev_demo: write called, count=%zu\n", count);
    return count;
}

/* 驱动向内核提供的操作函数表 */
static const struct file_operations demo_fops = {
    .owner = THIS_MODULE,
    .open = demo_open,
    .write = demo_write,
};

/* 模块加载：申请设备号并注册字符设备 */
static int __init cdev_demo_init(void)
{
    int ret;

    /* 1. 动态申请一个设备号 */
    ret = alloc_chrdev_region(&dev, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("cdev_demo: failed to allocate device number\n");
        return ret;
    }

    pr_info("cdev_demo: major=%d, minor=%d\n",
            MAJOR(dev), MINOR(dev));

    /* 2. 初始化 cdev，并关联操作函数表 */
    cdev_init(&demo_cdev, &demo_fops);

    /* 3. 将 cdev 注册到内核 */
    ret = cdev_add(&demo_cdev, dev, 1);
    if (ret < 0) {
        pr_err("cdev_demo: failed to add cdev\n");
        unregister_chrdev_region(dev, 1);
        return ret;
    }

    pr_info("cdev_demo: character device registered\n");

    return 0;
}

/* 模块卸载：注销字符设备并释放设备号 */
static void __exit cdev_demo_exit(void)
{
    cdev_del(&demo_cdev);

    unregister_chrdev_region(dev, 1);

    pr_info("cdev_demo: character device unregistered\n");
}

module_init(cdev_demo_init);
module_exit(cdev_demo_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Character device registration demo");
MODULE_AUTHOR("rcqiao");
