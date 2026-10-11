/* serial_probe.c —— 串口探测工具
 *
 * 做三件事：
 *   1. 打开 /dev/ttyS3，打印【默认】的 termios 配置（看看有多少终端行为开着）
 *   2. 用 cfmakeraw 改成裸模式 + 115200 8N1，再打印一次（对比改了哪些）
 *   3. 尝试内部回环（TIOCM_LOOP），发 "HELLO" 看能不能自己收回自己
 *
 * 编译：gcc -Wall -o serial_probe serial_probe.c
 * 运行：./serial_probe          （不需要 sudo，你在 dialout 组里）
 */

 #define _DEFAULT_SOURCE    /* 让 <termios.h> 暴露 cfmakeraw() 等 POSIX 扩展 */
 
 #include <string.h>    /* strerror, strlen, memcmp */
 #include <stdio.h>     /* printf, fprintf */
 #include <errno.h>     /* errno —— 系统调用失败时看它 */
 #include <fcntl.h>     /* open, O_RDWR, O_NOCTTY */
 #include <unistd.h>    /* read, write, close, usleep */
 #include <termios.h>   /* struct termios, tcgetattr, cfmakeraw, ... */
 #include <sys/ioctl.h>  /* ioctl, TIOCMBIS, TIOCMBIC, TIOCM_LOOP */

 /* ⚠️ TIOCM_LOOP 不在 glibc 的 <sys/ioctl.h> 里，必须自己补上
 *
 * 原因：TIOCM_* 这一族宏分两类 ——
 *   - POSIX 标准的 11 个（TIOCM_LE ~ TIOCM_RI）在 glibc 的 <bits/ioctl-types.h>
 *   - 【内核扩展】的 TIOCM_LOOP / TIOCM_OUT1 / TIOCM_OUT2 在 <asm-generic/termios.h>
 * glibc 的 <sys/ioctl.h> 只带前者，所以编译会报 "TIOCM_LOOP undeclared"。
 *
 * 为什么不直接 #include <asm-generic/termios.h>？
 *   因为那个头会重复定义 struct termios / NCCS 等，和 glibc 的 <termios.h> 冲突。
 *
 * 所以直接补一个值：0x8000 是 asm-generic 里的固定值，所有 Linux 架构通用。
 * 加 #ifndef 保护：若将来某个 libc 定义了它，就用 libc 的，不重复定义。
 */
#ifndef TIOCM_LOOP
#define TIOCM_LOOP      0x8000
#endif

#define SERIAL_DEV      "/dev/ttyS3"
    
 /* ============================================================
 * 辅助函数：把 speed_t 转换成【真实波特率数值】
 *
 * ⚠️ 为什么需要这个函数？
 *   cfgetispeed()/cfgetospeed() 返回的 speed_t 不是波特率，
 *   而是内核内部的【编码值】（历史上为了兼容硬件寄存器而设的枚举）。
 *   在 aarch64/asm-generic 上：
 *       B9600   = 0000015（八进制） = 13    十进制
 *       B115200 = 0010002（八进制） = 4098  十进制
 *   直接把 speed_t 当数字打印会得到 13 / 4098 这种莫名其妙的值。
 *   所以必须查表转换。
 * ============================================================ */
static int speed_to_baud(speed_t s)
{
    switch (s) {
    case B0:       return 0;
    case B50:      return 50;
    case B75:      return 75;
    case B110:     return 110;
    case B134:     return 134;
    case B150:     return 150;
    case B200:     return 200;
    case B300:     return 300;
    case B600:     return 600;
    case B1200:    return 1200;
    case B1800:    return 1800;
    case B2400:    return 2400;
    case B4800:    return 4800;
    case B9600:    return 9600;
    case B19200:   return 19200;
    case B38400:   return 38400;
    case B57600:   return 57600;
    case B115200:  return 115200;
    case B230400:  return 230400;
    case B460800:  return 460800;
    case B500000:  return 500000;
    case B576000:  return 576000;
    case B921600:  return 921600;
    case B1000000: return 1000000;
    case B1152000: return 1152000;
    case B1500000: return 1500000;
    default:       return -1;      /* 不认识的编码 */
    }
}

/* ============================================================
 * 辅助函数：把 termios 里"关键的那几项"打印出来
 *
 * 为什么要挑着打印而不是全打？因为结构体字段太多，
 * 只打"我们关心的"才能一眼看出配置变没变对。
 * ============================================================ */
