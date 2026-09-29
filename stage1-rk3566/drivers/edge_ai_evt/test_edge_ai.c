#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "edge_ai_evt.h"

int main(void)
{
    
    int fd = open("/dev/edge_ai0", O_RDWR);
    if (fd < 0) {perror("open"); return 1; }
    
    int len = -1;

    if (ioctl(fd, EDGE_AI_IOC_LEN, &len) < 0) {
        perror("ioctl EDGE_AI_IOC_LEN");
        close(fd);
        return 1;
    }
    printf("Buffer length: %d\n", len);

    // 清空
    if (ioctl(fd, EDGE_AI_IOC_CLEAR) < 0) {
        perror("ioctl EDGE_AI_IOC_CLEAR");
        close(fd);
        return 1;
    }

    // 再查询长度，此时长度应为0
    len = -1;
    if (ioctl(fd, EDGE_AI_IOC_LEN, &len) < 0) {
        perror("ioctl EDGE_AI_IOC_LEN");
        close(fd);
        return 1;
    }
    printf("After CLEAR, buffer length: %d\n", len);

    // 写入字符串
    const char *msg = "Hello from user space!";
    struct edge_ai_put_arg put_arg;
    memset(&put_arg, 0, sizeof(put_arg));
    strncpy(put_arg.data, msg, BUF_SIZE - 1);
    put_arg.len = strlen(put_arg.data);

    if (ioctl(fd, EDGE_AI_IOC_PUT, &put_arg) < 0) {
        perror("ioctl EDGE_AI_IOC_PUT");
        close(fd);
        return 1;
    }
    printf("PUT succeeded, len = %d\n", put_arg.len);     

    // 再次查询长度，此时长度应为写入的字符串长度
    len = -1;
    if (ioctl(fd, EDGE_AI_IOC_LEN, &len) < 0) {
        perror("ioctl EDGE_AI_IOC_LEN");
        close(fd);
        return 1;
    }
    printf("After PUT, buffer length: %d\n", len);

    // 发布一个不存在的命令，检查error == ENOTTY
    if (ioctl(fd, 0x12345678) < 0) {
        if (errno == ENOTTY) {
            printf("ioctl returned ENOTTY as expected for invalid command\n");
        } else {
            perror("ioctl invalid command");
            close(fd);
            return 1;
        }
    } else {
        printf("ioctl did not return error for invalid command\n");
        close(fd);
        return 1;
    }
    // // 发送一个超长的字符串，检查error == EINVAL
    struct edge_ai_put_arg big;
    memset(&big, 0, sizeof(big));
    big.len = BUF_SIZE + 1;              /* 故意越界一个字节 */
    if (ioctl(fd, EDGE_AI_IOC_PUT, &big) < 0) {
        if (errno == EINVAL) printf("EINVAL as expected for len > BUF_SIZE\n");
        else { perror("unexpected error"); return 1; }
    }

    // 发送一个负数长度，检查 error == EINVAL
    struct edge_ai_put_arg neg;
    memset(&neg, 0, sizeof(neg));
    neg.len = -1;                        /* 负数，触发 len < 0 分支 */
    if (ioctl(fd, EDGE_AI_IOC_PUT, &neg) < 0) {
        if (errno == EINVAL)
            printf("EINVAL as expected for len < 0\n");
        else {
            perror("unexpected error for len < 0");
            close(fd);
            return 1;
        }
    } else {
        printf("ioctl did not return error for len < 0\n");
        close(fd);
        return 1;
    }

    close(fd);
    return 0;


}