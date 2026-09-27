#include <linux/module.h>   /* 所有模块都要 */
#include <linux/kernel.h>  /* printk */
#include <linux/init.h>    /* __init __exit */
#include <linux/fs.h>     /* file_operations、register_chrdev、MKDEV */
#include <linux/device.h>       /* class_create、device_create*/

#define DEV_NAME    "edge_ai"

static int      major;       /* 内核分配的主设备号*/
static struct class     *edge_ai_class;     /* 类（/sys/class/edge_ai） */
static struct device    *edge_ai_device;    /* 设备（/dev/edge_ai0）*/

static int edge_ai_open(struct inode *inode, struct file *file)
{
    printk(KERN_INFO"edge_ai open\n");
    return 0;
}

static int edge_ai_release(struct inode *inode, struct file *file)
{
    printk(KERN_INFO"edge_ai release\n");
    return 0;
}

static const struct file_operations edge_ai_fops = {
    .owner = THIS_MODULE,   /* 引入计数保护*/
    .open = edge_ai_open,
    .release = edge_ai_release,     /* release 就对应close()*/
};

/* -- 加载时执行-- */
static int __init edge_ai_evt_init(void)
{
    /* --1-- 申请设备号（传 0 = 内核随便分配一个空闲主号）--*/
    major = register_chrdev(0, DEV_NAME, &edge_ai_fops);
    if(major < 0){
        printk(KERN_ERR"edge_ai_evt: register_chrdev failed: %d\n", major);
        return major;           /* 返回错误码（负数）→insmod 会失败*/
    }
    printk(KERN_INFO"edge_ai_evt: registered, major=%d\n", major);

    /* --2-- 创建设备类（/sys/class/ 下建目录）--*/
    edge_ai_class = class_create(THIS_MODULE, DEV_NAME);
    if(IS_ERR(edge_ai_class)){
        unregister_chrdev(major, DEV_NAME);
        printk(KERN_ERR"edge_ai_evt: class_create failed\n");
        return PTR_ERR(edge_ai_class);   /* 返回错误码（负数）→insmod 会失败*/
    }

    /* --3-- 创建设备: 内核发 uevent → udev 自动建 /dev/edge_ai0 --*/
    edge_ai_device = device_create(edge_ai_class, NULL, MKDEV(major, 0), NULL, "edge_ai0");

    if (IS_ERR(edge_ai_device)){
        class_destroy(edge_ai_class);
        unregister_chrdev(major, DEV_NAME);
        printk(KERN_ERR"edge_ai_evt: device_create failed\n");
        return PTR_ERR(edge_ai_device);   /* 返回错误码（负数）→insmod 会失败*/
    }

    printk(KERN_INFO"edge_ai_evt: module loaded\n");
    return 0;
}

static void __exit edge_ai_evt_exit(void)
{
    device_destroy(edge_ai_class, MKDEV(major, 0));     /* 删设备*/
    class_destroy(edge_ai_class);       /* 删类*/
    unregister_chrdev(major, DEV_NAME);     /* 释放设备号*/
    printk(KERN_INFO"edge_ai_evt: module unloaded\n");
}

module_init(edge_ai_evt_init);
module_exit(edge_ai_evt_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("lihaihui");
MODULE_DESCRIPTION("Edge AI dual-node: event queue char device (stage 1.2)");
MODULE_VERSION("0.1");