static void dump_termios(const char *tag, const struct termios *tio)
{
    printf("\n==================== %s =====================\n", tag);

    /* ---- c_iflag: 本地模式，全是“终端行为”，我们要全部关掉 ----*/
    printf("[c_lflag] 本地模式\n");
    printf("  ICANON (规范模式：数据被攒到 \\n 才交给应用) = %d\n", 
        !!(tio->c_lflag & ICANON));
    printf("    ECHO   (回显：收到的数据自动回显)          = %d\n",
           !!(tio->c_lflag & ECHO));
    printf("    ISIG   (控制字符触发信号，如 0x03 杀进程)  = %d\n",
           !!(tio->c_lflag & ISIG));

    /* ---- c_iflag: 输入方向的字符转换 ---------*/
    printf("[c_iflag] 输入方向的字符转换\n");
    printf("    ICRNL  (输入的 \\r 被转成 \\n)             = %d\n",
           !!(tio->c_iflag & ICRNL));
    printf("    IXON   (软件流控，会吃掉 0x11/0x13)        = %d\n",
           !!(tio->c_iflag & IXON));

    /* ---- c_oflag：输出方向的字符转换 ---- */
    printf("[c_oflag] 输出转换\n");
    printf("    OPOST  (输出处理总开关)                    = %d\n",
           !!(tio->c_oflag & OPOST));
    /* ⭐ ONLCR 是【子开关】，只有 OPOST（总开关）开着时才生效。
     *   所以打印时要 "与上" OPOST，否则会看到 "OPOST=0 但 ONLCR=1" 的假象 */
    printf("    ONLCR  (输出的 \\n 被扩成 \\r\\n)            = %d\n",
           !!(tio->c_oflag & ONLCR) && !!(tio->c_oflag & OPOST));

    /* ---- c_cflag：真正的硬件参数（对应你熟悉的 USART_InitTypeDef）---- */
    printf("[c_cflag] 硬件参数\n");
    printf("    CS8    (8 数据位)                          = %d\n",
           (tio->c_cflag & CSIZE) == CS8);
    printf("    CREAD  (使能接收)                          = %d\n",
           !!(tio->c_cflag & CREAD));
    printf("    CRTSCTS(硬件流控，没接线时开着会发不出去)   = %d\n",
           !!(tio->c_cflag & CRTSCTS));
    printf("    CLOCAL (忽略 modem 控制线)                 = %d\n",
           !!(tio->c_cflag & CLOCAL));

    /* ---- 波特率 ---- */
    printf("[速度] 输入 = %d baud, 输出 = %d baud\n",
           speed_to_baud(cfgetispeed(tio)),
           speed_to_baud(cfgetospeed(tio)));
}

