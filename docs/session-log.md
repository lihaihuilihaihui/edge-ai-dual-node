# 会话与设备状态记录（延续对话上下文用）

> 本文件用于跨会话延续上下文。新对话开始时若涉及本工程，先读本文件。

## 设备信息（已实测确认）
- 板卡：立创开发板 **Lckfb TSPI V10**，SoC **Rockchip RK3566**
- CPU：4× Cortex-A55 @1.8GHz（最高）/408MHz（最低）
- 内存：2GB（无板载 swap，已用 zram 缓解）
- NPU：RKNN（~1 TOPS），Linux 端待阶段 3 探测 /dev/rknpu*
- 网络：板载 **bcm43438 WiFi**（SDIO，mmc2），可正常联网
- 系统：Ubuntu 22.04.5 定制镜像（Rockchip/立创），**内核 6.1.141 为 2026-09-04 自编译**
- 启动链：U-Boot(09/04/2026) / ATF bl31-v1.46 / OP-TEE bl32-v2.16 / ddr-v1.25
- eMMC 14.6G：`/`=12G(剩~6.3G)、`/userdata`=2.2G、`/oem`、Rockchip Android 风格分区

## 已完成的系统配置（勿重复操作）
1. **zram 1G 交换已启用并开机自启**
   - systemd 服务 `/etc/systemd/system/zram-swap.service`（zstd，1G），enabled
   - 手动命令：`sudo zramctl -a zstd -s 1024M /dev/zram0 && sudo mkswap /dev/zram0 && sudo swapon -p 100 /dev/zram0`
2. **TF 卡已格式化并开机自挂**
   - /dev/mmcblk1p1，ext4，标签 lckfb_data，UUID=465007ea-1b9d-4554-9f94-5c8903745f05
   - 挂载点 `/mnt/tfcard`，fstab 已加 `UUID=... /mnt/tfcard ext4 defaults,noatime,nofail 0 2`
   - 实测：写 ~50MB/s，读 ~57MB/s
   - 目录：models/ datasets/ workspace/ backup/ Downloads/
3. **~/Downloads 已软链** → `/mnt/tfcard/Downloads`
4. **git 已安装**（2026-09-09，系统原未装）

## 已知小问题 / 备忘
- WiFi 缺 `clm_bcm43438a1.blob`（dmesg 报错，**非致命**，WiFi 正常）。修复需从板厂 SDK 获取合法文件，放 `/usr/lib/firmware/`
- eMMC 空间大头：/usr/lib/aarch64-linux-gnu 1.7G、.vscode-server 1.2G(可考虑迁移到 TF 省空间)、/var/lib 656M
- 内核自编译环境在另一台 x86 主机（lihaihui-virtual-machine）完成交叉编译

## 用户画像与项目决策
- 用户：智能电表 MCU 固件工程师（刚入职），MCU 经验为主
- 学习目标：不浪费 MCU 经验 → 嵌入式 Linux + 边缘 AI；锻炼 AI 辅助开发
- 时间：每天 1-1.5h + 大小周半天
- **主项目（定稿 2026-09-09）**：ESP32(已有) + RK3566 + NPU 视觉检测 = 边缘 AI 双芯感知节点
- 框架默认 ESP-IDF；视觉任务默认 YOLO→RKNN
- 详见 docs/PLAN.md

## 协作方式
- 全程 GitHub Copilot Agent 模式辅助；大文件/模型放 /mnt/tfcard
- 会话里程碑：2026-09-06 设备分析/zram/TF卡配置；2026-09-09 立项+建仓；2026-09-10 规格化+技能库建立
- **2026-09-10 起采用「教练模式」**：每个学习单元走 `讲原理 → 定目标 → 给思路 → 用户动手 → Copilot review` 五步。
  详见 [`COLLABORATION.md`](COLLABORATION.md)。Copilot 不得跳过原理直接给完整实现。

## 2026-09-10 会话记录
**做了什么**
1. 项目具体化：确定应用场景为「**区域智能安防监测**」，输出 [`SPEC.md`](SPEC.md)
   - 功能需求 FR（ESP32 8 条 / RK3566 11 条 / 协议 4 条，含 P0/P1/P2 优先级）
   - 量化指标：端到端延迟 < 800ms、≥8 FPS、召回率 ≥ 90%、24h 长稳
   - UART 帧协议 v1 定义（SOF/LEN/CMD/SEQ/PAYLOAD/CRC16-CCITT，8 条命令）
   - BOM（待购外设约 120 元）、风险登记册、待确认事项 6 条
