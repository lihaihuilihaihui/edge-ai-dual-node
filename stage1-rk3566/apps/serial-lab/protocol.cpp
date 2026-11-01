/* protocol.cpp —— 双芯 UART 帧协议的编解码
 *
 * 目前只实现了【帧封装】（proto_encode）。
 * 帧解析 + 状态机在下一轮加入。
 */

#include "protocol.h"
#include "crc16.h"      /* crc16_table() —— SPEC 指定用查表版 */

#include <stdio.h>      /* printf，proto_dump / dump_stats 用 */
#include <string.h>     /* memcpy / memset */

/* ============================================================
 * proto_encode —— 把帧对象编码成字节流
 *
 * 拼帧顺序（严格按 SPEC §5.1）：
 *
 *   [0] SOF0   = 0xAA
 *   [1] SOF1   = 0x55
 *   [2] LEN                      ← CRC 覆盖从这里开始
 *   [3] CMD
 *   [4] SEQ
 *   [5...] PAYLOAD               ← CRC 覆盖到这里结束
 *   [p]   CRC_L                  ← 小端：低字节在前
 *   [p+1] CRC_H
 *
 * 注意 CRC 的覆盖范围是 "LEN + CMD + SEQ + PAYLOAD"，
 * 也就是从 out_buf[2] 开始、共 (LEN 字段自己 1 字节 + LEN 的值) 个字节。
 * ============================================================ */
ProtoResult proto_encode(const struct ProtoFrame *frame,
                         uint8_t                 *out_buf,
                         size_t                   out_cap,
                         size_t                  *out_len)
{
    /* ---------- ① 参数检查 ----------
     * 空指针检查是防御性编程的第一步。
     * 如果调用方传了 NULL，这里直接返回错误，
     * 而不是等到后面解引用时崩溃（那样报错位置和真正原因会隔很远）。
     */
    if (frame == NULL || out_buf == NULL || out_len == NULL) {
        return PROTO_ERR_NULL;
    }

    /* ---------- ② 载荷长度检查 ----------
     * payload_len 是 uint8_t（0~255），而协议上限是 64。
     * ⭐ 这是"所有来自外部的长度都必须校验"原则的具体体现 ——
     *    如果不查，后面循环会把 payload[64..] 的垃圾数据也写出去。
     */
    if (frame->payload_len > PROTO_MAX_PAYLOAD) {
        return PROTO_ERR_PAYLOAD_TOO_LONG;
    }

    /* ---------- ③ 计算 LEN 和整帧长度 ----------
     * LEN = CMD(1) + SEQ(1) + PAYLOAD(payload_len)
     * 整帧 = SOF(2) + LEN字段(1) + LEN的值 + CRC(2)
     *
     * 这里用 size_t 算 total 而不是 uint8_t：
     *   uint8_t 最大 255，虽然本协议不会溢出，但用 size_t 更不容易想错。
     */
    const uint8_t len   = (uint8_t)(2 + frame->payload_len);
    const size_t  total = (size_t)PROTO_HEADER_LEN + (size_t)len + (size_t)PROTO_CRC_LEN;

    /* ---------- ④ 输出缓冲区容量检查 ----------
     * ⭐ 这一句是防缓冲区溢出的关键。
     *    少了它，调用方传个小 buffer 就会踩坏内存。
     *    （和 1.2 学的 copy_to_user 长度校验是同一个道理）
     */
    if (out_cap < total) {
        return PROTO_ERR_BUF_TOO_SMALL;
    }

    /* ---------- ⑤ 拼帧头 ----------
     * 用 p 作为"当前写到哪个下标"的游标，每写一个字节就 ++。
     * 这样不用手算下标，改顺序时也不容易错。
     */
    size_t p = 0;
    out_buf[p++] = PROTO_SOF0;      /* [0] 0xAA */
    out_buf[p++] = PROTO_SOF1;      /* [1] 0x55 */
    out_buf[p++] = len;             /* [2] LEN —— p 现在 = 3 */

    /* ---------- ⑥ 拼主体：CMD + SEQ + PAYLOAD ---------- */
    out_buf[p++] = frame->cmd;      /* [3] CMD */
    out_buf[p++] = frame->seq;      /* [4] SEQ */

    /* 逐个字节拷贝载荷。
     * 用 uint8_t 做循环变量：上限 64 < 255，不会溢出。 */
    for (uint8_t i = 0; i < frame->payload_len; i++) {
        out_buf[p++] = frame->payload[i];
    }

    /* 此时 p == 3 + len，正好是 CRC 该放的位置 */

    /* ---------- ⑦ 算 CRC ----------
     * 覆盖范围：LEN + CMD + SEQ + PAYLOAD
     *   - 起始位置：&out_buf[2]（LEN 所在处）
     *   - 长度    ：1（LEN 字段自己）+ len（CMD+SEQ+PAYLOAD）= len + 1
     *
     * ⭐ 直接从已经拼好的输出缓冲区上算 CRC，而不是重新拼一遍 ——
     *    这样保证"校验的数据"和"实际发出去的数据"绝对是同一份。
     *    如果分开算，容易两边不一致（经典 bug）。
     */
    const uint16_t crc = crc16_table(&out_buf[2], (size_t)len + 1);

    /* ---------- ⑧ 按小端放入 CRC ----------
     * ⚠️ 这里就是 SPEC 歧义的那个决定点：
     *    SPEC 文字说"字节序：小端" → 低字节在前 → 先放 CRC_L
     *    （如果今后改成大端，只需交换这两行）
     */
    out_buf[p++] = (uint8_t)(crc & 0xFF);           /* CRC_L */
    out_buf[p++] = (uint8_t)((crc >> 8) & 0xFF);    /* CRC_H */

    /* ---------- ⑨ 返回实际长度 ---------- */
    *out_len = p;       /* p == total，也可以直接写 *out_len = total; */

    return PROTO_OK;
}

