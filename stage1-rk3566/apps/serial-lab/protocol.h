/* protocol.h —— 双芯 UART 帧协议（SPEC.md §5.1 / §5.2）
 *
 * 帧格式：
 *   +--------+--------+--------+--------+--------+-----...-----+---------+---------+
 *   |  SOF0  |  SOF1  |  LEN   |  CMD   |  SEQ   |   PAYLOAD   |  CRC_L  |  CRC_H  |
 *   |  0xAA  |  0x55  |  1B    |  1B    |  1B    |   0..N B    |   1B    |   1B    |
 *   +--------+--------+--------+--------+--------+-----...-----+---------+---------+
 *        帧头（2B）      └──────── LEN = CMD+SEQ+PAYLOAD 的字节数 ────────┘  └ 校验 2B ┘
 *   ⚠️ 注意 CRC 是【CRC_L 在前、CRC_H 在后】（小端）。
 *      SPEC §5.1 的示意图画的是反的（CRC_H | CRC_L），以本图为准，理由见下。
 *
 * 参数：
 *   物理层  : 115200 8N1
 *   字节序  : 小端（SPEC §5.1）
 *   CRC 范围: LEN + CMD + SEQ + PAYLOAD（不含 SOF、不含 CRC 自身）
 *   CRC 算法: CRC-16/CCITT-FALSE（crc16.h）
 *   载荷上限: 64 字节（LEN 范围 2..66，超出视为非法帧）
 *
 * ⚠️ 本实现明确了 SPEC 里的一处歧义：【CRC 按小端存放，即 CRC_L 在前、CRC_H 在后】
 *    理由：SPEC 文字明确写了"字节序：小端"；且小端与 CPU 一致，可直接按内存布局发送。
 *    如果今后改为大端，只需改 protocol.cpp 里 proto_encode() 的最后两行。
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

/* ============================================================
 * 协议常量
 * ============================================================ */

/* ---- 帧头 ---- */
#define PROTO_SOF0              0xAA
#define PROTO_SOF1              0x55

/* ---- 各部分长度（字节） ---- */
#define PROTO_SOF_LEN           2       /* SOF0 + SOF1 */
#define PROTO_HEADER_LEN        3       /* SOF0 + SOF1 + LEN */
#define PROTO_CRC_LEN           2       /* CRC_L + CRC_H */

/* ---- LEN 字段的取值范围（LEN = CMD + SEQ + PAYLOAD） ---- */
#define PROTO_MIN_LEN           2       /* CMD(1) + SEQ(1)，无载荷 */
#define PROTO_MAX_PAYLOAD       64
#define PROTO_MAX_LEN           (2 + PROTO_MAX_PAYLOAD)         /* 66 */

/* ---- 整帧最大长度 = SOF(2) + LEN字段(1) + LEN最大值(66) + CRC(2) ---- */
#define PROTO_MAX_FRAME         (PROTO_HEADER_LEN + PROTO_MAX_LEN + PROTO_CRC_LEN)  /* 71 */

/* ============================================================
 * 命令码（SPEC.md §5.2）
 *
 * ⭐ 设计细节：用最高位区分方向 ——
 *      0x0x  → ESP32 上报给 RK3566
 *      0x8x  → RK3566 下发给 ESP32
 *    调试时看一眼就知道这帧是哪个方向的。
 * ============================================================ */

/* --- ESP32 → RK3566 --- */
#define CMD_HEARTBEAT           0x01    /* payload: uint8 fw_ver, uint8 status */
#define CMD_PIR_EVENT           0x02    /* payload: uint32 timestamp_ms */
#define CMD_ENV_DATA            0x03    /* payload: int16 temp_x10, uint16 humi_x10 */
#define CMD_ACK                 0x04    /* payload: uint8 ack_seq, uint8 result */

/* --- RK3566 → ESP32 --- */
#define CMD_ALARM_SET           0x81    /* payload: uint8 on_off, uint8 pattern */
#define CMD_SERVO_SET           0x82    /* payload: uint8 angle (0-180) */
#define CMD_DISPLAY_SET         0x83    /* payload: uint8 line, char text[16] */
#define CMD_LED_SET             0x84    /* payload: uint8 led_id, uint8 mode */
#define CMD_QUERY_STATUS        0x85    /* 无 payload */

/* ============================================================
 * 帧对象：内存里表示一帧（跟字节序无关）
 *
 * 上层代码（决策模块等）只跟这个结构体打交道，
 * 不用关心 CRC 放哪个字节、字节序是什么。
 * ============================================================ */
struct ProtoFrame {
    uint8_t cmd;
    uint8_t seq;
    uint8_t payload[PROTO_MAX_PAYLOAD];
    uint8_t payload_len;        /* 0 ~ 64 */
};

/* ============================================================
 * 返回码
 *
 * ⭐ 为什么用枚举而不是 bool：
 *    bool 只能说"失败了"，无法区分原因。
 *    而"载荷超长"（程序 bug）和"缓冲不够"（调用方参数问题）
 *    需要完全不同的处理方式。
 * ============================================================ */
enum ProtoResult {
    PROTO_OK = 0,                  /* 成功 */
    PROTO_ERR_NULL,                /* 传入了空指针 */
    PROTO_ERR_PAYLOAD_TOO_LONG,    /* payload_len > 64 */
    PROTO_ERR_BUF_TOO_SMALL,       /* out_cap 装不下整帧 */
};

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * proto_encode —— 把帧对象编码成字节流
 *
 * 入参：
 *   frame    : 要发送的帧（cmd / seq / payload / payload_len）
 *   out_buf  : 输出缓冲区
 *   out_cap  : out_buf 的容量（⭐ 必须传，用于防止缓冲区溢出）
 * 出参：
 *   out_len  : 实际写入的字节数（成功时 = LEN + 5）
 *
 * 返回：ProtoResult
 * ============================================================ */

