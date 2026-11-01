/* serial_loopback_test.c —— 1.4 第 3 次：集成 + 自环压测
 *
 * ============================================================
 * 这个程序把前面三次的零件串成一条【真链路】：
 *
 *   termios 配置（第 1 次）
 *        ↓
 *   proto_encode()          （第 2 次）
 *        ↓
 *   write() → UART TX → [芯片内部回环] → UART RX → read()
 *                                                  ↓
 *                                    proto_parser_feed()（第 2 次）
 *
 * 之前所有测试都是"进程内闭环"（内存里 encode → 内存里 feed），
 * 这个程序是第一次真正跨过【硬件】。
 * ============================================================
 *
 * 硬件需求：只有板卡 + 电源，不需要任何外部配件。
 *   原因：8250 兼容 UART 的 MCR 寄存器有 LOOP 位，
 *         置位后芯片内部把 TX 直接接回 RX（TIOCM_LOOP）。
 *
 * 编译（注意用 g++，因为 protocol.cpp 是 C++）：
 *   g++ -Wall -Wextra -Werror -o serial_loopback_test \
 *       serial_loopback_test.c protocol.cpp crc16.cpp
 *
 * 运行：
 *   ./serial_loopback_test
 *   ./serial_loopback_test -n 5000        # 自定义帧数
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <time.h>

#include "protocol.h"

/* TIOCM_LOOP 不在 glibc 头文件里（内核扩展宏）→ K-050 */
#ifndef TIOCM_LOOP
#define TIOCM_LOOP      0x8000
#endif

#define SERIAL_DEV      "/dev/ttyS3"
#define DEF_NFRAMES     1000
#define READ_TIMEOUT_MS 1000     /* 等一帧最多 1 秒 */
#define QMAX            32       /* 帧队列容量（一次 read 最多解析出的帧数） */

/* ============================================================
 * 全局退出标志
 *
 * ⚠️ 为什么要它：如果用户按 Ctrl+C，默认行为是【直接杀进程】，
 *    那样 tcsetattr 恢复、关回环都不会执行 →
 *    下次谁 open /dev/ttyS3 读到的就是 raw 模式（K-052）。
 *
 *    volatile  : 告诉编译器"这个变量可能被信号处理器改，别优化到寄存器里"
 *    sig_atomic_t: 标准保证"信号处理器里读写它是原子的"
 * ============================================================ */
static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig)
{
    (void)sig;          /* 参数不用，但 -Wextra 要求显式说明 */
    g_stop = 1;
}

/* ============================================================
 * now_us —— 单调时钟（微秒）
 *
 * ⭐ 为什么用 CLOCK_MONOTONIC 而不是 CLOCK_REALTIME：
 *    REALTIME 会被 NTP 校时、手动改时间影响 → 算出来的耗时可能是负数。
 *    MONOTONIC 只往前走，专门用来测时间间隔。
 * ============================================================ */
static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)(ts.tv_nsec / 1000);
}

/* ============================================================
 * serial_config —— 打开串口并配成 raw 115200 8N1
 *
 * 成功返回 fd；失败返回 -1。
 * *orig 会被填成"配置前的 termios"，退出时用它恢复。
 * ============================================================ */