2. 建立协作原则 [`COLLABORATION.md`](COLLABORATION.md)（教练模式五步法 + 提问模板 + 禁止事项）
3. 建立项目技能库 `.github/skills/edge-ai-node/`
   - `SKILL.md`：工作流程、技术栈速查、工程约定、排错流程、常用命令
   - `references/hardware.md`：板卡事实 + **8 条待确认硬件信息**
   - `references/environment.md`：环境配置 + **8 条待安装/待确认工具**
   - `references/knowledge-log.md`：15 条预置通用踩坑记录（串口/V4L2/RKNN/驱动/ESP32/工具链）
4. 更新 `README.md`、`PLAN.md` 索引与场景描述

**环境探测（当天完成）**

✅ **可用**
| 项 | 实测结果 |
|----|---------|
| UVC 驱动 | `CONFIG_USB_VIDEO_CLASS=y` 已内置，`uvcvideo` 已注册 → USB 摄像头**插上即用** |
| NPU | `CONFIG_ROCKCHIP_RKNPU=y`；驱动 **rknpu 0.9.8 (20240828)** 已初始化；平台设备 `fde40000.npu` |
| 内核头文件 | `/lib/modules/6.1.141/build` 存在 → **可以写内核模块** |
| 工具链 | gcc 11.4.0 / make / lsusb / v4l2-ctl 均已安装 |
| USB | 板载 **Type-A 为 USB 2.0**（USB 3.0 需从 39PIN 高速扩展接口引出，本项目不用）；磁盘 `/` 剩 4.6G，TF 卡剩 111G |

⚠️ **需处理 / 需注意**
| 项 | 发现 | 对应动作 |
|----|------|---------|
| 用户组 | `lihaihui` **不在 `video`/`render` 组** → `/dev/video*`、`/dev/dri/renderD*` 都 Permission denied | `sudo usermod -aG video,render lihaihui` + 重新登录 |
| video 编号 | **`/dev/video0`~`video9` 已被 rkisp 平台驱动占用**（`rkisp_mainpath` 等）；USB 摄像头将从 **video10** 起 | 代码里**绝不硬编码**设备节点 |
| NPU 节点 | **不是 `/dev/rknpu`**，而是 `/dev/dri/renderD128`（新版 rknpu2 用 DRM + `RKNPU_DRM_GEM`） | 阶段 3 排查时改查 `/dev/dri/renderD*` |
| NPU 警告 | dmesg 有 `can't request region for resource` + `IRQ npu_irq not found`，但驱动仍初始化成功 | 阶段 3 用 SDK demo 实测确认 |

> 📌 已将 K-016~K-021 六条计入 `knowledge-log.md`（本项目首批【实测】条目）。

**🎥 已有摄像头验证（2026-09-10 实测通过，无需采购）**

用户手头已有的 USB 摄像头型号 `b249:b081`（标识 `DHZJ-221209-X`）：

| 项 | 结果 |
|----|------|
| 是否 UVC | ✅ `bInterfaceClass=14 (Video)`，标准 UVC，`uvcvideo` 自动接管 |
| 设备节点 | **`/dev/video10`**（视频流）/ `video11`（metadata，无格式） |
| 挂载位置 | `usb3` 根 hub（ehci-platform）→ **板载 Type-A，USB 2.0** |
| 格式 | MJPG 640×480 / 1280×720 / 1600×1200 均 **30fps**；YUYV 720p 只剩 10fps |
| **稳态帧率** | **MJPG 640×480 @ 30.23 fps**（300 帧长测）✅ 远超指标要求（≥8 FPS） |
| 控制项 | 曝光/白平衡/伽马/背光补偿齐全，**支持自动对焦** |
| 取电 | `bMaxPower = 500mA`（USB 2.0 单口上限） |

> ✅ 结论：**摄像头不买了**。规格富余（摄像头 30fps → 推理目标 8fps，余量近 4 倍）。
> 💡 启发：「Windows 免驱」不能作为 Linux 可用性依据，判据是 USB 描述符的 UVC 类（K-021）。

**📝 勘误（用户纠正）**
初版环境探测报告中称「板载有 USB 3.0」，**这是错的**。
- 错误来源：从 `/sys/bus/usb/devices/usb2/speed=5000` 反推物理接口
- 事实：**板载 Type-A 为 USB 2.0**；USB 3.0 需从 **39PIN 高速信号扩展接口**引出
- 影响：摄像头选型时 **MJPEG 支持从「建议」升级为「必需」**（720p YUYV 需 55MB/s，远超 USB 2.0 带宽）
- 已修正：`hardware.md` §2.4、`SPEC.md` §4.2/§4.3/§10/§11、`knowledge-log.md` K-019

**权限修复（✅ 已完成并验证）**
```
groups = lihaihui, sudo, video(44), render(111)
免 sudo 访问 /dev/video10                ✅
免 sudo 读写 /dev/dri/renderD128         ✅
```

---

## 🔖 本日收尾（2026-09-10 结束状态）

**阶段 0 进度：0.1 环境体检 —— 已完成约 80%**