/* ⚠️ 这里必须写 `enum ProtoResult`，不能只写 `ProtoResult`：
 *      C 里 enum 的类型名是 `enum Xxx`，裸的 `Xxx` 不存在；
 *      只有 C++ 才允许省略 enum 关键字。
 *    本头文件被 .c 文件包含（serial_probe.c / serial_loopback_test.c /
 *    将来的 ESP32 代码），所以必须写成 C 兼容形式。 */
enum ProtoResult proto_encode(const struct ProtoFrame *frame,
                              uint8_t                 *out_buf,
                              size_t                   out_cap,
                              size_t                  *out_len);

/* ============================================================
 * proto_dump —— 把一段字节按 "AA 55 06 02 05 ..." 格式打印出来
 * 纯调试用，不参与协议逻辑。
 * ============================================================ */
void proto_dump(const char *tag, const uint8_t *data, size_t len);

/* ============================================================
 * 帧解析（1.4 iteration 3）
 *
 * ⭐ 核心认知：串口给的是【字节流】，不是【帧】。
 *    一次 read() 可能返回 0 字节 / 半帧 / 1.5 帧 / 3 帧。
 *    read() 没有任何“帧边界”概念。
 *
 *    所以模型必须是：
 *      每来一个字节 → 过一遍状态机 → 凑齐一帧就吐出来
 *
 *    ⭐ 因此必须有个结构体把“走到哪儿了”记住 ——
 *       因为一帧可能跨越多次 read()。
 * ============================================================ */

/* ---- 解析状态机的 8 个状态（SPEC §5.3） ---- */
enum ProtoParseState {
    PST_SOF0 = 0,   /* 等第 1 个帧头字节 0xAA */
    PST_SOF1,       /* 等第 2 个帧头字节 0x55 */
    PST_LEN,        /* 等 LEN */
    PST_CMD,        /* 等 CMD */
    PST_SEQ,        /* 等 SEQ */
    PST_PAYLOAD,    /* 正在收 PAYLOAD（长度 = LEN - 2） */
    PST_CRC_L,      /* 等 CRC 低字节 */
    PST_CRC_H,      /* 等 CRC 高字节（收到即校验） */
};

/* ---- 解析器实例 ----
 *
 * ⭐ 为什么必须有个“结构体”来保存状态：
 *    因为一帧可能跨越多次 read()。上一次调用的进度（状态 + 已收字节）
 *    必须被保存下来，下一次调用接着干。
 *    这就是“状态机”存在的意义。
 *
 * ⭐ 整个结构体大约 71 + 24 = 95 字节，全在栈上/静态区，
 *    没有 malloc —— 嵌入式场景下不会产生内存碎片。
 */
struct ProtoParser {
    /* --- 解析进度 --- */
    enum ProtoParseState state;     /* 当前在哪个状态 */
    uint8_t buf[PROTO_MAX_FRAME];   /* 累积缓冲区（含 SOF） */
    size_t  idx;                    /* 已累积的字节数 */
    uint8_t len;                    /* 已解析到的 LEN 值 */

    /* --- 统计（调试/验收用） --- */
    uint32_t n_ok;                  /* 成功解析的帧数 */
    uint32_t n_crc_err;             /* CRC 校验失败次数 */
    uint32_t n_len_err;             /* LEN 非法次数 */
    uint32_t n_dropped;             /* 丢弃的垃圾字节数 */
    uint32_t n_overrun;             /* 缓冲区保护触发次数（正常应为 0） */
};

/* ---- 喂一个字节的结果 ---- */
enum ProtoFeedResult {
    PROTO_FEED_NONE = 0,    /* 还没凑齐一帧，继续喂 */
    PROTO_FEED_FRAME,       /* ⭐ 凑齐并校验通过，out 已填充 */
    PROTO_FEED_CRC_ERR,     /* 凑齐一帧但 CRC 不匹配 */
    PROTO_FEED_LEN_ERR,     /* LEN 非法（< 2 或 > 66），已丢弃 */
};

/* ============================================================
 * proto_parser_init —— 初始化（清零）解析器
 * ============================================================ */
void proto_parser_init(struct ProtoParser *p);

/* ============================================================
 * proto_parser_feed —— 核心：喂【一个字节】
 *
 * 返：ProtoFeedResult
 *   命中 PROTO_FEED_FRAME 时，*out 被填充为完整的帧。
 * ============================================================ */
enum ProtoFeedResult proto_parser_feed(struct ProtoParser *p,
                                       uint8_t byte,
                                       struct ProtoFrame *out);

/* ============================================================
 * proto_parser_feed_buf —— 便利层：喂一批
 *
 * 把 data[0..n) 逐个喂进状态机，把解析出的帧依次放进 out[0..out_max)。
 * 返：本次解析出的帧数（<= out_max）。
 *
 * ⭐ 这个函数就是为 read() 设计的：
 *     `size_t got = proto_parser_feed_buf(&ps, buf, readed, frames, 8);`
 *    无论 readed 是半帧、1.5 帧还是 3 帧，都不会丢数据。
 * ============================================================ */
size_t proto_parser_feed_buf(struct ProtoParser *p,
                             const uint8_t *data, size_t n,
                             struct ProtoFrame *out, size_t out_max);

/* ============================================================
 * proto_parser_dump_stats —— 打印状态机统计（调试用）
 * ============================================================ */
void proto_parser_dump_stats(const char *tag, const struct ProtoParser *p);

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_H */