static int serial_config(const char *dev, struct termios *orig)
{
    /* O_NOCTTY：不要把这个串口当作我的控制终端
     *           （否则对端发 0x03 会让内核给我发 SIGINT） */
    int fd = open(dev, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        fprintf(stderr, "打开 %s 失败: %s\n", dev, strerror(errno));
        return -1;
    }

    struct termios tio;
    if (tcgetattr(fd, &tio) < 0) {
        fprintf(stderr, "tcgetattr 失败: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    /* 存一份原始配置，退出前要还回去（K-052） */
    *orig = tio;

    /* ⭐ 一行关掉 7 个终端陷阱（ICANON/ECHO/ISIG/ICRNL/IXON/OPOST/ONLCR） */
    cfmakeraw(&tio);

    cfsetispeed(&tio, B115200);
    cfsetospeed(&tio, B115200);

    tio.c_cflag |=  (CLOCAL | CREAD);   /* 忽略 modem 线；使能接收 */
    tio.c_cflag &= ~CRTSCTS;            /* 关硬件流控（没接 RTS/CTS） */
    tio.c_cflag &= ~CSTOPB;             /* 1 停止位 */
    tio.c_cflag &= ~PARENB;             /* 无校验 */
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |=  CS8;                /* 8 数据位 */

    if (tcsetattr(fd, TCSANOW, &tio) < 0) {
        fprintf(stderr, "tcsetattr 失败: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    /* 清掉配置过程中可能残留的噪声 */
    tcflush(fd, TCIOFLUSH);

    /* ⭐ 配置类操作永远读回验证一次（和 V4L2 的 S_FMT → G_FMT 一个道理） */
    struct termios check;
    if (tcgetattr(fd, &check) < 0) {
        fprintf(stderr, "tcgetattr(验证) 失败: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    if (!(check.c_cflag & CS8) || (check.c_cflag & PARENB) || (check.c_cflag & CSTOPB)) {
        fprintf(stderr, "❌ 读回验证失败：不是 8N1\n");
        close(fd);
        return -1;
    }

    return fd;
}

/* ============================================================
 * loopback —— 开 / 关 内部回环
 *
 * TIOCMBIS = "set bit(s)"     → 置位
 * TIOCMBIC = "clear bit(s)"   → 清零
 * ============================================================ */
static int loopback(int fd, int on)
{
    int flag = TIOCM_LOOP;
    int rc = ioctl(fd, on ? TIOCMBIS : TIOCMBIC, &flag);
    if (rc < 0) {
        fprintf(stderr, "内部回环 %s 失败: %s\n",
                on ? "开启" : "关闭", strerror(errno));
        if (on) {
            fprintf(stderr, "  -> 该 UART 驱动不支持 TIOCM_LOOP\n");
        }
        return -1;
    }
    return 0;
}

/* ============================================================
 * 简易帧队列
 *
 * ⚠️ 为什么需要它：
 *    read(fd, buf, 256) 一次可能读到【多帧】（最小帧只有 7 字节，
 *    256 字节里最多能塞 36 帧）。
 *    如果解析出第一帧就返回，后面的帧就被自己扔掉了 ——
 *    看起来像"丢帧"，其实是 bug。
 * ============================================================ */
struct FrameQ {
    struct ProtoFrame f[QMAX];
    int n;
};

static void q_clear(struct FrameQ *q)
{
    q->n = 0;
}

/* 从队首取一帧；没有返回 0 */
static int q_pop(struct FrameQ *q, struct ProtoFrame *out)
{
    if (q->n <= 0) {
        return 0;
    }
    *out = q->f[0];
    /* 后面的往前挪一格（数量很小，memmove 足够） */
    memmove(&q->f[0], &q->f[1], (size_t)(q->n - 1) * sizeof(q->f[0]));
    q->n--;
    return 1;
}

/* ============================================================
 * pump —— poll + read + feed，把解析出的帧塞进队列
 *
 * 返：1 = 队列里至少有一帧；0 = 超时；-1 = 出错
 *
 * ⭐ deadline 是【绝对时间】而不是"每次重置的相对超时"，
 *    否则循环里反复 poll 会让总等待时间无限长。
 * ============================================================ */
static int pump(int fd, struct ProtoParser *ps, struct FrameQ *q, int timeout_ms)
{
    uint64_t deadline = now_us() + (uint64_t)timeout_ms * 1000u;

    while (q->n == 0) {
        if (g_stop) {
            return 0;
        }

        uint64_t now = now_us();
        if (now >= deadline) {
            return 0;                       /* 超时 */
        }

        int remain_ms = (int)((deadline - now) / 1000u);
        if (remain_ms <= 0) {
            remain_ms = 1;
        }

        struct pollfd pfd;
        pfd.fd      = fd;
        pfd.events  = POLLIN;
        pfd.revents = 0;

        int r = poll(&pfd, 1, remain_ms);
        if (r == 0) {
            return 0;                       /* 超时 */
        }
        if (r < 0) {
            if (errno == EINTR) {
                continue;                   /* 被信号打断，重试 */
            }
            fprintf(stderr, "poll 失败: %s\n", strerror(errno));
            return -1;
        }

        uint8_t buf[256];
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "read 失败: %s\n", strerror(errno));
            return -1;
        }
        if (n == 0) {
            continue;
        }

        /* ⭐ 逐字节喂状态机 —— SPEC §5.3 的硬性要求 */
        for (ssize_t i = 0; i < n; i++) {
            struct ProtoFrame tmp;
            if (proto_parser_feed(ps, buf[i], &tmp) == PROTO_FEED_FRAME) {
                if (q->n < QMAX) {
                    q->f[q->n++] = tmp;
                }
                /* 队列满了就丢弃 —— 正常不会发生（写一帧等一帧） */
            }
        }
    }

    return 1;
}

/* ============================================================
 * 构造一个测试帧
 *
 * ⭐ 每帧的 payload_len 在 0..64 间轮转，cmd / seq 用 i 的低位，
 *    这样既覆盖了全部长度区间，又能从 seq 一眼看出丢了第几帧。
 * ============================================================ */
static void make_test_frame(struct ProtoFrame *f, int i)
{
    memset(f, 0, sizeof(*f));

    f->cmd = (uint8_t)(i & 0xFF);
    f->seq = (uint8_t)(i & 0xFF);

    uint8_t plen = (uint8_t)(i % (PROTO_MAX_PAYLOAD + 1));   /* 0..64 轮转 */
    f->payload_len = plen;
    for (uint8_t j = 0; j < plen; j++) {
        f->payload[j] = (uint8_t)(i + j);
    }
}

/* ============================================================
 * run_roundtrip —— 主测试：N 帧往返
 * ============================================================ */
static int run_roundtrip(int fd, int nframes)
{
    printf("\n================ 主测试：%d 帧往返 ================\n", nframes);

    struct ProtoParser ps;
    proto_parser_init(&ps);

    struct FrameQ q;
    q_clear(&q);

    int  n_ok       = 0;
    int  n_bad      = 0;    /* 内容不一致 */
    int  n_timeout  = 0;    /* 等不到帧 */
    size_t total_bytes = 0;

    uint64_t t0 = now_us();

    for (int i = 0; i < nframes && !g_stop; i++) {
        /* ---- 1. 构造并编码 ---- */
        struct ProtoFrame src;
        make_test_frame(&src, i);

        uint8_t raw[PROTO_MAX_FRAME];
        size_t  raw_len = 0;
        if (proto_encode(&src, raw, sizeof(raw), &raw_len) != PROTO_OK) {
            fprintf(stderr, "proto_encode 失败 @%d\n", i);
            return -1;
        }
        total_bytes += raw_len;

        /* ---- 2. 写出去 ---- */
        ssize_t w = write(fd, raw, raw_len);
        if (w != (ssize_t)raw_len) {
            fprintf(stderr, "write 失败 @%d: %s\n", i, strerror(errno));
            return -1;
        }

        /* ---- 3. 等它绕回来 ---- */
        int got = pump(fd, &ps, &q, READ_TIMEOUT_MS);
        if (got < 0) {
            return -1;
        }
        if (got == 0) {
            n_timeout++;
            if (n_timeout <= 3) {
                printf("  ⚠️ 第 %d 帧等待超时（seq=0x%02X）\n", i, src.seq);
            }
            continue;
        }

        /* ---- 4. 逐字段对比 ---- */
        struct ProtoFrame dst;
        q_pop(&q, &dst);

        int same = (dst.cmd == src.cmd)
                && (dst.seq == src.seq)
                && (dst.payload_len == src.payload_len)
                && (memcmp(dst.payload, src.payload, src.payload_len) == 0);

        if (same) {
            n_ok++;
        } else {
            n_bad++;
            if (n_bad <= 3) {
                printf("  ❌ 第 %d 帧内容不一致：\n", i);
                printf("     期望 cmd=0x%02X seq=0x%02X len=%u\n",
                       src.cmd, src.seq, src.payload_len);
                printf("     实际 cmd=0x%02X seq=0x%02X len=%u\n",
                       dst.cmd, dst.seq, dst.payload_len);
            }
        }
    }

    uint64_t t1 = now_us();
    double sec = (double)(t1 - t0) / 1e6;

    /* ============================================================
     * 统计报告
     * ============================================================ */
    printf("\n--------------- 结果 ---------------\n");
    printf("  发送帧数      : %d\n", nframes);
    printf("  正确还原      : %d\n", n_ok);
    printf("  内容不一致    : %d\n", n_bad);
    printf("  等待超时      : %d\n", n_timeout);

    printf("\n  状态机统计    : 成功 %u | CRC错 %u | LEN错 %u | 丢弃字节 %u | 溢出 %u\n",
           (unsigned)ps.n_ok, (unsigned)ps.n_crc_err,
           (unsigned)ps.n_len_err, (unsigned)ps.n_dropped,
           (unsigned)ps.n_overrun);

    printf("\n--------------- 性能 ---------------\n");
    printf("  总耗时        : %.3f 秒\n", sec);
    if (sec > 0) {
        double fps  = (double)n_ok / sec;
        double avg  = (double)total_bytes / (double)(nframes > 0 ? nframes : 1);
        double bps  = (double)total_bytes / sec;

        printf("  发送字节数    : %zu\n", total_bytes);
        printf("  平均帧长      : %.1f 字节\n", avg);
        printf("  实测吞吐      : %.1f 帧/秒, %.0f 字节/秒\n", fps, bps);

        /* 理论极限：115200 baud / 10 bit每字节 = 11520 字节/秒 */
        double theory_bps = 11520.0;
        double theory_fps = theory_bps / avg;
        printf("  理论极限      : %.1f 帧/秒, %.0f 字节/秒（115200 8N1）\n",
               theory_fps, theory_bps);
        printf("  线路利用率    : %.1f%%\n", bps / theory_bps * 100.0);
        printf("\n  ⭐ 线路利用率 < 100%% 是正常的：\n"
               "     每帧之间有 poll/read syscall 开销和往返延迟，\n"
               "     逐帧同步模型下这就是上限。想更高就得上流水线（poll 双向）。\n");
    }

    int pass = (n_ok == nframes) && (n_bad == 0) && (n_timeout == 0)
            && (ps.n_crc_err == 0) && (ps.n_len_err == 0) && (ps.n_dropped == 0);

    printf("\n  %s\n", pass ? "✅ 主测试通过：0 误码" : "❌ 主测试未通过");
    return pass ? 0 : 1;
}

/* ============================================================
 * run_injection —— 注入测试：垃圾字节 + 截断半帧
 *
 * 目的是验证 SPEC 的验收要求：
 *   "注入随机字节后通信能自恢复"
 *
 * ⭐ 精确推演会发生什么：
 *   1. 先写垃圾 00 11 22 → 状态机在 PST_SOF0，全部丢弃（n_dropped += 3）
 *   2. 再写半帧 AA 55 06 02 05 → 状态机进入 PST_PAYLOAD，
 *      期待 6-2 = 4 字节载荷
 *   3. 然后写完整帧 A = AA 55 02 01 00 CD 91：
 *        AA 55 02 01  → 被当成"载荷"，收满 4 字节 → PST_CRC_L
 *        00 CD        → 被当成 CRC → 校验【失败】→ n_crc_err++
 *        91           → 垃圾（n_dropped++）
 *      ⇒ 帧 A 被半帧"吃掉"，不产出
 *   4. 再写完整帧 B：状态机已复位，正常解析 ✅
 *
 *   所以期望：写 1 半帧 + N 完整帧 → 产出 N-1 帧，n_crc_err >= 1
 * ============================================================ */
static int run_injection(int fd)
{
    printf("\n================ 注入测试：垃圾 + 截断半帧 ================\n");

    struct ProtoParser ps;
    proto_parser_init(&ps);

    struct FrameQ q;
    q_clear(&q);

    /* ---- 1. 垃圾字节 ---- */
    const uint8_t junk[] = { 0x00, 0x11, 0x22, 0x55 };
    if (write(fd, junk, sizeof(junk)) != (ssize_t)sizeof(junk)) {
        fprintf(stderr, "写垃圾失败: %s\n", strerror(errno));
        return -1;
    }

    /* ---- 2. 半帧：LEN=6 但只发到 SEQ（缺 4 字节载荷 + 2 字节 CRC）---- */
    const uint8_t half[] = { PROTO_SOF0, PROTO_SOF1, 0x06, 0xAA, 0xBB };
    if (write(fd, half, sizeof(half)) != (ssize_t)sizeof(half)) {
        fprintf(stderr, "写半帧失败: %s\n", strerror(errno));
        return -1;
    }

    /* 确保上面两段真的发完了，再发正片 */
    tcdrain(fd);

    /* ---- 3. 正片：5 个完整帧 ---- */
    const int NFRAME = 5;
    for (int i = 0; i < NFRAME; i++) {
        struct ProtoFrame src;
        make_test_frame(&src, 100 + i);

        uint8_t raw[PROTO_MAX_FRAME];
        size_t  raw_len = 0;
        proto_encode(&src, raw, sizeof(raw), &raw_len);

        if (write(fd, raw, raw_len) != (ssize_t)raw_len) {
            fprintf(stderr, "写正片失败 @%d: %s\n", i, strerror(errno));
            return -1;
        }
    }

    /* ---- 4. 收：等够 NFRAME 帧或超时 ---- */
    int got = 0;
    for (int guard = 0; guard < 50 && got < NFRAME && !g_stop; guard++) {
        int r = pump(fd, &ps, &q, 200);
        if (r < 0) {
            return -1;
        }
        if (r == 0) {
            break;              /* 没更多数据了 */
        }
        struct ProtoFrame tmp;
        while (q_pop(&q, &tmp)) {
            got++;
            if (got <= NFRAME) {
                printf("  收到第 %d 帧: cmd=0x%02X seq=0x%02X len=%u\n",
                       got, tmp.cmd, tmp.seq, tmp.payload_len);
            }
        }
    }

    printf("\n  期望: 完整帧 %d 个 → 实际收到 %d 个（第 1 个被半帧吃掉）\n",
           NFRAME, got);
    printf("  状态机: 成功 %u | CRC错 %u | LEN错 %u | 丢弃字节 %u\n",
           (unsigned)ps.n_ok, (unsigned)ps.n_crc_err,
           (unsigned)ps.n_len_err, (unsigned)ps.n_dropped);

    /* ---- 5. 断言 ---- */
    int pass = 1;

    if (got != NFRAME - 1) {
        printf("  ❌ 期望收到 %d 帧，实际 %d 帧\n", NFRAME - 1, got);
        pass = 0;
    } else {
        printf("  ✅ 帧数符合预期（%d 帧，被吃掉 1 帧）\n", got);
    }

    if (ps.n_crc_err < 1) {
        printf("  ❌ 期望至少 1 次 CRC 错（半帧导致的错位），实际 %u\n",
               (unsigned)ps.n_crc_err);
        pass = 0;
    } else {
        printf("  ✅ 检测到 %u 次 CRC 错（半帧错位被捕获）\n",
               (unsigned)ps.n_crc_err);
    }

    if (ps.n_dropped < 4) {     /* 至少 4 个垃圾字节 + 可能还有被吃掉的 */
        printf("  ⚠️ 丢弃字节只有 %u 个，可能偏少（不一定是错误）\n",
               (unsigned)ps.n_dropped);
    }

    /* ---- 6. 关键：状态机必须已经回到 PST_SOF0，能继续干活 ---- */
    if (ps.state != PST_SOF0) {
        printf("  ❌ 状态机没回到 PST_SOF0（当前 %d）—— 后续通信会卡死\n",
               (int)ps.state);
        pass = 0;
    } else {
        printf("  ✅ 状态机已复位到 PST_SOF0，可继续通信\n");
    }

    printf("\n  %s\n", pass ? "✅ 注入测试通过：错位后能自恢复"
                            : "❌ 注入测试未通过");
    return pass ? 0 : 1;
}

/* ============================================================
 * main
 * ============================================================ */
int main(int argc, char **argv)
{
    int nframes = DEF_NFRAMES;

    /* 简单参数解析：-n <数量> */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            nframes = atoi(argv[++i]);
            if (nframes <= 0) {
                fprintf(stderr, "帧数必须 > 0\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("用法: %s [-n 帧数]\n", argv[0]);
            return 0;
        }
    }

    printf("=========================================================\n");
    printf(" 1.4 集成 + 自环压测\n");
    printf(" 设备: %s   115200 8N1   内部回环(TIOCM_LOOP)\n", SERIAL_DEV);
    printf("=========================================================\n");

    /* ---- 注册信号处理：Ctrl+C 时优雅退出 ---- */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    /* ⭐ 不加 SA_RESTART：让 poll/read 被信号打断时返回 EINTR，
     *    这样主循环能尽快看到 g_stop */
    sa.sa_flags = 0;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* ---- 1. 配置串口 ---- */
    struct termios orig_tio;
    int fd = serial_config(SERIAL_DEV, &orig_tio);
    if (fd < 0) {
        return 1;
    }
    printf("✅ 串口已配置：115200 8N1（已读回验证）\n");

    /* ---- 2. 开内部回环 ---- */
    if (loopback(fd, 1) < 0) {
        tcsetattr(fd, TCSANOW, &orig_tio);
        close(fd);
        return 2;
    }
    printf("✅ 内部回环已开启（TX 在芯片内部接回 RX）\n");

    /* ---- 3. 跑测试 ---- */
    int rc1 = run_roundtrip(fd, nframes);
    int rc2 = run_injection(fd);

    /* ---- 4. ⭐ 收尾：关回环 + 恢复 termios ----
     * 顺序不能反：先关回环（硬件状态），再恢复 tty 配置。
     *
     * ⚠️ 即使中途出错/被 Ctrl+C，也必须走到这里 ——
     *    否则 /dev/ttyS3 会一直停留在 raw 模式（K-052）。
     */
    loopback(fd, 0);
    printf("\n✅ 内部回环已关闭\n");

    if (tcsetattr(fd, TCSANOW, &orig_tio) < 0) {
        fprintf(stderr, "⚠️ 恢复 termios 失败: %s\n", strerror(errno));
    } else {
        printf("✅ termios 已恢复为进入程序前的配置\n");
    }

    close(fd);

    /* ---- 5. 总结 ---- */
    printf("\n=========================================================\n");
    if (rc1 == 0 && rc2 == 0) {
        printf(" 🎉 全部通过 —— 1.4 可以结课了\n");
    } else {
        printf(" ⚠️ 有测试未通过，详见上面输出\n");
    }
    printf("=========================================================\n");

    /* ⭐ 提示用户手动验证硬件计数器（需要 sudo） */
    printf("\n💡 想看硬件层的收发计数（含 overrun），另开终端执行：\n");
    printf("   sudo cat /proc/tty/driver/serial | grep '^3:'\n");
    printf("   关注 tx / rx 是否都增加了，以及【有没有 oe（overrun）】\n");

    return (rc1 == 0 && rc2 == 0) ? 0 : 1;
}