/* ============================================================
 * proto_dump —— 把字节流按十六进制打印出来（调试用）
 *
 * 输出形如：
 *   [最小帧] (7 字节): AA 55 02 01 00 3B 5C
 * ============================================================ */
void proto_dump(const char *tag, const uint8_t *data, size_t len)
{
    printf("%s (%zu 字节): ", tag, len);

    for (size_t i = 0; i < len; i++) {
        printf("%02X ", data[i]);

        /* 每 16 字节换一次行，避免长帧挤成一行看不清 */
        if ((i + 1) % 16 == 0 && (i + 1) < len) {
            printf("\n                            ");
        }
    }

    printf("\n");
}

/* ============================================================
 *                       帧解析（状态机）
 * ============================================================ */

/* ------------------------------------------------------------
 * parser_resync —— “失败后重新同步”的公共逻辑
 *
 * ⚠️ 这是状态机里最容易写错的地方，所以抽成一个函数。
 *
 * 背景：当校验失败 / LEN 非法时，不能简单地把状态机清零就完事 ——
 *       因为【当前这个字节本身】可能就是新一帧的 SOF0（0xAA）！
 *       如果直接扔掉它，新帧就会“少了头”，后面全部错位。
 *
 * 所以：
 *   - 如果这个字节是 0xAA → 把它当新帧的 SOF0，直接进入 PST_SOF1
 *   - 否则               → 计为垃圾字节，回到 PST_SOF0 干等
 *
 * ⭐ 这个函数是 static 的 —— 只在 protocol.cpp 内部可见，
 *    不导出给外部（避免污染命名空间，也防止被误用）。
 * ------------------------------------------------------------ */
static void parser_resync(struct ProtoParser *p, uint8_t byte)
{
    p->idx = 0;

    if (byte == PROTO_SOF0) {
        p->buf[0] = byte;
        p->idx     = 1;
        p->state   = PST_SOF1;
    } else {
        p->n_dropped++;
        p->state = PST_SOF0;
    }
}

/* ------------------------------------------------------------
 * proto_parser_init —— 清零
 * ------------------------------------------------------------ */
void proto_parser_init(struct ProtoParser *p)
{
    if (p == NULL) {
        return;
    }

    /* ⭐ memset 一次把 state / idx / len / 所有统计清零。
     *    好处：以后往结构体里加字段时，不用记得来这里补一句初始化。
     *
     * ⚠️ 注意 memset 全 0 对【指针】和【浮点数】并不严格可移植
     *    （C 标准不保证“全 0 字节”等于 NULL 指针或 0.0）。
     *    本结构体全是整数，所以安全。
     */
    memset(p, 0, sizeof(*p));

    p->state = PST_SOF0;    /* 显式写出，防止将来有人把 PST_SOF0 改掉 */
}

