#ifndef _EDGE_AI_EVT_H_
#define _EDGE_AI_EVT_H_

#ifdef __KERNEL__
#include <linux/ioctl.h>    /* 内核态用这个*/
#else
#include <sys/ioctl.h>     /* 用户态用这个*/ 
#endif

#define EDGE_AI_IOC_MAGIC 'E'
#define DEV_NAME    "edge_ai"
#define BUF_SIZE    256

struct edge_ai_put_arg{
    int len;        /* 数据长度 */
    char data[BUF_SIZE];
};

#define EDGE_AI_IOC_CLEAR       _IO(EDGE_AI_IOC_MAGIC, 0)    /* 清空缓冲区 */
#define EDGE_AI_IOC_PUT         _IOW(EDGE_AI_IOC_MAGIC, 1, struct edge_ai_put_arg)    /* 写入数据到缓冲区 */
#define EDGE_AI_IOC_LEN         _IOR(EDGE_AI_IOC_MAGIC, 2, int)    /* 获取缓冲区有效数据长度 */

#endif