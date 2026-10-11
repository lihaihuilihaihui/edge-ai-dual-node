/* test_crc16.cpp —— CRC-16/CCITT-FALSE 的单元测试
 *
 * 测试分三组：
 *   1. 黄金测试向量（"123456789" → 0x29B1）—— 全世界通用，算不出就是错的
 *   2. 边界与特例（空数据、单字节 0x00、全 0xFF…）
 *   3. 两种实现互相比对（逐位 vs 查表，用随机数据交叉验证）
 *
 * 编译：g++ -Wall -o test_crc16 test_crc16.cpp crc16.cpp
 * 运行：./test_crc16
 */

#include "crc16.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---------------- 测试计数器 ---------------- */
static int g_pass = 0;
static int g_fail = 0;

static void check(const char *name, uint16_t got, uint16_t expect)
{
    if (got == expect) {
        printf("  [PASS] %-45s CRC = 0x%04X\n", name, got);
        g_pass++;
    } else {
        printf("  [FAIL] %-45s CRC = 0x%04X  (期望 0x%04X)  ← 差 0x%04X\n",
               name, got, expect, (uint16_t)(got ^ expect));
        g_fail++;
    }
}

int main(void)
{
    printf("=========================================================\n");
    printf(" CRC-16/CCITT-FALSE 单元测试\n");
    printf("   多项式=0x1021  初始值=0xFFFF  输入/输出不反转  无异或\n");
    printf("=========================================================\n\n");

    /* ============================================================
     * 第 1 组：黄金测试向量
     *
     * "123456789" 是 CRC 界的标准测试串。
     * 全世界任何一份 CRC 参数表都用它来给出 "check" 值。
     * 对 CRC-16/CCITT-FALSE，这个值固定是 0x29B1。
     * 只要这个不对，后面全都不用看了。
     * ============================================================ */
    printf("【第 1 组】黄金测试向量\n");
    {
        const char *s = "123456789";      /* 注意：不含结尾的 '\0'！ */
        uint16_t expect = 0x29B1;

        check("\"123456789\"  逐位实现", crc16_bitwise((const uint8_t *)s, strlen(s)), expect);
        check("\"123456789\"  查表实现", crc16_table  ((const uint8_t *)s, strlen(s)), expect);
    }

    /* ============================================================
     * 第 2 组：边界与特例
     *
     * 这些期望值我"不能"凭空写出来，所以先跑一遍程序拿到实际值，
     * 再人工核对"是否有道理"。这个测试主要是防回归（以后改代码别改坏）。
     *
     * 其中"空数据"特别值得看：结果应该正好等于初始值 0xFFFF。
     * ============================================================ */
    printf("\n【第 2 组】边界与特例\n");
    {
        /* 空数据：一个字节都不处理，CRC 应该保持初始值不变 */
        check("空数据 (len=0)", crc16_bitwise(NULL, 0), 0xFFFF);

        /* 单个 0x00 */
        {
            const uint8_t d[] = { 0x00 };
            uint16_t a = crc16_bitwise(d, 1);
            uint16_t b = crc16_table  (d, 1);
            printf("  [INFO] 单字节 0x00            → 逐位=0x%04X  查表=0x%04X\n", a, b);
            if (a == b) { printf("  [PASS] 两种实现一致\n"); g_pass++; }
            else        { printf("  [FAIL] 两种实现不一致!\n"); g_fail++; }
        }

        /* 单个 0xFF */
        {
            const uint8_t d[] = { 0xFF };
            uint16_t a = crc16_bitwise(d, 1);
            uint16_t b = crc16_table  (d, 1);
            printf("  [INFO] 单字节 0xFF            → 逐位=0x%04X  查表=0x%04X\n", a, b);
            if (a == b) { printf("  [PASS] 两种实现一致\n"); g_pass++; }
            else        { printf("  [FAIL] 两种实现不一致!\n"); g_fail++; }
        }

        /* 一个典型的一帧数据长度（约 70 字节）全 0xAA */
        {
            uint8_t d[70];
            memset(d, 0xAA, sizeof(d));
            uint16_t a = crc16_bitwise(d, sizeof(d));
            uint16_t b = crc16_table  (d, sizeof(d));
            printf("  [INFO] 70 字节全 0xAA         → 逐位=0x%04X  查表=0x%04X\n", a, b);
            if (a == b) { printf("  [PASS] 两种实现一致\n"); g_pass++; }
            else        { printf("  [FAIL] 两种实现不一致!\n"); g_fail++; }
        }
    }

    /* ============================================================
     * 第 3 组：随机数据交叉验证
     *
     * 随机生成 1000 组不同长度、不同内容的数据，
     * 逐一比较"逐位实现"和"查表实现"的结果。
     *
     * 为什么这样测有效？
     *   逐位实现只有 6 行，逻辑一目了然，不容易错，可以当"标准答案"。
     *   查表实现的表是算出来的，如果建表逻辑有 bug，两边就会不一致。
     *   而且随机数据能覆盖到各种位模式，比手写几个用例强得多。
     * ============================================================ */
    printf("\n【第 3 组】随机数据交叉验证（1000 组）\n");
    {
        srand(12345);         /* 固定种子 → 每次跑结果一样，便于复现 */
        int mismatch = 0;

        for (int t = 0; t < 1000; t++) {
            size_t len = (size_t)(rand() % 128);      /* 0 ~ 127 字节 */
            uint8_t buf[128];
            for (size_t i = 0; i < len; i++) {
                buf[i] = (uint8_t)(rand() & 0xFF);
            }

            uint16_t a = crc16_bitwise(buf, len);
            uint16_t b = crc16_table  (buf, len);

            if (a != b) {
                printf("  [FAIL] 第 %d 组不一致: len=%zu  逐位=0x%04X  查表=0x%04X\n",
                       t, len, a, b);
                mismatch++;
            }
        }

        if (mismatch == 0) {
            printf("  [PASS] 1000 组全部一致\n");
            g_pass++;
        } else {
            printf("  [FAIL] 有 %d 组不一致\n", mismatch);
            g_fail++;
        }
    }

    /* ============================================================
     * 第 4 组：CRC 的"纠错能力"演示
     *
     * 这是理解 CRC 为什么有用的最好方式：
     *   把原始数据改一个 bit，看 CRC 会不会变。
     *   会变 → 说明这个错误能被检出。
     * ============================================================ */
    printf("\n【第 4 组】改一个 bit，CRC 应该变\n");
    {
        const char *s = "123456789";
        uint8_t buf[16];
        memcpy(buf, s, strlen(s));
        size_t len = strlen(s);

        uint16_t base = crc16_table(buf, len);
        printf("  [INFO] 原始数据 CRC = 0x%04X\n", base);

        int detected = 0;
        int total = 0;

        /* 逐个翻转 len*8 个 bit 中的每一个，看 CRC 是否都变了 */
        for (size_t i = 0; i < len; i++) {
            for (int bit = 0; bit < 8; bit++) {
                buf[i] ^= (uint8_t)(1u << bit);         /* 翻转一个 bit */
                uint16_t c = crc16_table(buf, len);
                buf[i] ^= (uint8_t)(1u << bit);         /* 翻回来 */

                total++;
                if (c != base) detected++;
            }
        }

        printf("  [INFO] 翻转 %d 个不同的 bit，CRC 变化了 %d 次\n", total, detected);
        if (detected == total) {
            printf("  [PASS] 单 bit 错误 100%% 能被检出\n");
            g_pass++;
        } else {
            printf("  [FAIL] 有 %d 个单 bit 错误漏检\n", total - detected);
            g_fail++;
        }
    }

    /* ---------------- 汇总 ---------------- */
    printf("\n=========================================================\n");
    printf(" 通过: %d    失败: %d\n", g_pass, g_fail);
    printf("=========================================================\n");

    return (g_fail == 0) ? 0 : 1;
}
