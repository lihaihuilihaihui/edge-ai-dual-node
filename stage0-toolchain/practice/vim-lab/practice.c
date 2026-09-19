/* vim 练习文件 —— 放心改，本文件就是用来折腾的 */
#include <stdio.h>
#include <stdlib.h>

static int debug_level = 1;          /* TODO: 改成 1 */

int main(void)
{
    printf("line 1\n");
    printf("line 2\n");
    printf("line 3\n");
    printf("debug = %d\n", debug_level);
    printf("debug = %d\n", debug_level);
    return 0;
}

/* TODO: 在这一行下面添加一个新函数 */
static void test_vim(void) {}
