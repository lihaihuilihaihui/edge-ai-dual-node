/* test_protocol_parse.cpp —— 帧解析状态机的单元测试
 *
 * 测试分十组：
 *   1. 逐字节喂：三种典型帧，验证中间态与最终态
 *   2. 粘包    ：多帧一次喂
 *   3. 半包    ：按 1/2/3/.../7 字节乱切分喂
 *   4. 垃圾前缀：帧外混入噪声字节
 *   5. 载荷含 AA 55：验证 LEN 字段保护了载荷
 *   6. 重复帧头 AA AA 55
 *   7. CRC 错 + 后续恢复（这是最关键的容错测试）
 *   8. LEN 非法：1 / 67 / 255
 *   9. 往返 1000 帧随机数据（encode → feed → 逐帧还原）
 *  10. 空参数 / out_max = 0 的边界
 *
 * 编译：g++ -Wall -o test_protocol_parse test_protocol_parse.cpp protocol.cpp crc16.cpp
 * 运行：./test_protocol_parse
 */

#include "protocol.h"
#include "crc16.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>     /* rand / srand */

/* ============================================================
 * 测试辅助
 * ============================================================ */
static int g_pass = 0;
static int g_fail = 0;

static void check_eq(const char *what, int got, int expect)
{
    if (got == expect) {
        printf("  [PASS] %s\n", what);
        g_pass++;
    } else {
        printf("  [FAIL] %s  (得到 %d，期望 %d)\n", what, got, expect);
        g_fail++;
    }
}

static void check_sz(const char *what, size_t got, size_t expect)
{
    if (got == expect) {
        printf("  [PASS] %-40s = %zu\n", what, got);
        g_pass++;
    } else {
        printf("  [FAIL] %-40s = %zu  (期望 %zu)\n", what, got, expect);
        g_fail++;
    }
}

static void check_u8(const char *what, uint8_t got, uint8_t expect)
{
    if (got == expect) {
        printf("  [PASS] %-40s = 0x%02X\n", what, got);
        g_pass++;
    } else {
        printf("  [FAIL] %-40s = 0x%02X  (期望 0x%02X)\n", what, got, expect);
        g_fail++;
    }
}

/* ------------------------------------------------------------
 * make_frame —— 构造并编码一帧，返回字节数（失败返回 0）
 *
 * ⭐ 这是"测试复用生产代码"的辅助函数。
 *    parse 测试里我们有大量的"造一帧"需求，
 *    如果每次都手写字节序列，既啰嗦又容易写错。
 *
 *    用 proto_encode 造帧是【可接受的】，因为：
 *      - encode 本身已经被 test_protocol.cpp 单独验证过
 *      - 这里测的是 parse，不是 encode
 *
 *    但要注意：这意味 encode 错了 parse 测试也会一起错。
 *    所以 test_protocol.cpp 里对 encode 的【独立字节级校验】不可少。
 * ------------------------------------------------------------ */
static size_t make_frame(uint8_t cmd, uint8_t seq,
                         const uint8_t *pl, uint8_t pl_len,
                         uint8_t *buf)
{
    struct ProtoFrame f;
    memset(&f, 0, sizeof(f));
    f.cmd         = cmd;
    f.seq         = seq;
    f.payload_len = pl_len;
    if (pl != NULL && pl_len > 0) {
        memcpy(f.payload, pl, pl_len);
    }

    size_t n = 0;
    if (proto_encode(&f, buf, PROTO_MAX_FRAME, &n) != PROTO_OK) {
        return 0;
    }
    return n;
}

/* 两个帧是否完全相等 */
static int frame_eq(const struct ProtoFrame *a, const struct ProtoFrame *b)
{
    return (a->cmd == b->cmd)
        && (a->seq == b->seq)
        && (a->payload_len == b->payload_len)
        && (memcmp(a->payload, b->payload, a->payload_len) == 0);
}

/* ============================================================
 * 第 1 组：逐字节喂
 * ============================================================ */
