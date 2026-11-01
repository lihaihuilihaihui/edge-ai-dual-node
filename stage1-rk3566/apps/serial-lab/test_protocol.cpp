/* test_protocol.cpp —— 帧封装（proto_encode）的单元测试
 *
 * 测试分四组：
 *   1. 结构校验：最小帧 / 典型帧 / 最大帧，【逐字节】验证每个字段
 *   2. 独立重算 CRC：不信任 proto_encode 里的 CRC 计算，自己再算一遍对比
 *   3. 错误处理：载荷超长 / 缓冲区不够 / 空指针
 *   4. 边界：LEN 正好取到最小值和最大值
 *
 * 编译：g++ -Wall -o test_protocol test_protocol.cpp protocol.cpp crc16.cpp
 * 运行：./test_protocol
 */

#include "protocol.h"
#include "crc16.h"

#include <stdio.h>
#include <string.h>

/* ---------------- 测试计数器 ---------------- */
static int g_pass = 0;
static int g_fail = 0;

/* ---------------- 断言辅助 ---------------- */
static void check_u8(const char *what, uint8_t got, uint8_t expect)
{
    if (got == expect) {
        printf("  [PASS] %-34s = 0x%02X\n", what, got);
        g_pass++;
    } else {
        printf("  [FAIL] %-34s = 0x%02X  (期望 0x%02X)\n", what, got, expect);
        g_fail++;
    }
}

static void check_sz(const char *what, size_t got, size_t expect)
{
    if (got == expect) {
        printf("  [PASS] %-34s = %zu\n", what, got);
        g_pass++;
    } else {
        printf("  [FAIL] %-34s = %zu  (期望 %zu)\n", what, got, expect);
        g_fail++;
    }
}

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

/* ============================================================
 * verify_frame —— 完整校验一帧的结构
 *
 * 这是本测试的核心：拿到 proto_encode 的输出后，
 * 不"相信"它，而是【从字节流里把每个字段抠出来】，
 * 逐个和预期比较。
 *
 * 为什么要这么"较真"？
 *   因为 proto_encode 如果把 CRC 覆盖范围算错、
 *   或者字节序搞反，只看"函数返回 OK"是发现不了的 ——
 *   必须逐个字节验证，才能证明它真的拼对了。
 * ============================================================ */
static void verify_frame(const char *name,
                         const uint8_t *f, size_t total_len,
                         uint8_t expect_cmd, uint8_t expect_seq,
                         const uint8_t *expect_payload, uint8_t expect_plen)
{
    printf("\n【%s】\n", name);
    proto_dump("  原始字节", f, total_len);

    /* ---- ① 总长度 = SOF(2) + LEN字段(1) + LEN值 + CRC(2) ---- */
    const uint8_t len_field = f[2];
    check_sz("总长度 = LEN值 + 5", total_len, (size_t)len_field + 5);

    /* ---- ② 帧头 ---- */
    check_u8("SOF0", f[0], PROTO_SOF0);
    check_u8("SOF1", f[1], PROTO_SOF1);

    /* ---- ③ LEN 字段 = CMD + SEQ + PAYLOAD ---- */
    check_u8("LEN (= 2 + payload_len)", len_field, (uint8_t)(2 + expect_plen));

    /* ---- ④ CMD（在 f[3]）---- */
    check_u8("CMD  (f[3])", f[3], expect_cmd);

    /* ---- ⑤ SEQ（在 f[4]）---- */
    check_u8("SEQ  (f[4])", f[4], expect_seq);

    /* ---- ⑥ PAYLOAD（从 f[5] 开始，共 expect_plen 字节）---- */
    if (expect_plen > 0) {
        const int same = (memcmp(&f[5], expect_payload, expect_plen) == 0);
        check_eq("PAYLOAD 内容一致", same, 1);
    } else {
        printf("  [INFO] 无载荷，跳过内容比较\n");
    }

    /* ---- ⑦ ⭐ 独立重算 CRC ----
     * 关键点：这里【重新调用 crc16_table()】算一遍，
     * 而不是复用 proto_encode 内部的结果。
     *
     * 覆盖范围必须和实现一致：从 f[2]（LEN）开始，共 (len_field + 1) 字节。
     * 如果 proto_encode 少算了 LEN 字段、或多算了 SOF，这里就会不等。
     */
    const uint16_t crc_calc = crc16_table(&f[2], (size_t)len_field + 1);

    /* 从帧尾按【小端】读回（最后两字节：先 CRC_L 后 CRC_H） */
    const uint16_t crc_in_frame =
        (uint16_t)f[total_len - 2] | ((uint16_t)f[total_len - 1] << 8);

    printf("  [INFO] CRC 重算 = 0x%04X, 帧内读回 = 0x%04X\n", crc_calc, crc_in_frame);
    check_eq("CRC 独立重算与帧内一致", crc_calc == crc_in_frame, 1);

    /* 额外确认：帧内是小端存放的（低字节在前） */
    check_u8("帧内倒数第 2 字节 = CRC 低字节", f[total_len - 2], (uint8_t)(crc_calc & 0xFF));
    check_u8("帧内倒数第 1 字节 = CRC 高字节", f[total_len - 1], (uint8_t)((crc_calc >> 8) & 0xFF));
}