/* ------------------------------------------------------------
 * proto_parser_feed —— 核心：喂一个字节
 *
 * ⭐ 设计要点：
 *   1. 用 switch(p->state) 分派，每个状态只干一件事。
 *   2. 每个 case 都用 {} 包起来 ——
 *      因为 switch 的各个 case 【不构成独立作用域】，
 *      同名局部变量会导致 “redeclaration” 编译错误（K-036）。
 *   3. 任何异常（CRC 错 / LEN 非法）都回到 PST_SOF0，
 *      并尝试把“当前字节”当作新帧的 SOF0 重新同步。
 * ------------------------------------------------------------ */
enum ProtoFeedResult proto_parser_feed(struct ProtoParser *p,
                                       uint8_t byte,
                                       struct ProtoFrame *out)
{
    /* ---------- ① 防御检查 ---------- */
    if (p == NULL || out == NULL) {
        return PROTO_FEED_NONE;
    }

    /* ---------- ② 缓冲区越界保护 ----------
     * 理论上不可能触发（每个状态切换都在 total 之前结束），
     * 但如果状态机将来被改错、或者内存被踩，
     * 这一句能防止 buf[] 被写越界。
     * ⭐ 计数器 n_overrun 就是给这种情况留的“黑匣子”。
     */
    if (p->idx >= PROTO_MAX_FRAME) {
        p->n_overrun++;
        p->state = PST_SOF0;
        p->idx   = 0;
        /* 故意不 return：让当前字节在新一轮里被正常处理 */
    }