static void test_byte_by_byte(void)
{
    printf("\n===== 第 1 组：逐字节喂 =====\n");

    const uint8_t pl4[4] = {0x3B, 0x2A, 0x01, 0x00};
    uint8_t pl64[PROTO_MAX_PAYLOAD];
    for (int i = 0; i < PROTO_MAX_PAYLOAD; i++) {
        pl64[i] = (uint8_t)i;
    }

    struct {
        const char     *name;
        uint8_t         cmd;
        uint8_t         seq;
        const uint8_t  *pl;
        uint8_t         pl_len;
    } cases[] = {
        { "1.1 最小帧（无载荷）",     CMD_HEARTBEAT,   0x00, NULL, 0 },
        { "1.2 典型帧（载荷 4B）",   CMD_PIR_EVENT,   0x05, pl4,  4 },
        { "1.3 最大帧（载荷 64B）",  CMD_DISPLAY_SET, 0xFF, pl64, PROTO_MAX_PAYLOAD },
    };

    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        printf("\n【%s】\n", cases[c].name);

        uint8_t raw[PROTO_MAX_FRAME];
        size_t  n = make_frame(cases[c].cmd, cases[c].seq,
                               cases[c].pl, cases[c].pl_len, raw);
        check_eq("proto_encode 造帧成功", n > 0, 1);
        if (n == 0) {
            continue;
        }
        proto_dump("  输入字节", raw, n);

        struct ProtoParser ps;
        proto_parser_init(&ps);
        check_eq("init 后状态 = PST_SOF0", ps.state == PST_SOF0, 1);

        struct ProtoFrame got;
        memset(&got, 0, sizeof(got));

        /* ⭐ 关键测试点：喂前 n-1 个字节，【都必须】返回 NONE。
         *    这验证了"状态机不会在帧没凑齐时提前吐帧"。 */
        int mid_produced = 0;
        for (size_t i = 0; i + 1 < n; i++) {
            if (proto_parser_feed(&ps, raw[i], &got) != PROTO_FEED_NONE) {
                mid_produced++;
            }
        }
        check_eq("前 n-1 字节都不产生帧", mid_produced, 0);

        /* 最后一个字节应该凑齐 */
        enum ProtoFeedResult r = proto_parser_feed(&ps, raw[n - 1], &got);
        check_eq("最后一字节返回 FRAME", r == PROTO_FEED_FRAME, 1);

        /* 还原出来的帧必须和原始帧完全一致 */
        struct ProtoFrame want;
        memset(&want, 0, sizeof(want));
        want.cmd         = cases[c].cmd;
        want.seq         = cases[c].seq;
        want.payload_len = cases[c].pl_len;
        if (cases[c].pl != NULL && cases[c].pl_len > 0) {
            memcpy(want.payload, cases[c].pl, cases[c].pl_len);
        }
        check_eq("还原的帧与原始帧完全一致", frame_eq(&got, &want), 1);

        check_sz("n_ok", ps.n_ok, 1);
        check_sz("n_crc_err", ps.n_crc_err, 0);
        check_sz("n_dropped", ps.n_dropped, 0);
        check_eq("回到 PST_SOF0 等下一帧", ps.state == PST_SOF0, 1);
    }
}

/* ============================================================
 * 第 2 组：粘包 —— 三帧一次喂
 * ============================================================ */
static void test_piggyback(void)
{
    printf("\n===== 第 2 组：粘包（3 帧一次喂）=====\n");

    uint8_t stream[3 * PROTO_MAX_FRAME];
    size_t  off = 0;
    off += make_frame(CMD_HEARTBEAT, 1, NULL, 0, stream + off);
    off += make_frame(CMD_PIR_EVENT, 2, (const uint8_t *)"\xAA\xBB\xCC\xDD", 4, stream + off);
    off += make_frame(CMD_ENV_DATA,  3, (const uint8_t *)"\x01\x02", 2, stream + off);

    proto_dump("  三帧拼接", stream, off);

    struct ProtoParser ps;
    proto_parser_init(&ps);

    struct ProtoFrame outs[4];
    memset(outs, 0, sizeof(outs));
    size_t got = proto_parser_feed_buf(&ps, stream, off, outs, 4);

    check_sz("一次喂 3 帧 → 解析出 3 帧", got, 3);

    check_u8("第 1 帧 cmd", outs[0].cmd, CMD_HEARTBEAT);
    check_u8("第 1 帧 seq", outs[0].seq, 1);
    check_u8("第 2 帧 cmd", outs[1].cmd, CMD_PIR_EVENT);
    check_u8("第 2 帧 seq", outs[1].seq, 2);
    check_u8("第 3 帧 cmd", outs[2].cmd, CMD_ENV_DATA);
    check_u8("第 3 帧 seq", outs[2].seq, 3);
    check_eq("第 2 帧载荷一致", memcmp(outs[1].payload, "\xAA\xBB\xCC\xDD", 4) == 0, 1);
    check_sz("n_ok", ps.n_ok, 3);
    check_sz("n_crc_err", ps.n_crc_err, 0);
}