int main(void)
{
    /* ============================================================
     * 第 1 步：打开设备
     *
     * O_RDWR    : 读写都要
     * O_NOCTTY  : ⚠️ 关键！不要把这个串口当成我的"控制终端"
     *             否则对端发个 Ctrl+C(0x03)，内核会直接杀我的进程
     * ============================================================ */
    int fd = open(SERIAL_DEV, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        fprintf(stderr, "打开 %s 失败: %s\n", SERIAL_DEV, strerror(errno));
        return 1;
    }
    printf("打开 %s 成功, fd = %d\n", SERIAL_DEV, fd);

    /* ============================================================
     * 第 2 步：读出【当前】配置并打印
     *
     * tcgetattr = "get attributes"，把当前的 termios 读出来
     * 必须先读出来，因为后面是在这个基础上改，不能凭空造一个
     * ============================================================ */
    struct termios tio;
    if (tcgetattr(fd, &tio) < 0){
        fprintf(stderr, "tcgetattr 失败: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    dump_termios("第 2 步：配置前（内核默认值）", &tio);

    /* ⭐ 把"进程序时的配置"存一份 —— 退出前要还回去（见文件末尾）
     *
     * 为什么要还：tcsetattr 改的是【内核里这个 tty 的状态】，不是进程状态。
     * 程序退出（close）【不会】自动恢复 —— 下次谁 open 它，
     * 读到的就是你这次设的 raw 模式。
     */
    struct termios orig_tio = tio;

    /* ============================================================
     * 第 3 步：改成"裸模式" + 115200 8N1
     * ============================================================ */

    /* (a) ⭐ 一行关掉所有终端行为（ICANON/ECHO/ISIG/ICRNL/OPOST...） */
    cfmakeraw(&tio);

    /* (b) 设波特率。B115200 是个宏，不用记它的数值 */
    cfsetispeed(&tio, B115200);      /* 输入方向 */
    cfsetospeed(&tio, B115200);      /* 输出方向 */

    /* (c) c_cflag 微调（对应 STM32 的 USART_InitTypeDef） */
    tio.c_cflag |=  (CLOCAL | CREAD);   /* 忽略 modem 控制线；使能接收 */
    tio.c_cflag &= ~CRTSCTS;            /* ⚠️ 关硬件流控：没接 RTS/CTS 线时，
                                           开着它会让 UART 以为"对方没准备好"，
                                           结果一个字节都发不出去 */
    tio.c_cflag &= ~CSTOPB;             /* 1 个停止位（8N1 的 1） */
    tio.c_cflag &= ~PARENB;             /* 无校验（8N1 的 N） */
    tio.c_cflag &= ~CSIZE;              /* 先清空"数据位"这个字段…… */
    tio.c_cflag |=  CS8;                /* ……再设成 8 位（8N1 的 8） */

    /* (d) 让配置生效
     * TCSANOW = "现在立刻"，不等输入输出队列排空 */
    if (tcsetattr(fd, TCSANOW, &tio) < 0) {
        fprintf(stderr, "tcsetattr 失败: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    /* (e) 清掉驱动里可能残留的数据（配之前的噪声） */
    tcflush(fd, TCIOFLUSH);

    /* ============================================================
     * 第 4 步：再读回来确认 —— "改了"不等于"改成功了"
     *
     * 这是个好习惯：配置类操作永远要读回验证。
     * （和你在 1.3 学的 V4L2 S_FMT 后要 G_FMT 读回是一个道理）
     * ============================================================ */
    struct termios tio2;
    if (tcgetattr(fd, &tio2) < 0) {
        fprintf(stderr, "tcgetattr 失败: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    dump_termios("第 4 步：配置后（cfmakeraw + 115200 8N1）", &tio2);

    /* ============================================================
     * 第 5 步：试内部回环
     *
     * 原理：8250 系列 UART（你这块是 16550A 兼容）的 MCR 寄存器有个 LOOP 位，
     *       置位后芯片内部把 TX 直接接回 RX —— 不需要任何外部导线。
     *
     * TIOCMBIS = "设置（或上）modem 控制位"，TIOCMBIC = "清除（或掉）"
     * TIOCM_LOOP 就是 LOOP 这一位的名字
     * ============================================================ */
    printf("\n================ 第 5 步：内部回环测试 ================\n");

    int loop_flag = TIOCM_LOOP;
    if (ioctl(fd, TIOCMBIS, &loop_flag) < 0) {
        fprintf(stderr, "开启内部回环失败: %s\n", strerror(errno));
        fprintf(stderr, "  -> 该驱动不支持 TIOCM_LOOP\n");
        fprintf(stderr, "  -> 后续需要外部导线自环，或用 socat 虚拟串口\n");
        close(fd);
        return 2;
    }
    printf("TIOCM_LOOP 设置成功，现在发什么应该就能收回什么\n");

    /* 发 5 个字节 */
    const char *msg = "HELLO";
    ssize_t nw = write(fd, msg, strlen(msg));
    if (nw < 0) {
        fprintf(stderr, "write 失败: %s\n", strerror(errno));
    } else {
        printf("已发出 %zd 字节: [%s]\n", nw, msg);
    }

    /* 等 200ms，让回环的数据在驱动里走完一圈 */
    usleep(200 * 1000);

    /* 读回来 */
    char buf[64];
    ssize_t nr = read(fd, buf, sizeof(buf) - 1);

    if (nr > 0) {
        buf[nr] = '\0';                     /* 当字符串打印，补个结束符 */
        printf("✅ 读回 %zd 字节: [%s]\n", nr, buf);

        if ((size_t)nr == strlen(msg) && memcmp(buf, msg, (size_t)nr) == 0) {
            printf("✅ 内容完全一致 —— 内部回环可用！1.4 能完整做完，不需要导线\n");
        } else {
            printf("⚠️ 收到了数据，但内容不一致（可能是残留噪声）\n");
        }
    } else if (nr == 0) {
        printf("⚠️ read 返回 0：没有数据可读\n");
        printf("  -> 驱动接受了设置但硬件不上报，需要外部导线自环\n");
    } else {
        fprintf(stderr, "read 失败: %s\n", strerror(errno));
    }

    /* ============================================================
     * 第 6 步：清理 —— 关掉回环，关闭 fd
     * ============================================================ */
    int noloop_flag = TIOCM_LOOP;
    ioctl(fd, TIOCMBIC, &noloop_flag);      /* 清掉 LOOP 位 */

    /* ⭐ 好习惯：把 termios 还原成程序启动时的样子
     * 否则下次谁打开这个串口，读到的就是你现在设的 raw 模式，
     * 用 screen/minicom 连它会觉得"行为很怪"（没回显、Ctrl+C 不工作）
     *
     * 注：这只覆盖了"正常退出"。如果程序被 kill -9，还是需要 stty sane 手动恢复。 */
    if (tcsetattr(fd, TCSANOW, &orig_tio) == 0)
        printf("[恢复] termios 已还原成程序启动时的配置\n");
    else
        fprintf(stderr, "[恢复] 失败: %s\n", strerror(errno));

    close(fd);
    printf("\n完成。\n");
    return 0;
}