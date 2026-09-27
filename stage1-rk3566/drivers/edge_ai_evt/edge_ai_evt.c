#include <linux/module.h>   /* 所有模块都要 */
#include <linux/kernel.h>  /* printk */
#include <linux/init.h>    /* __init __exit */

/* -- 加载时执行-- */
static int __init edge_ai_evt_init(void)
{
    printk(KERN_INFO"edge_ai_evt init\n");
    return 0;
}

static void __exit edge_ai_evt_exit(void)
{
    printk("edge_ai_evt exit\n");
}

module_init(edge_ai_evt_init);
module_exit(edge_ai_evt_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("lihaihui");
MODULE_DESCRIPTION("Edge AI dual-node: event queue char device (stage 1.2)");
MODULE_VERSION("0.1");