/* ============================================================
 * 第 3 组：半包 —— 乱切分喂
 * ============================================================ */
static void test_fragmented(void)
{
    printf("\n===== 第 3 组：半包（按 1,2,3,...,7,1,2... 乱切分）=====\n");

    /* 造 5 帧拼成一条流 */
    uint8_t stream[5 * PROTO_MAX_FRAME];
    size_t  off = 0;
    for (int i = 0; i < 5; i++) {
        uint8_t pl[8];
        uint8_t pl_len = (uint8_t)(i * 2);          /* 0,2,4,6,8 */
        for (int j = 0; j < pl_len; j++) {
            pl[j] = (uint8_t)(i * 16 + j);
        }
        off += make_frame((uint8_t)(0x10 + i), (uint8_t)i, pl, pl_len, stream + off);
    }

    proto_dump("  原始流", stream, off);

    struct ProtoParser ps;
    proto_parser_init(&ps);

    size_t pos = 0, chunk = 1, frames = 0;
    size_t feeds = 0;

    while (pos < off) {
        size_t take = chunk;
        if (pos + take > off) {
            take = off - pos;
        }

        struct ProtoFrame tmp[5];
        frames += proto_parser_feed_buf(&ps, stream + pos, take, tmp, 5);

        pos += take;
        feeds++;
        chunk = chunk % 7 + 1;      /* 1→2→3→4→5→6→7→1→2... */
    }

    printf("  [INFO] 共切成 %zu 次喂入（每次 1~7 字节）\n", feeds);
    check_sz("乱切分后仍解析出 5 帧", frames, 5);
    check_sz("n_ok", ps.n_ok, 5);
    check_sz("n_crc_err", ps.n_crc_err, 0);
    check_sz("n_dropped", ps.n_dropped, 0);
}

/* ============================================================
 * 第 4 组：垃圾前缀
 * ============================================================ */
static void test_garbage_prefix(void)
{
    printf("\n===== 第 4 组：垃圾字节前缀 =====\n");

    /* ⚠️ 故意让垃圾里含一个 0x55：它不应该被误认为 SOF1 */
    const uint8_t junk[] = { 0x00, 0x11, 0x22, 0xFF, 0x55, 0xAA };
    /*                                                      ↑ 最后这个是 0xAA，
     *                                                        它会让状态机进入 PST_SOF1！ */

    uint8_t raw[PROTO_MAX_FRAME];
    size_t  n = make_frame(CMD_HEARTBEAT, 0x42, NULL, 0, raw);

    struct ProtoParser ps;
    proto_parser_init(&ps);

    struct ProtoFrame outs[2];
    size_t g1 = proto_parser_feed_buf(&ps, junk, sizeof(junk), outs, 2);
    check_sz("垃圾字节本身不产生帧", g1, 0);
    check_eq("垃圾末尾的 0xAA 让状态机进入 PST_SOF1",
             ps.state == PST_SOF1, 1);

    /* 现在紧跟一个真帧：它的 SOF0(0xAA) 会被当作"重复帧头"处理 —— 
     *   → 这正是 PST_SOF1 里 else-if 分支的用武之地！ */
    size_t g2 = proto_parser_feed_buf(&ps, raw, n, outs, 2);
    check_sz("垃圾后紧跟真帧 → 解析出 1 帧", g2, 1);
    check_u8("还原 seq 正确", outs[0].seq, 0x42);
    check_sz("n_ok", ps.n_ok, 1);
    check_sz("n_crc_err", ps.n_crc_err, 0);

    printf("  [INFO] n_dropped = %u（垃圾 + 被 SOF 逻辑吞掉的）\n",
           (unsigned)ps.n_dropped);
}