int main(void)
{
    printf("=========================================================\n");
    printf(" 帧封装（proto_encode）单元测试\n");
    printf(" 协议：SOF AA55 + LEN + CMD + SEQ + PAYLOAD + CRC(小端)\n");
    printf("=========================================================\n");

    uint8_t buf[128];
    size_t  out_len = 0;
    ProtoResult r;

    /* ============================================================
     * 第 1 组：结构校验（三种典型帧）
     * ============================================================ */
    printf("\n===== 第 1 组：结构校验 =====\n");

    /* --- 1.1 最小帧：只有 CMD + SEQ，无载荷 --- */
    {
        struct ProtoFrame f;
        memset(&f, 0, sizeof(f));
        f.cmd         = CMD_HEARTBEAT;   /* 0x01 */
        f.seq         = 0x00;
        f.payload_len = 0;

        r = proto_encode(&f, buf, sizeof(buf), &out_len);
        check_eq("proto_encode 返回 PROTO_OK", r == PROTO_OK, 1);
        verify_frame("1.1 最小帧（CMD+SEQ，无载荷）",
                     buf, out_len, CMD_HEARTBEAT, 0x00, NULL, 0);
    }

    /* --- 1.2 典型帧：PIR_EVENT，载荷 4 字节（uint32 时间戳） --- */
    {
        struct ProtoFrame f;
        memset(&f, 0, sizeof(f));
        f.cmd = CMD_PIR_EVENT;           /* 0x02 */
        f.seq = 0x05;
        /* 载荷：uint32 timestamp_ms = 0x00012A3B，按小端存放 */
        f.payload[0] = 0x3B;
        f.payload[1] = 0x2A;
        f.payload[2] = 0x01;
        f.payload[3] = 0x00;
        f.payload_len = 4;

        r = proto_encode(&f, buf, sizeof(buf), &out_len);
        check_eq("proto_encode 返回 PROTO_OK", r == PROTO_OK, 1);
        verify_frame("1.2 典型帧（PIR_EVENT，载荷 4 字节）",
                     buf, out_len, CMD_PIR_EVENT, 0x05, f.payload, 4);
    }

    /* --- 1.3 最大帧：载荷正好 64 字节（协议上限） --- */
    {
        struct ProtoFrame f;
        memset(&f, 0, sizeof(f));
        f.cmd = CMD_DISPLAY_SET;         /* 0x83 */
        f.seq = 0xFF;                    /* 序列号最大值 */
        for (int i = 0; i < PROTO_MAX_PAYLOAD; i++) {
            f.payload[i] = (uint8_t)i;   /* 0,1,2,...,63 */
        }
        f.payload_len = PROTO_MAX_PAYLOAD;

        r = proto_encode(&f, buf, sizeof(buf), &out_len);
        check_eq("proto_encode 返回 PROTO_OK", r == PROTO_OK, 1);
        verify_frame("1.3 最大帧（载荷 64 字节）",
                     buf, out_len, CMD_DISPLAY_SET, 0xFF, f.payload, PROTO_MAX_PAYLOAD);

        /* 额外确认：这确实是协议允许的最大帧 */
        check_sz("总长度 = PROTO_MAX_FRAME", out_len, PROTO_MAX_FRAME);
    }

    /* ============================================================
     * 第 2 组：错误处理
     * ============================================================ */
    printf("\n===== 第 2 组：错误处理 =====\n");

    /* --- 2.1 载荷超长（65 > 上限 64）--- */
    {
        struct ProtoFrame f;
        memset(&f, 0, sizeof(f));
        f.cmd         = 0x01;
        f.seq         = 0x00;
        f.payload_len = PROTO_MAX_PAYLOAD + 1;   /* 65 */

        r = proto_encode(&f, buf, sizeof(buf), &out_len);
        check_eq("载荷 65 字节 → 返回 PROTO_ERR_PAYLOAD_TOO_LONG",
                 r == PROTO_ERR_PAYLOAD_TOO_LONG, 1);
    }

    /* --- 2.2 输出缓冲区太小 ---
     * 这一帧的尺寸计算（⚠️ 第一次我在这里写错了，记下来）：
     *      payload_len = 4
     *      LEN 值      = CMD(1) + SEQ(1) + PAYLOAD(4) = 6   ← 不是 4！
     *      整帧        = SOF(2) + LEN字段(1) + 6 + CRC(2) = 11
     * 故意给 8 字节的缓冲区，应该被拒绕。 */
    {
        struct ProtoFrame f;
        memset(&f, 0, sizeof(f));
        f.cmd         = CMD_PIR_EVENT;
        f.seq         = 0x00;
        f.payload_len = 4;

        uint8_t small[8];                        /* 故意只给 8 字节（实际需要 11） */
        r = proto_encode(&f, small, sizeof(small), &out_len);
        check_eq("缓冲区 8 字节装 11 字节帧 → 返回 PROTO_ERR_BUF_TOO_SMALL",
                 r == PROTO_ERR_BUF_TOO_SMALL, 1);

        /* 反过来验证：给够 11 字节就能成功 */
        uint8_t enough[11];
        r = proto_encode(&f, enough, sizeof(enough), &out_len);
        check_eq("缓冲区 11 字节装 11 字节帧 → 成功", r == PROTO_OK, 1);
    }

    /* --- 2.3 空指针 --- */
    {
        struct ProtoFrame f;
        memset(&f, 0, sizeof(f));
        f.payload_len = 0;

        r = proto_encode(NULL, buf, sizeof(buf), &out_len);
        check_eq("frame = NULL → 返回 PROTO_ERR_NULL", r == PROTO_ERR_NULL, 1);

        r = proto_encode(&f, NULL, sizeof(buf), &out_len);
        check_eq("out_buf = NULL → 返回 PROTO_ERR_NULL", r == PROTO_ERR_NULL, 1);

        r = proto_encode(&f, buf, sizeof(buf), NULL);
        check_eq("out_len = NULL → 返回 PROTO_ERR_NULL", r == PROTO_ERR_NULL, 1);
    }

    /* ============================================================
     * 第 3 组：CRC 覆盖范围的"负向验证"
     *
     * 前面的 verify_frame 已经验证"正确范围能算出一样的 CRC"。
     * 这里反过来验证：如果【用错范围】算 CRC，结果一定不同 ——
     * 这证明我们的范围确实被"用上了"，而不是碰巧一致。
     * ============================================================ */
    printf("\n===== 第 3 组：CRC 覆盖范围负向验证 =====\n");
    {
        struct ProtoFrame f;
        memset(&f, 0, sizeof(f));
        f.cmd         = CMD_PIR_EVENT;
        f.seq         = 0x05;
        f.payload_len = 4;

        r = proto_encode(&f, buf, sizeof(buf), &out_len);
        check_eq("proto_encode 返回 PROTO_OK", r == PROTO_OK, 1);

        const uint8_t  len_field = buf[2];
        const uint16_t crc_right = crc16_table(&buf[2], (size_t)len_field + 1);

        /* 范围少了 1 字节（漏掉 LEN 字段）*/
        const uint16_t crc_miss_len = crc16_table(&buf[3], (size_t)len_field);
        /* 范围多了 2 字节（把 SOF 也算进去）*/
        const uint16_t crc_with_sof = crc16_table(&buf[0], (size_t)len_field + 3);

        printf("  [INFO] 正确范围 CRC       = 0x%04X\n", crc_right);
        printf("  [INFO] 漏掉 LEN 字段      = 0x%04X\n", crc_miss_len);
        printf("  [INFO] 多算 SOF           = 0x%04X\n", crc_with_sof);

        check_eq("少算 LEN 字段 → CRC 不同", crc_miss_len != crc_right, 1);
        check_eq("多算 SOF      → CRC 不同", crc_with_sof != crc_right, 1);
    }

    /* ============================================================
     * 第 4 组：LEN 边界值
     * ============================================================ */
    printf("\n===== 第 4 组：LEN 边界值 =====\n");
    {
        /* LEN 最小值 2（无载荷） */
        struct ProtoFrame f0;
        memset(&f0, 0, sizeof(f0));
        f0.cmd = 0x01; f0.seq = 0x00; f0.payload_len = 0;
        r = proto_encode(&f0, buf, sizeof(buf), &out_len);
        check_u8("LEN 最小值（无载荷）", buf[2], PROTO_MIN_LEN);

        /* LEN 最大值 66（载荷 64） */
        struct ProtoFrame f1;
        memset(&f1, 0, sizeof(f1));
        f1.cmd = 0x01; f1.seq = 0x00; f1.payload_len = PROTO_MAX_PAYLOAD;
        r = proto_encode(&f1, buf, sizeof(buf), &out_len);
        check_u8("LEN 最大值（载荷 64）", buf[2], PROTO_MAX_LEN);
    }

    /* ---------------- 汇总 ---------------- */
    printf("\n=========================================================\n");
    printf(" 通过: %d    失败: %d\n", g_pass, g_fail);
    printf("=========================================================\n");

    return (g_fail == 0) ? 0 : 1;
}