    switch (p->state) {

    /* ---------------- 等 SOF0 ---------------- */
    case PST_SOF0: {
        if (byte == PROTO_SOF0) {
            p->buf[0] = byte;
            p->idx    = 1;
            p->state  = PST_SOF1;
        } else {
            p->n_dropped++;     /* 垃圾字节，记一笔，继续等 */
        }
        break;
    }

    /* ---------------- 等 SOF1 ---------------- */
    case PST_SOF1: {
        if (byte == PROTO_SOF1) {
            p->buf[1] = byte;
            p->idx    = 2;
            p->state  = PST_LEN;
        } else if (byte == PROTO_SOF0) {
            /* ⭐ 处理 “AA AA 55” 这种“帧头前多了个 AA”的情况：
             *    后面这个 0xAA 才是真正的 SOF0。
             *    所以【留在 PST_SOF1】，只把 buf[0] 更新一下。
             *    （注意 idx 仍然是 1，不能累加） */
            p->buf[0] = byte;
            p->idx    = 1;
        } else {
            parser_resync(p, byte);
        }
        break;
    }

    /* ---------------- 等 LEN ---------------- */
    case PST_LEN: {
        /* LEN 的合法范围：2（CMD+SEQ）~ 66（+64 字节载荷） */
        if (byte < PROTO_MIN_LEN || byte > PROTO_MAX_LEN) {
            p->n_len_err++;
            /* ⚠️ 关键：这个字节本身可能就是新帧的 SOF0，
             *    所以交给 parser_resync 而不是直接扔掉。 */
            parser_resync(p, byte);
            return PROTO_FEED_LEN_ERR;      /* 注意是 return，不是 break */
        }

        p->len    = byte;
        p->buf[2] = byte;
        p->idx    = 3;
        p->state  = PST_CMD;
        break;
    }

    /* ---------------- 等 CMD ---------------- */
    case PST_CMD: {
        p->buf[p->idx++] = byte;    /* idx: 3 → 4 */
        p->state         = PST_SEQ;
        break;
    }

    /* ---------------- 等 SEQ ---------------- */
    case PST_SEQ: {
        p->buf[p->idx++] = byte;    /* idx: 4 → 5 */

        /* ⭐ 这里要决定下一步去哪 —— 载荷长度可能是 0！
         *    LEN == 2 表示只有 CMD+SEQ，没有载荷，
         *    此时必须【直接跳到 CRC】，不能进 PST_PAYLOAD
         *    （否则会在那里干等永远等不到的字节）。 */
        if (p->len > PROTO_MIN_LEN) {
            p->state = PST_PAYLOAD;
        } else {
            p->state = PST_CRC_L;
        }
        break;
    }

    /* ---------------- 收 PAYLOAD ---------------- */
    case PST_PAYLOAD: {
        p->buf[p->idx++] = byte;

        /* 已收字节数达到“5（帧头+CMD+SEQ）+ payload_len”了吗？
         *   payload_len = LEN - 2，所以目标 idx = 5 + LEN - 2 = LEN + 3 */
        if (p->idx >= (size_t)(p->len + 3)) {
            p->state = PST_CRC_L;
        }
        break;
    }

    /* ---------------- 等 CRC 低字节 ---------------- */
    case PST_CRC_L: {
        p->buf[p->idx++] = byte;
        p->state         = PST_CRC_H;
        break;
    }

    /* ---------------- 等 CRC 高字节 → 校验 ---------------- */
    case PST_CRC_H: {
        p->buf[p->idx++] = byte;

        const size_t   total    = (size_t)p->len + 5;                          /* 整帧长度 */
        const uint16_t crc_calc = crc16_table(&p->buf[2], (size_t)p->len + 1);  /* 重算 */
        const uint16_t crc_in   = (uint16_t)p->buf[total - 2]                  /* 帧内读回 */
                                | ((uint16_t)p->buf[total - 1] << 8);          /* 小端拼装 */

        enum ProtoFeedResult ret;

        if (crc_calc == crc_in) {
            /* ---- 校验通过：把字节流还原成逻辑帧 ---- */
            out->cmd = p->buf[3];                        /* CMD  在 buf[3] */
            out->seq = p->buf[4];                        /* SEQ  在 buf[4] */
            out->payload_len = (uint8_t)(p->len - 2);    /* 载荷长度 = LEN - 2 */

            if (out->payload_len > 0) {
                memcpy(out->payload, &p->buf[5], out->payload_len);
            } else {
                /* ⭐ 无载荷时也把 payload 清掉：
                 *    否则 out 里残留的是【上一次调用】的数据，
                 *    调用方万一忘了看 payload_len 就会被误导。 */
                memset(out->payload, 0, sizeof(out->payload));
            }

            p->n_ok++;
            ret = PROTO_FEED_FRAME;
        } else {
            p->n_crc_err++;
            ret = PROTO_FEED_CRC_ERR;
        }

        /* ---- 无论成败，都回到起点，准备下一帧 ----
         * ⭐ 这里【不做】回滚重扫，理由：
         *    1) CRC 错说明链路有问题，硬解析不如先修链路
         *    2) 重扫有 1/65536 的机会“接受噪声伪造的帧”
         *    3) 上层有 seq 跳号，丢帧能被发现
         *    详见 docs/1.4-serial-notes.md
         */
        p->state = PST_SOF0;
        p->idx   = 0;
        return ret;
    }

    /* ---------------- 兜底（正常不可达） ---------------- */
    default: {
        p->state = PST_SOF0;
        p->idx   = 0;
        break;
    }

    }   /* end switch */

    return PROTO_FEED_NONE;
}

/* ------------------------------------------------------------
 * proto_parser_feed_buf —— 批量喂
 * ------------------------------------------------------------ */
size_t proto_parser_feed_buf(struct ProtoParser *p,
                             const uint8_t *data, size_t n,
                             struct ProtoFrame *out, size_t out_max)
{
    if (p == NULL || data == NULL || out == NULL || out_max == 0) {
        return 0;
    }

    size_t got = 0;

    for (size_t i = 0; i < n && got < out_max; i++) {
        /* ⚠️ 注意 &out[got]：用【当前】got 作下标。
         *    必须在这一轮 got 增加【之前】算出来。 */
        if (proto_parser_feed(p, data[i], &out[got]) == PROTO_FEED_FRAME) {
            got++;
        }
    }

    return got;
}

/* ------------------------------------------------------------
 * proto_parser_dump_stats —— 打印统计
 * ------------------------------------------------------------ */
void proto_parser_dump_stats(const char *tag, const struct ProtoParser *p)
{
    if (p == NULL) {
        return;
    }

    printf("%s: 成功 %u | CRC错 %u | LEN错 %u | 丢弃字节 %u | 溢出 %u | 当前状态 %d\n",
           tag,
           (unsigned)p->n_ok, (unsigned)p->n_crc_err, (unsigned)p->n_len_err,
           (unsigned)p->n_dropped, (unsigned)p->n_overrun,
           (int)p->state);
}