/* ============================================================
 * 第 5 组：载荷里含 AA 55
 * ============================================================ */
static void test_payload_with_sof(void)
{
    printf("\n===== 第 5 组：载荷里含假帧头 AA 55 =====\n");

    /* 载荷故意由 AA 55 AA 00 55 组成 —— 全是"看起来像帧头"的字节 */
    const uint8_t tricky[5] = { 0xAA, 0x55, 0xAA, 0x00, 0x55 };

    uint8_t raw[PROTO_MAX_FRAME];
    size_t  n = make_frame(CMD_DISPLAY_SET, 0x42, tricky, 5, raw);
    proto_dump("  编码结果", raw, n);

    struct ProtoParser ps;
    proto_parser_init(&ps);

    struct ProtoFrame got;
    memset(&got, 0, sizeof(got));
    size_t g = proto_parser_feed_buf(&ps, raw, n, &got, 1);

    check_sz("解析出 1 帧", g, 1);
    check_u8("payload_len = 5", got.payload_len, 5);
    check_eq("载荷原样保留（没被当成帧头）",
             memcmp(got.payload, tricky, 5) == 0, 1);
    check_sz("n_crc_err", ps.n_crc_err, 0);

    /* 额外做一次逐字节喂，确认中途不会"被假帧头带跑" */
    proto_parser_init(&ps);
    memset(&got, 0, sizeof(got));
    int mid_produced = 0;
    for (size_t i = 0; i < n; i++) {
        if (proto_parser_feed(&ps, raw[i], &got) == PROTO_FEED_FRAME && i + 1 < n) {
            mid_produced++;     /* 提前吐帧 = 错 */
        }
    }
    check_eq("逐字节喂时没有提前吐帧", mid_produced, 0);
}

/* ============================================================
 * 第 6 组：重复帧头 AA AA 55
 * ============================================================ */
static void test_double_sof(void)
{
    printf("\n===== 第 6 组：重复帧头 AA AA 55 =====\n");

    uint8_t raw[PROTO_MAX_FRAME];
    size_t  n = make_frame(CMD_HEARTBEAT, 0x07, NULL, 0, raw);

    /* 在真帧前面插入一个多余的 0xAA */
    uint8_t dup[PROTO_MAX_FRAME + 1];
    dup[0] = 0xAA;
    memcpy(dup + 1, raw, n);
    proto_dump("  插入冗余 0xAA", dup, n + 1);

    struct ProtoParser ps;
    proto_parser_init(&ps);

    struct ProtoFrame outs[2];
    size_t g = proto_parser_feed_buf(&ps, dup, n + 1, outs, 2);

    check_sz("解析出 1 帧", g, 1);
    check_u8("seq 正确", outs[0].seq, 0x07);
    check_sz("n_dropped = 0（AA 是重复帧头，不算垃圾）", ps.n_dropped, 0);
    check_sz("n_crc_err", ps.n_crc_err, 0);
}

/* ============================================================
 * 第 7 组：CRC 错 + 后续恢复（最关键的容错测试）
 * ============================================================ */