| 小节 | 状态 |
|------|------|
| 0.1 环境体检与权限 | 🟡 权限已修完；**还差「讲清 Linux 权限模型原理」这一步**（教学部分未做） |
| 0.2 git 工作流 | ⬜ 未开始 |
| 0.3 交叉编译 | ⬜ 未开始 |
| 0.4 Makefile 与构建 | ⬜ 未开始 |
| 0.5 终端/vim | ⬜ 未开始 |
| 0.6 项目骨架与首次推送 | ⬜ 未开始 |

**今天实际产出**
- 项目规格化：`SPEC.md`（FR 清单 / 量化指标 / 协议 v1 / BOM / 风险登记册）
- 协作原则：`COLLABORATION.md`（教练模式五步法）
- 项目技能库：`.github/skills/edge-ai-node/`（SKILL.md + 3 个 references）
- 阶段 0 学习地图：`stage0-toolchain/LEARNING-MAP.md`
- 环境实测：uvcvideo ✅ / RKNPU 0.9.8 ✅ / 内核头文件 ✅ / 工具链 ✅ / 权限 ✅
- 摄像头验证通过（`b249:b081`，MJPG 640×480@30fps 稳态）
- 经验库新增 6 条【实测】条目（K-016 ~ K-021）
- `.gitignore` 补充测试图片忽略规则

**⏭️ 明天的起点**

> 从 [`LEARNING-MAP.md`](../stage0-toolchain/LEARNING-MAP.md) 的 **0.1 收尾** 开始：
> Copilot 先补讲 **Linux 权限模型原理**（设备节点属主/属组/权限位、为什么 MCU 没有这一层、
> `usermod` 为什么必须重新登录才生效），再进入 **0.2 git 工作流**。

**📌 另需处理（不阻塞学习）**
- 下单：SG90 **180°** 舵机 ×2 + 二轴云台支架 + 杜邦线 + 独立 5V 电源（~50 元）
- 摄像头已确认可用，**无需采购**

> ⚠️ 下次会话开始时，Copilot 应先读本文件 + `SKILL.md`，直接接上 0.1 收尾，不要重复已完成的探测。

---

## 2026-09-12 · 0.1 收尾（Linux 权限模型）

**已讲完并理解**
- 权限点：属主/属组/权限位三段结构；**检查顺序「命中即停」不叠加**
- MCU 对比：MCU 无内核态/用户态隔离，`能跑代码 = 有权限`；Linux 里两件事分开
- `usermod` 必须重新登录：改的是 `/etc/group` **文件**，改不了已运行进程内核里的 `struct cred`
- 为何不用 sudo 做日常开发：产物属主变 root，越用越乱
- 主次设备号：**主设备号 → 驱动（1 个）**；**次设备号 → 该驱动下的设备实例**
- 设备文件名数字 ≠ 次设备号（`video10` 的次号是 13）
- 串口次设备号 = 64 + 端口号（`ttyS1`=65、`ttyS3`=67）

**任务 1 结果**：字段解读全部正确 ✅；仅「次设备号 = 第 N 个驱动」一处概念错（已纠正，K-022）

**新实测发现（重要）**
- 板上 **10 个 UART 控制器只启用 2 个**：`ttyS1`(fe650000)、`ttyS3`(fe670000)
- **`/dev/ttyS0`/`ttyS2` 不存在**（控制器 status=disabled）
- console 是 **`ttyFIQ0`**，不占用 ttyS1/ttyS3
- ⚠️ 串口属组是 **`dialout`**（不是 `tty`）→ 需 `sudo usermod -aG dialout lihaihui`
- `ttyS1` 有 `failed to request DMA, use interrupt mode` 警告
- ⬜ 待查：哪个 UART 引到排针？蓝牙 hci0 是否占用 ttyS1？

**进度**

| 小节 | 状态 |
|------|------|
| 0.1 环境体检与权限 | ✅ **完成**（权限已配 + 原理已讲） |
| 0.2 git 工作流 | ⬜ **下一个** |
| 0.3 ~ 0.6 | ⬜ 未开始 |

**⏭️ 下次起点**
- 用户执行 `sudo usermod -aG dialout lihaihui` + 重新登录（阶段 1.3 前完成即可，不阻塞 0.2）
- 开始 **0.2 git 工作流**（0.2.1 三层模型：工作区 / 暂存区 / 版本库）
- 用户尚欠：**任务 3**（`chmod` 权限实验）——可穿插或跳过，非关键路径

**经验库累计**：8 条【实测】+ 15 条【通用】

---

## 2026-09-14 · 0.1 结课 + 0.2.1 / 0.2.2（git 三层模型与数据模型）

### 0.1 结课（任务 3 补完）

**三步实验验证「命中即停」**（文件固定 `chmod 004`）：

