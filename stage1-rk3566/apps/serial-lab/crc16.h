/* crc16.h —— CRC-16/CCITT-FALSE 实现
 *
 * 参数（来自 SPEC.md §5.1）：
 *   多项式   : 0x1021  (x^16 + x^12 + x^5 + 1)
 *   初始值   : 0xFFFF
 *   输入反转 : 否
 *   输出反转 : 否
 *   输出异或 : 0x0000
 *   校验值   : 0x29B1   <-- 用字符串 "123456789" 算出来的权威基准
 *
 * 提供两个实现：
 *   crc16_bitwise() —— 逐位运算，慢（快 8 倍算什么…），但算法一目了然，用作"标准答案"
 *   crc16_table()   —— 查表法，工业标准做法，快 8 倍
 * 两者结果必须完全相同，单元测试里会互相比对。
 */

#ifndef CRC16_H
#define CRC16_H

#include <stdint.h>      /* uint8_t, uint16_t */
#include <stddef.h>      /* size_t */

#ifdef __cplusplus
extern "C" {             /* C++ 编译器看到这个，会用 C 的命名规则，避免链接找不到 */
#endif

/* ---- 逐位实现：直观、慢，作为参考实现 ---- */
uint16_t crc16_bitwise(const uint8_t *data, size_t len);

/* ---- 查表实现：快 8 倍，实际用这个 ---- */
uint16_t crc16_table(const uint8_t *data, size_t len);

/* ---- 把表打印出来（方便你眼看一遍，也可以以后硬编码进源码） ---- */
void crc16_print_table(void);

#ifdef __cplusplus
}
#endif

#endif /* CRC16_H */