static void test_crc_error_recovery(void)
{
    printf("\n===== 第 7 组：CRC 错 + 后续恢复 =====\n");

    /* --- 造一个"被破坏"的帧 --- */
    const uint8_t pl4[4] = { 0x11, 0x22, 0x33, 0x44 };
    uint8_t bad[PROTO_MAX_FRAME];
    size_t  bad_n = make_frame(CMD_PIR_EVENT, 5, pl4, 4, bad);

    /* 翻转 CRC 的最后一个字节 → CRC 必然不匹配 */
    bad[bad_n - 1] ^= 0x01;
    proto_dump("  被破坏的帧", bad, bad_n);

    /* --- 拼流：[坏帧][好帧 seq=10][好帧 seq=11] --- */
    uint8_t stream[3 * PROTO_MAX_FRAME];
    size_t  off = 0;
    memcpy(stream + off, bad, bad_n);
    off += bad_n;
    off += make_frame(CMD_HEARTBEAT, 10, NULL, 0, stream + off);
    off += make_frame(CMD_HEARTBEAT, 11, NULL, 0, stream + off);

    struct ProtoParser ps;
    proto_parser_init(&ps);

    struct ProtoFrame outs[4];
    memset(outs, 0, sizeof(outs));
    size_t g = proto_parser_feed_buf(&ps, stream, off, outs, 4);

    check_sz("坏帧被丢弃，只解析出 2 帧", g, 2);
    check_sz("n_crc_err = 1", ps.n_crc_err, 1);
    check_u8("第 1 帧 seq = 10", outs[0].seq, 10);
    check_u8("第 2 帧 seq = 11", outs[1].seq, 11);
    check_sz("n_ok", ps.n_ok, 2);
    check_eq("状态机已复位，可继续用", ps.state == PST_SOF0, 1);

    /* ⭐ 这个测试证明：
     *    CRC 失败后状态机【干净地回到 PST_SOF0】，
     *    不会"卡在中间"把后续所有帧都吃掉。
     *    （当然后续帧如果被吃掉 1-2 字节，仍会连锁失败 —— 
     *      那是"字节流错位"的固有代价，见 docs 里的取舍讨论。） */
}

/* ============================================================
 * 第 8 组：LEN 非法
 * ============================================================ */
static void test_bad_len(void)
{
    printf("\n===== 第 8 组：LEN 非法 =====\n");

    const uint8_t bad_lens[] = { 1, 67, 255 };

    for (size_t k = 0; k < sizeof(bad_lens) / sizeof(bad_lens[0]); k++) {
        uint8_t s[4];
        s[0] = PROTO_SOF0;
        s[1] = PROTO_SOF1;
        s[2] = bad_lens[k];
        s[3] = 0x00;

        struct ProtoParser ps;
        proto_parser_init(&ps);

        struct ProtoFrame tmp[2];
        size_t g = proto_parser_feed_buf(&ps, s, sizeof(s), tmp, 2);

        char msg[96];
        snprintf(msg, sizeof(msg),
                 "LEN = %3u → n_len_err=1，0 帧输出", (unsigned)bad_lens[k]);
        check_eq(msg, (ps.n_len_err == 1) && (g == 0), 1);
    }

    /* 边界确认：LEN = 2 和 LEN = 66 是【合法】的 */
    {
        uint8_t raw[PROTO_MAX_FRAME];

        size_t n_min = make_frame(CMD_HEARTBEAT, 1, NULL, 0, raw);
        check_eq("造最小帧成功", n_min > 0, 1);
        check_u8("LEN = 2（下界）合法", raw[2], PROTO_MIN_LEN);

        size_t n_max = make_frame(CMD_HEARTBEAT, 1, NULL, PROTO_MAX_PAYLOAD, raw);
        check_eq("造最大帧成功", n_max > 0, 1);
        check_u8("LEN = 66（上界）合法", raw[2], PROTO_MAX_LEN);
    }
}

/* ============================================================
 * 第 9 组：往返 1000 帧随机数据
 * ============================================================ */