| 步骤 | 属主 | 属组 | 你的身份 | 结果 |
|------|------|------|---------|------|
| ① | `lihaihui` | sudo | 属主 | ❌ |
| ② | `lckfb` | sudo | **属组（在 sudo 组）** | **❌** |
| ③ | `lckfb` | `lckfb` | other | ✅ |

- 用户初次**漏跑第 ② 格**（属主不匹配 + 属组匹配 + other 可读 → 仍拒绝），补跑后确认：
  **属组段命中即停，不会去看 other 段**；反直觉结论：`004` 让「外人」成了唯一可读者
- 一处措辞纠正：内核比对的是 **UID/GID 数字**，不是用户名（`ls -l` 显示的名字只是翻译）

**顺带撞出新坑 K-024：`sudo` 也写不进 `/tmp` 里别人的文件**
- 现象：`sudo sh -c 'echo SECRET > /tmp/perm-lab.txt'` → `Permission denied`（**root + 666 全开仍被拒**）
- 根因：`/tmp` 的 **sticky bit** + `fs.protected_regular=2`（Ubuntu 默认）
- 实测三组对照：唯一决定因素是「**文件属主 == 写入者**」，与权限位无关；
  该检查**不走权限位、不受 `CAP_DAC_OVERRIDE` 豁免**（所以 root 也拦）
- 设计意图：防「共享目录投毒」（攻击者预置同名文件诱使 root 写入）
- **认知升级：权限模型有 4 层** —— DAC 九位 → sticky bit → capabilities → LSM / `protected_*`
- 学习笔记落盘：`stage0-toolchain/docs/0.1-permission-notes.md`

### 0.2.1 三层模型（工作区 / 暂存区 / 版本库）

- 检验题**全对**，含反直觉题：commit 后 `cat` 仍是工作区的最新内容（**commit 不碰工作区**）
- 关键结论：
  - 暂存区存的是**内容快照**，不是文件指针 → 长格式 `status` 里同一文件出现在两段；短格式 `MM`
  - `git commit` = 「暂存区 → 版本库」；想看历史里的版本用 `git show HEAD:file`
  - 新增习惯：**commit 前先 `git diff --staged` 验收**（最后一道防线）
- 顺手解决：`git config --global user.name/email` 未配置（**提交身份 ≠ 推送密钥**，两者无关）

### 0.2.2 数据模型（blob / tree / commit）

- **git 存快照，不存 diff**；靠「内容寻址 + 相同内容只存一份」避免膨胀
- 亲手验证：`x.txt` / `y.txt` 内容相同 → `git rev-parse` 得到**同一个 blob 哈希**（`bf7c6b26`）
- `git diff` 是**现算**的：tree 里没有任何 diff 痕迹；且 diff 输出的 `index` 行暴露它读的是 blob
- git 只记录权限中的**可执行位**（644/755），**不记录属主/属组**（那属于部署环境，不属代码）
- 小错一处（已纠正）：tree 四列含义错位 —— `100644` 是**模式位**（`rw-r--r--`），不是"内容对象"

### 首次实战提交（2 个 commit）

```
e5f1763  docs(stage0): 新增 0.1 权限模型学习笔记与会话记录        2 files, +181
dc3a5b1  docs(skill): 沉淀串口/UART 实测事实与 K-022~K-024 经验   3 files, +120
```

- 分组标准通过：按「同一件事」拆（知识库沉淀 / 学习记录），**没有用 `git add .`**
- message 格式规范，工作区验收干净

**⚠️ 认知补齐：commit ≠ 备份** —— 本地库「完整」不等于「安全」，
真正的安全是**同一份历史存在两个物理位置**。异地备份待 0.2.4 / 0.6（推 GitHub）。

### 进度

| 小节 | 状态 |
|------|------|
| 0.1 环境体检与权限 | ✅ **结课**（原理 + 实验 + 笔记落盘） |
| 0.2.1 三层模型 | ✅ 完成 |
| 0.2.2 命令与数据模型 | ✅ 完成 |
| 0.2.3 分支 | ⬜ **下一个** |
| 0.2.4 远程 / 0.2.5 撤销 / 0.3~0.6 | ⬜ 未开始 |

### ⏭️ 下次起点

1. **0.2.3 分支**：本质 = **指向 commit 的指针**（与 0.2.2 的对象模型天然衔接，约 30 min）
2. 待办未清：哪个 UART 引到排针？蓝牙 `hci0` 是否占用 `ttyS1`？（阶段 1.3 前完成）
3. 待下单：SG90 **180°** 舵机 ×2 + 二轴云台支架 + 杜邦线 + 独立 5V 电源（~50 元）

**经验库累计**：9 条【实测】+ 15 条【通用】
**练习仓库**：`~/git-lab`（可保留，后续 git 实验都在这里做，不污染项目仓库）
