# edge_ai_evt —— 教学用字符设备驱动

## 这是什么

`edge-ai-dual-node` 项目阶段 **1.2** 的产物：一个**不绑定具体硬件**的字符设备驱动，向用户态暴露一个**事件缓冲区**（对应 `docs/SPEC.md` **FR-28**）。

目的是把"一切皆文件"从口号变成能上手的东西：**用户态 `read`/`write`/`ioctl` 一个 `/dev/` 节点，背后跑的是你自己写的内核代码。**

## 文件说明

| 文件 | 说明 |
|------|------|
| `edge_ai_evt.h` | **内核态与用户态共用的 ABI 契约**：命令号、结构体、常量 |
| `edge_ai_evt.c` | 驱动本体 |
| `test_edge_ai.c` | 用户态测试程序 |
| `Makefile` | kbuild 模块 Makefile（`KDIR` 仅作占位，实际编译由 `module-deploy.sh` 传入内核树） |
| `README.md` | 本文件 |

## 构建与部署

⚠️ **不能在板卡上原生编译**（原因见知识库 **K-032**：内核树里的 host 工具是 x86-64 二进制，aarch64 板卡无法执行 → `fixdep: Exec format error`）。

在 **x86 虚拟机**上用一键脚本：

```bash
module-deploy.sh edge_ai_evt
```

它做三件事：`rsync` 拉源码 → 在 `~/tspi_sdk/kernel-6.1` 交叉编译 → `scp` 回传 `.ko`。

## 使用

```bash
# 1. 加载
sudo insmod edge_ai_evt.ko
ls -l /dev/edge_ai0            # 自动出现；主设备号动态分配（实测 510）

# 2. 读（init 时预填了一条欢迎消息）
sudo cat /dev/edge_ai0

# 3. 写（覆盖式，位置受 f_pos 控制）
echo "hello" | sudo tee /dev/edge_ai0

# 4. ioctl（先编译用户态测试程序）
gcc -Wall -o test_edge_ai test_edge_ai.c
sudo ./test_edge_ai

# 5. 卸载
sudo rmmod edge_ai_evt
```

## 接口

### 设备节点

`/dev/edge_ai0`（主设备号由内核动态分配，从高位往下找空号）

### 文件操作

| 操作 | 行为 |
|------|------|
| `open()` | 无副作用，打印日志 |
| `read(fd, buf, n)` | 从 `f_pos` 处拷贝最多 `n` 字节；读空返回 `0`（EOF） |
| `write(fd, buf, n)` | 从 `f_pos` 处覆盖写；`f_pos` 已达末尾返回 `-ENOSPC` |
| `close()` | 打印日志 |

> ⭐ 读写位置用 `*ppos`（即 `struct file::f_pos`），**每文件描述符独立** —— 所以每次 `open` 都从头读。
> 这与"用全局变量记录读位置"的行为**完全不同**（后者会导致第二个进程读到空）。

### ioctl 命令

| 命令 | 编码 | `arg` | 行为 |
|------|------|-------|------|
| `EDGE_AI_IOC_CLEAR` | `_IO('E', 0)` | 无 | 清空缓冲区（`klen = 0`） |
| `EDGE_AI_IOC_PUT` | `_IOW('E', 1, struct edge_ai_put_arg)` | 指向 `struct edge_ai_put_arg` | 设置缓冲区内容 |
| `EDGE_AI_IOC_LEN` | `_IOR('E', 2, int)` | 指向 `int` | 返回当前有效数据长度 |

**`struct edge_ai_put_arg`**：

```c
struct edge_ai_put_arg {
    int  len;              /* 数据长度，须满足 0 <= len <= BUF_SIZE */
    char data[BUF_SIZE];   /* BUF_SIZE = 256 */
};
```

### 错误码

| 错误码 | 场景 |
|--------|------|
| `EINVAL` | `PUT` 的 `len < 0` 或 `len > BUF_SIZE` |
| `EFAULT` | 用户指针非法 |
| `ENOTTY` | 未知 ioctl 命令，或命令的魔术字不是 `'E'` |
| `ENOSPC` | `write` 时 `f_pos` 已到缓冲区末尾 |

## 已知限制

- 缓冲区**静态**（`init` 时 `kmalloc` 一次），不支持动态扩容
- **不支持阻塞读**（无数据时立即返回 `0`/EOF），没有等待队列
- 读语义是"**可重复读**"（每次 `open` 从头），**不是**"一次性消费"的事件队列 —— 后者需要等待队列 + 消费式语义，留给阶段 2（串口/事件流）
- `/dev/edge_ai0` 权限是 `root:root 600`（udev 默认规则给的），普通用户需 `sudo`，或自加 udev 规则

## 学习笔记

完整原理、逐步实测记录与踩坑见 **`stage1-rk3566/docs/1.2-driver-notes.md`**（§一~§十二）。

## 相关经验条目（`knowledge-log.md`）

| 编号 | 内容 |
|------|------|
| K-032 | 交叉编译内核树的 host 工具问题 |
| K-034 | `EACCES` vs `EINVAL` 分层 |
| K-035 | IDE 报"应输入 )" → `KBUILD_MODNAME` 未定义的推理 |
| K-036 | `case` 不构成作用域 |
| K-037 | `copy_from_user` 固定长度 = 运气型 bug |
| K-038 | 无换行输出导致的视觉误判 |
| K-039 | `register_chrdev` 返回主设备号（不是 0） |