static void test_roundtrip_random(void)
{
    printf("\n===== 第 9 组：往返 1000 帧随机数据 =====\n");

    /* 每批攒 8 帧一起喂 —— 这样 chunk 大小 = 8 * 71 = 568 字节，
     * 最多就是 8 帧，outs[8] 刚好够。 */
    static uint8_t chunk[8 * PROTO_MAX_FRAME];

    srand(20261015);        /* ⭐ 固定种子 → 可复现 */

    struct ProtoParser ps;
    proto_parser_init(&ps);

    size_t off = 0, in_chunk = 0, produced = 0;
    int    encode_fail = 0;

    for (int i = 0; i < 1000; i++) {
        struct ProtoFrame src;
        memset(&src, 0, sizeof(src));
        src.cmd         = (uint8_t)(rand() & 0xFF);
        src.seq         = (uint8_t)(rand() & 0xFF);
        src.payload_len = (uint8_t)(rand() % (PROTO_MAX_PAYLOAD + 1));
        for (int j = 0; j < src.payload_len; j++) {
            src.payload[j] = (uint8_t)(rand() & 0xFF);
        }

        uint8_t raw[PROTO_MAX_FRAME];
        size_t  n = 0;
        if (proto_encode(&src, raw, sizeof(raw), &n) != PROTO_OK) {
            encode_fail++;
            continue;
        }

        memcpy(chunk + off, raw, n);
        off      += n;
        in_chunk += 1;

        if (in_chunk == 8) {
            struct ProtoFrame outs[8];
            produced += proto_parser_feed_buf(&ps, chunk, off, outs, 8);
            off      = 0;
            in_chunk = 0;
        }
    }

    /* 处理最后不足 8 帧的尾巴 */
    if (in_chunk > 0) {
        struct ProtoFrame outs[8];
        produced += proto_parser_feed_buf(&ps, chunk, off, outs, 8);
    }

    check_sz("encode 失败次数", (size_t)encode_fail, 0);
    check_sz("往返解析出的帧数", produced, 1000);
    check_sz("n_ok", ps.n_ok, 1000);
    check_sz("n_crc_err", ps.n_crc_err, 0);
    check_sz("n_len_err", ps.n_len_err, 0);
    check_sz("n_dropped", ps.n_dropped, 0);
    check_sz("n_overrun", ps.n_overrun, 0);

    proto_parser_dump_stats("  最终统计", &ps);
}

/* ============================================================
 * 第 10 组：空参数 / out_max = 0
 * ============================================================ */
static void test_edge_params(void)
{
    printf("\n===== 第 10 组：空参数与 out_max 边界 =====\n");

    uint8_t raw[PROTO_MAX_FRAME];
    size_t  n = make_frame(CMD_HEARTBEAT, 1, NULL, 0, raw);

    struct ProtoParser ps;
    proto_parser_init(&ps);

    struct ProtoFrame tmp[2];

    check_sz("out_max = 0 → 返回 0",
             proto_parser_feed_buf(&ps, raw, n, tmp, 0), 0);
    check_sz("out_max = 0 → 数据未被消耗（n_ok 仍为 0）", ps.n_ok, 0);
    check_eq("out_max = 0 → 状态机未被推进", ps.state == PST_SOF0, 1);

    check_sz("p = NULL → 返回 0",
             proto_parser_feed_buf(NULL, raw, n, tmp, 2), 0);
    check_sz("data = NULL → 返回 0",
             proto_parser_feed_buf(&ps, NULL, n, tmp, 2), 0);
    check_sz("out = NULL → 返回 0",
             proto_parser_feed_buf(&ps, raw, n, NULL, 2), 0);

    /* feed 的 NULL 检查 */
    struct ProtoFrame got;
    check_eq("feed(p=NULL) → PROTO_FEED_NONE",
             proto_parser_feed(NULL, 0xAA, &got) == PROTO_FEED_NONE, 1);
    check_eq("feed(out=NULL) → PROTO_FEED_NONE",
             proto_parser_feed(&ps, 0xAA, NULL) == PROTO_FEED_NONE, 1);

    /* init(NULL) 不应崩溃 */
    proto_parser_init(NULL);
    check_eq("init(NULL) 不崩溃", 1, 1);

    /* 正常路径仍然可用 */
    proto_parser_init(&ps);
    size_t g = proto_parser_feed_buf(&ps, raw, n, tmp, 2);
    check_sz("空参数测试后仍能正常解析", g, 1);
}

/* ============================================================
 * main
 * ============================================================ */
int main(void)
{
    printf("=========================================================\n");
    printf(" 帧解析状态机 单元测试\n");
    printf(" 协议：SOF AA55 + LEN + CMD + SEQ + PAYLOAD + CRC(小端)\n");
    printf("=========================================================\n");

    test_byte_by_byte();
    test_piggyback();
    test_fragmented();
    test_garbage_prefix();
    test_payload_with_sof();
    test_double_sof();
    test_crc_error_recovery();
    test_bad_len();
    test_roundtrip_random();
    test_edge_params();

    printf("\n=========================================================\n");
    printf(" 通过: %d    失败: %d\n", g_pass, g_fail);
    printf("=========================================================\n");

    return (g_fail == 0) ? 0 : 1;
}
