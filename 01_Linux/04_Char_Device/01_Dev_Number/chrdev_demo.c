#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>

static dev_t dev;

static int __init chrdev_demo_init(void)
{
    int ret;

    ret = alloc_chrdev_region(&dev, 0, 2, "chrdev_demo");
    if (ret < 0) {
        pr_err("chrdev_demo: failed to allocate device number\n");
        return ret;
    }

    pr_info("chrdev_demo: registered major=%d, minor=%d\n",
            MAJOR(dev), MINOR(dev));

    return 0;
}

static void __exit chrdev_demo_exit(void)
{
    unregister_chrdev_region(dev, 2);

    pr_info("chrdev_demo: device numbers unregistered\n");
}

module_init(chrdev_demo_init);
module_exit(chrdev_demo_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Character device number allocation demo");
