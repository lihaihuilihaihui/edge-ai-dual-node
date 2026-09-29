#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>   /* 所有模块都要 */
#include <linux/kernel.h>  /* printk */
#include <linux/init.h>    /* __init __exit */
#include <linux/fs.h>     /* file_operations、register_chrdev、MKDEV */
#include <linux/device.h>       /* class_create、device_create*/
#include <linux/uaccess.h>  /* copy_to_user、copy_from_user */
#include <linux/slab.h>       /* kmalloc、kfree */
#include <linux/mutex.h>      /* 互斥锁 */
#include "edge_ai_evt.h"    /* 自定义头文件，包含 ioctl 命令定义 */



static int      major;       /* 内核分配的主设备号*/
static struct class     *edge_ai_class;     /* 类（/sys/class/edge_ai） */
static struct device    *edge_ai_device;    /* 设备（/dev/edge_ai0）*/

static char *kbuf;      /* 内核缓冲区（init 里 kmalloc）*/
static size_t klen;     /* 缓冲区里有效数据长度 */
static DEFINE_MUTEX(kbuf_lock);   /* 互斥锁，保护缓冲区 */

static ssize_t edge_ai_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    ssize_t ret = 0;
    size_t pos = *ppos;

    mutex_lock(&kbuf_lock);

    if (pos >= klen) {ret = 0; goto out;}

    if (count > klen - pos) count = klen - pos;

    if (copy_to_user(buf, kbuf + pos, count)) {
        ret = -EFAULT;
        goto out;
    }

    *ppos = pos + count;
    ret = count;
out:
    mutex_unlock(&kbuf_lock);
    return ret;
}

static ssize_t edge_ai_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    ssize_t ret = 0;
    size_t pos = *ppos;

    mutex_lock(&kbuf_lock);

    if (pos >= BUF_SIZE) {ret = -ENOSPC; goto out;}

    if (count > BUF_SIZE - pos) count = BUF_SIZE - pos;

    if (copy_from_user(kbuf + pos, buf, count)) {
        ret = -EFAULT;
        goto out;
    }

    *ppos = pos + count;
    klen = umax(klen, *ppos);
    ret = count;
out:
    mutex_unlock(&kbuf_lock);
    return ret;
}

static long edge_ai_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    long ret = 0;

    /* 建议先校验魔术字，不是自己的命令直接 ENOTTY */
    if (_IOC_TYPE(cmd) != EDGE_AI_IOC_MAGIC)
        return -ENOTTY;

    switch (cmd) {
        case EDGE_AI_IOC_CLEAR:
            mutex_lock(&kbuf_lock);
            klen = 0;
            mutex_unlock(&kbuf_lock);
            pr_info("Buffer cleared\n");
            break;

        case EDGE_AI_IOC_PUT: {
            struct edge_ai_put_arg put_arg;
            if (copy_from_user(&put_arg, (void __user *)arg, sizeof(put_arg))) {
                ret = -EFAULT;
                break;
            }
            if (put_arg.len < 0 || put_arg.len > BUF_SIZE) {
                ret = -EINVAL;       /* 参数不合法 */
                break;
            }
            mutex_lock(&kbuf_lock);
            memcpy(kbuf, put_arg.data, put_arg.len);
            klen = put_arg.len;
            mutex_unlock(&kbuf_lock);
            break;
        }

        case EDGE_AI_IOC_LEN:{
            int len;
            mutex_lock(&kbuf_lock);
            len = klen;
            mutex_unlock(&kbuf_lock);
            if (copy_to_user((int __user *)arg, &len, sizeof(len)))
                ret = -EFAULT;
            break;
        }
        default:
            ret = -ENOTTY;
            pr_err("Invalid ioctl command: %u\n", cmd);
            break;
    }

    return ret;
}

static int edge_ai_open(struct inode *inode, struct file *file)
{
    pr_info("edge_ai open\n");
    return 0;
}

static int edge_ai_release(struct inode *inode, struct file *file)
{
    pr_info("edge_ai release\n");
    return 0;
}

static const struct file_operations edge_ai_fops = {
    .owner = THIS_MODULE,   /* 引入计数保护*/
    .open = edge_ai_open,
    .release = edge_ai_release,     /* release 就对应close()*/
    .read = edge_ai_read,     /* read 就对应 read()*/
    .write = edge_ai_write,   /* write 就对应 write()*/
    .unlocked_ioctl = edge_ai_ioctl,   /* ioctl 就对应 ioctl()*/
};

/* -- 加载时执行-- */
static int __init edge_ai_evt_init(void)
{
    /* --1-- 申请设备号（传 0 = 内核随便分配一个空闲主号）--*/
    major = register_chrdev(0, DEV_NAME, &edge_ai_fops);
    if(major < 0){
        pr_err("register_chrdev failed: %d\n", major);
        return major;           /* 返回错误码（负数）→insmod 会失败*/
    }
    pr_info("registered, major=%d\n", major);

    /* --2-- 创建设备类（/sys/class/ 下建目录）--*/
    edge_ai_class = class_create(THIS_MODULE, DEV_NAME);
    if(IS_ERR(edge_ai_class)){
        unregister_chrdev(major, DEV_NAME);
        pr_err("class_create failed\n");
        return PTR_ERR(edge_ai_class);   /* 返回错误码（负数）→insmod 会失败*/
    }

    /* --3-- 创建设备: 内核发 uevent → udev 自动建 /dev/edge_ai0 --*/
    edge_ai_device = device_create(edge_ai_class, NULL, MKDEV(major, 0), NULL, "edge_ai0");

    if (IS_ERR(edge_ai_device)){
        class_destroy(edge_ai_class);
        unregister_chrdev(major, DEV_NAME);
        pr_err("device_create failed\n");
        return PTR_ERR(edge_ai_device);   /* 返回错误码（负数）→insmod 会失败*/
    }

    kbuf = kmalloc(BUF_SIZE, GFP_KERNEL);
    if (!kbuf) {
        device_destroy(edge_ai_class, MKDEV(major, 0));
        class_destroy(edge_ai_class);
        unregister_chrdev(major, DEV_NAME);
        pr_err("kmalloc failed\n");
        return -ENOMEM;
    }
    /* 预填测试数据 */
    snprintf(kbuf, BUF_SIZE, "Hello from edge_ai_evt module!\n");
    klen = strlen(kbuf);

    pr_info("module loaded\n");
    return 0;
}

static void __exit edge_ai_evt_exit(void)
{
    device_destroy(edge_ai_class, MKDEV(major, 0));     /* 删设备*/
    class_destroy(edge_ai_class);       /* 删类*/
    unregister_chrdev(major, DEV_NAME);     /* 释放设备号*/
    kfree(kbuf);
    kbuf = NULL;
    pr_info("module unloaded\n");
}

module_init(edge_ai_evt_init);
module_exit(edge_ai_evt_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("lihaihui");
MODULE_DESCRIPTION("Edge AI dual-node: event queue char device (stage 1.2)");
MODULE_VERSION("0.3");