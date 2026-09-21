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

---

## 2026-09-16 · 0.2.3 分支 + 0.2.4 远程

### 0.2.3 分支（练习仓库 `~/git-lab`）

**核心认知：分支 = 一个 41 字节的文件**

```
HEAD (内容: "ref: refs/heads/master")  →  refs/heads/master (内容: 一个哈希)  →  commit 对象
```

- `HEAD` 存的是**分支名**；分支文件存的是 **commit 哈希**
- **提交时被改写的是「当前分支文件」，HEAD 不动**
- 创建分支 = 复制一个 41 字节文件（对比 SVN 复制整棵目录树）→ 这才是"零成本"的字面含义

**分叉与合并（亲手跑出两种结果）**

| 场景 | 结果 | 历史形状 |
|------|------|---------|
| master 未动 | **Fast-forward**：只挪指针，不产生新 commit | 一条直线 |
| 两边各有新提交 | **三方合并**：产生一个有两个 parent 的新 commit | 分叉再汇合 |

实测图形：

```
*   b8b1aa5 (HEAD -> master) Merge branch 'feature-a'
|\
| * 190a310 (feature-a) feat: 分支继续前进
* | ddad9ec feat: master 上的改动
|/
* 791399e feat: 分支上的改动
```

- 概念纠正（用户初答）：合并产生的是**新 commit**，不是"新分支"；FF 是挪**分支指针**，不是挪 HEAD

**detached HEAD（拿到铁证）**

- `git switch <commit>` **被拒绝**：`fatal: a branch is expected, got commit`
  → git 自己在报错里区分 branch / commit
- 正确姿势：`git switch --detach <commit>`（**必须显式声明意图才放行**，这是 `switch` 替代 `checkout` 的安全动机）
- 铁证：`cat .git/HEAD` 从 `ref: refs/heads/master` 变成**裸哈希**
- 其他表现：`git status` 首行变 `HEAD detached at ...`；`git log` 标签从 `(HEAD -> master)` 变 `(HEAD)`——**箭头消失**
- 风险：此状态下的提交不属于任何分支，切走后成孤儿（只能靠 `reflog` 捞）
- 现代实践：`checkout` 已拆为 `switch`（切分支）/ `restore`（丢改动）

**⚠️ 重要观察：工作区的改动「不属于任何分支」**

- 实验中反复出现 `M a.txt` 提示——未提交的改动**跟着分支切换来回走**（走了 5 趟以上）
- git 只在"改动会被覆盖"时才拒绝：`error: Your local changes ... would be overwritten`
- 所以「切分支前先 `git status`」的真正意义：**别让手上的草稿被提交到错误的分支上**

### 0.2.4 远程（本地伪远程实验，不需要网络与账号）

**核心认知：远程只是「一个名字 + 一个 URL」，git 不关心它是 GitHub 还是本地目录**

实验拓扑：

```
~/git-lab（开发者 A）──push──► ~/git-remote.git（裸仓库）
                                    ▲
~/git-other（B，由 clone 而来）──push──┘
```

- **裸仓库（`git init --bare`）没有工作区**——只用于被 push/pull，不给人编辑
- `origin` 只是 `clone` 自动起的**默认名字**，不是关键字
- `-u`（`--set-upstream`）建立追踪关系，之后 `git push` / `git pull` 无需带参数

**🔑 本节最重要的认知：`origin/master` 是本地文件，是缓存**

实测证据（`git status` 前后"矛盾"）：

```
push 成功后：  Your branch is up to date with 'origin/master'.    ← 但远程早已是新 commit
git fetch 后： Your branch is behind 'origin/master' by 1 commit  ← 这时才同步下来
```

- 文件位置：`.git/refs/remotes/origin/master`
- 含义：「**上次与远程通信时**，远程 master 指向哪个 commit」→ **可能过时，不是实时状态**

**fetch vs pull（用输出箭头一眼分辨）**

```
git fetch  →   b8b1aa5..dc29320  master -> origin/master    ← 只更新本地缓存，不碰工作区
git pull   →   Updating b8b1aa5..dc29320  Fast-forward      ← 额外把改动展开到工作区
```

- `fetch` 后 `git log --oneline` **看不到**远程新提交（不在当前分支链上）；`git log --all` 能看到
- `fetch` 后工作区**没有**新文件；`pull` 之后才有 → **fetch 动的是数据库，pull 额外动了你的文件**

### 进度

| 小节 | 状态 |
|------|------|
| 0.1 环境体检与权限 | ✅ 结课 |
| 0.2.1 三层模型 | ✅ |
| 0.2.2 命令与数据模型 | ✅ |
| 0.2.3 分支 | ✅ |
| 0.2.4 远程 | ✅ |
| 0.2.5 撤销 | ⬜ **下一个** |
| 0.6 首次推送 GitHub | ⬜ |

### ⏭️ 下次起点

1. **0.2.5 撤销**：`restore` / `reset` / `revert` 的区别（`reset --hard` 是 git 里最容易丢代码的命令）
   - 素材现成：`~/git-lab` 里正堆着「未提交改动 + 多分支 + 远程」的活场景
2. **0.6 推 GitHub 前需确认**：GitHub 账号 + 认证方式（HTTPS token 或 SSH key）——这才是"密钥"真正登场的时机
3. 待办未清：哪个 UART 引到排针？蓝牙 `hci0` 是否占用 `ttyS1`？（阶段 1.3 前）
4. 待下单：SG90 **180°** 舵机 ×2 + 二轴云台支架 + 杜邦线 + 独立 5V 电源（~50 元）

**练习仓库**：`~/git-lab`（开发 A）、`~/git-other`（模拟另一台电脑）、`~/git-remote.git`（裸仓库当远程）

**经验库累计**：10 条【实测】+ 15 条【通用】

---

## 2026-09-18 · 0.2.5 撤销（**0.2 git 工作流全部完结 ✅**）

### 四个区 ↔ 四组命令

| 想撤销什么 | 命令 | 可恢复性 |
|-----------|------|---------|
| 工作区改动（未 add） | `git restore <file>` | 💀 **永久消失** |
| 暂存区登记（已 add） | `git restore --staged <file>` | 🟢 安全（工作区完全不动） |
| 本地 commit（未推送） | `git reset [--soft\|--mixed\|--hard] HEAD~n` | 见下表 |
| 已推送的 commit | `git revert <commit>` | 🟢 安全（不改历史） |

### `reset` 三兄弟

| 模式 | 分支指针 | 暂存区 | 工作区 | **改动去了哪** |
|------|---------|-------|-------|---------------|
| `--soft` | 后移 | 不动 | 不动 | 全部回到**暂存区** |
| `--mixed`（默认） | 后移 | 重置 | 不动 | 全部回到**工作区** |
| `--hard` | 后移 | 重置 | 重置 | 💀 **消失** |

实测状态变化：`--soft` → `MM` ｜ `--mixed` → ` M` ｜ `--hard` → 空

⚠️ 措辞要点：是**分支指针**后移，不是"HEAD 移动"（HEAD 始终指向分支名，`(HEAD -> master)` 即是证据）

### `reset` vs `revert`：分水岭是「推送了没有」

- **没推** → `reset`（改写历史，干净利落）
- **推了** → **必须 `revert`**（不删任何东西，只追加一个反向 commit）

实测 `revert` 后：

```
8d65a0b Revert "test: 临时提交"     ← 新增的反向 commit
613fe88 test: 临时提交              ← ★ 原 commit 依然存在
```

git 自动写入 `This reverts commit 613fe88...` → **可追溯（审计价值）**

> **黄金法则：不要 reset / 强推已经共享出去的 commit。**
> 违反后果不是自己出问题，而是**所有拉过这段历史的人一起出问题**。

### 🔑 本节最重要的纠正：只有「曾被 commit 引用」的内容才能恢复

用户初答「暂存区有记录就能找回」→ **错**。实验一锤定音：

| 内容 | 经历 | 能否救回 |
|------|------|---------|
| `AAA` | add → **commit** | ✅ 被 commit 的 tree 引用，reflog 可定位 |
| `CCC` | add → **commit** | ✅ 同上 |
| `DDD` | 只 `echo >>`，从未 add | ❌ 对象都不存在，无从救起 |

- `git reflog` 记录的是 **commit 指针的移动**，所以只能救"曾经是 commit 的东西"
- `git reset --hard HEAD@{2}` 实测成功找回 `613fe88`
- **`git restore <file>` 丢弃的改动永久消失**

> 💡 自保策略：**舍不得的改动，先 `git commit`（不是先 `git add`）。**

### 其他纠正与小插曲

- 用户初答「revert 需要 reflog 救」→ **反了**：revert 最安全，什么都不删
- `git stash` / `git stash pop` 跑通（临时收起草稿，pop 后自动 drop）
- 用户两次把 `restore --staged` 的机制说成"移动 HEAD"（实为撤回暂存登记）
- 小插曲：`git staus` 拼错
- 练习仓库状态：本地 master 领先 `origin/master` 2 个 commit（`613fe88` + `8d65a0b`）

### 进度

| 小节 | 状态 |
|------|------|
| 0.1 环境体检与权限 | ✅ 结课 |
| 0.2.1 三层模型 | ✅ |
| 0.2.2 命令与数据模型 | ✅ |
| 0.2.3 分支 | ✅ |
| 0.2.4 远程 | ✅ |
| **0.2.5 撤销** | ✅ **（0.2 全部完结）** |
| 0.6 项目骨架与首次推送 GitHub | ⬜ **下一个** |
| 0.3 交叉编译 / 0.4 Makefile / 0.5 终端 vim | ⬜ |

### ⏭️ 下次起点

1. **0.6 首次推送到 GitHub** —— 补上「commit ≠ 备份」的缺口
   - **前置**：需要 GitHub 账号（建议起一个能长期用、可放简历的用户名）
   - 认证二选一：HTTPS + Personal Access Token ／ SSH key（密钥终于登场）
2. 待办未清：哪个 UART 引到排针？蓝牙 `hci0` 是否占用 `ttyS1`？（阶段 1.3 前）
3. 待下单：SG90 **180°** 舵机 ×2 + 二轴云台支架 + 杜邦线 + 独立 5V 电源（~50 元）

**练习仓库**：`~/git-lab`（开发 A）、`~/git-other`（模拟另一台电脑）、`~/git-remote.git`（裸仓库当远程）
**学习笔记**：`stage0-toolchain/docs/0.2-git-notes.md`（0.2 全节复习卡）
**经验库累计**：11 条【实测】+ 15 条【通用】

---

## 2026-09-19 · 0.6 推送 + 0.4 Makefile + 0.3 交叉编译 + 0.5 vim
### 🎉 阶段 0 全部完成

---

### 0.6 首次推送到 GitHub ✅

- `ssh-keygen -t ed25519` 生成密钥对；私钥 `600`、公钥 `644`（私钥权限被 SSH 严格检查）
- 公钥上传 GitHub → `ssh -T git@github.com` 认证成功
  - 首次连接的 host key 确认是 **TOFU 机制**：验证的是「**服务器是不是真的 GitHub**」（防中间人），不是"你是谁"
- `git branch -M main` → `git remote add origin git@github.com:lihaihuilihaihui/edge-ai-dual-node.git`
- `git push -u origin main` → **79 objects / 76.22 KiB 成功**
- ✅ **「commit ≠ 备份」的缺口正式关闭**（本地 + GitHub 两份物理副本）

**讲清的机制**：
- 建仓不勾 `Add a README` 的原因：远程若有 commit，两条历史**无共同祖先** → push 被拒
- **push 的本质是"把远程指针往前挪"，不能抹掉远程已有的 commit** → 所以要求 fast-forward
- 踩坑小记：粘贴占位符时把 `<` `>` `!` 一起带进命令 → 尖括号是占位符标记，`!` 在 bash 里是历史扩展

### 0.4 Makefile 与构建 ✅

用户**独立写出生产级 Makefile**（`stage0-toolchain/practice/make-lab/`）：

```makefile
CC      = gcc
CFLAGS  = -Wall -g -MMD -MP
TARGET  = app
OBJS    = main.o a.o b.o
DEPS    = $(OBJS:.o=.d)

all: $(TARGET)
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
.PHONY: clean
clean:
	rm -f $(TARGET) $(OBJS) *.d
-include $(DEPS)
```

**实测数据（关键验收）**：

| 操作 | 结果 | 说明 |
|------|------|------|
| `make` 首次 | 3 编译 + 1 链接 | |
| `make` 再次 | `Nothing to be done` | 时间戳判断生效 |
| `touch a.c && make` | **只重编 a.o + 链接（共 2 条）** | ★ 核心验收 |
| `touch a.h && make`（修复前） | `Nothing to be done` | ⚠️ 头文件盲区实证 |
| `touch a.h && make`（修复后） | **重编 main.o + a.o** | ★ 传递依赖被捕捉 |

**重大发现：`main.d` 暴露了传递依赖**
```
main.o: main.c a.h b.h        ← main.c 也 include 了 a.h！
```
- 手工写依赖极易漏掉 `main.o: a.h` → 改头文件不重编 → **编译通过但行为诡异**
- **`-MMD` 让编译器（真正读 `#include` 的那个程序）来回答依赖问题**，从根上杜绝遗漏
- **精确性验证**：`touch a.h` 只重编 main.o+a.o（**b.o 不动**）；`touch b.h` 只重编 main.o+b.o（**a.o 不动**）
- **自举机制**：首次编译时 `.d` 不存在（`-include` 静默跳过），编译"顺带"产出 `.d`，第二次起生效

**纠错**：用户初版 `%.o: %.c` 只写 `-c $<`（缺 `-o $@`）——当前项目恰好能跑（gcc 默认命名），但换成 `build/%.o: src/%.c` 立刻失败。已指出。

### 0.3 交叉编译 ✅

**认知锚点（用户自己问出来的）**：
> 「我之前用 Keil 编译开发板代码时，是软件默认的交叉编译吗？」

**是的**——`arm-none-eabi-gcc` 就是交叉编译器。Keil 把「工具链 + sysroot + 链接脚本」全自动配好了；Linux 下这些要自己管。**错误类型也一一对应**（`undefined symbol` ↔ `undefined reference`）。

**环境实测**：

| 项 | 结果 |
|----|------|
| 虚拟机架构 | `x86_64` |
| 交叉工具链 | `aarch64-linux-gnu-gcc` **11.4.0**（与板卡 gcc 版本一致 ✅，无 glibc 版本风险） |
| 网络 | 板卡 `192.168.31.151`（LAN）／虚拟机 `192.168.27.128`（VMware NAT） |
| 传输通路 | ✅ **虚拟机 → `scp` → 板卡** 已实测可通 |
| 虚拟机额外能力 | 装了 **QEMU user-mode + binfmt_misc**，直接执行 aarch64 程序会尝试 QEMU 模拟 |
| ⚠️ 板卡缺 | **`file`**（已 `apt install -y file`） |

**实操链路（全部打通）**：
```
x86 上 aarch64-linux-gnu-gcc 编译 → scp 到板卡 → ./hello-arm64 运行成功
输出：Hello from aarch64 (ARMv8 64-bit)!
```

**核心知识点**：

1. **sysroot ≠ 运行时库**：
   - `/usr/aarch64-linux-gnu/lib/` → **编译链接时**用（交叉链接器找 `-lc`）
   - `/lib/` → **运行时**用（内核/动态链接器加载）
   - 实测：虚拟机 `/lib/ld-linux-aarch64.so.1` **不存在** → 报 `aarch64-binfmt-P: Could not open ...`；板卡上它就是本机链接器 → 运行成功
2. **架构由工具链决定，不由源码决定**（用户初答"条件编译决定架构"→ 因果反了）
   - 因果链：`用哪条工具链 → 编译器预定义哪个宏 → 源码走哪个分支`
3. `file` 输出关键两处：**架构名** + **interpreter 路径**（ELF 头里写死的动态链接器）
4. **`ldd` 不能用于跨架构诊断**（本质是脚本，会尝试执行目标程序）→ 用 `aarch64-linux-gnu-readelf -d`

**两个意外收获**：
- 用户 `hello.c` 把 `__aarch64__` 拼成 `__arrch64__` → **在 x86 上"看起来正常"（第二分支接住），到板子上才暴露** → 活教材：有些 bug 只有目标平台能暴露
- 解析工具输出必须加 `LC_ALL=C`（中文 locale 会把 `readelf` 的 `Class/Machine/Type` 翻译掉，grep 全空）→ **K-027**

### 0.5 vim 生存技能 ✅

- 掌握：模式切换（`i`/`a`/`o` ↔ `Esc`）、`dd`、`yy`+`p`、`/搜索`、`:wq`、**`:q!` 救命**
- **心法**：vim 是 `[动词][数量][名词]` 的语法（`d3w`/`3dd`），不是命令清单
- **救命三步**：`Esc`（按几次）→ `:q!` → 回车
- 第 7 关（救命练习）通过：乱改后 `:q!` 干净逃出，文件内容未受影响 ✅
- VS Code 分屏 + `tail -f` 实测通过（**tmux 暂不学**——阶段 3 长稳测试需要会话持久化时再说）
- `grep -rn` / `find` 实测通过
- ⬜ **遗留**：`:%s/line/row/g` 全文替换那一关没做，下次顺手补

### 进度

| 小节 | 状态 |
|------|------|
| 0.1 环境体检与权限 | ✅ |
| 0.2 git 工作流（0.2.1~0.2.5） | ✅ |
| 0.3 交叉编译 | ✅ |
| 0.4 Makefile 与构建 | ✅ |
| 0.5 终端与 vim | ✅（`:%s///g` 待补） |
| 0.6 项目骨架与首次推送 | ✅ |
| **阶段 0** | 🎉 **全部完成** |

### ⏭️ 下次起点：**阶段 1（RK3566 Linux 底层）**

按 [`COLLABORATION.md`](../docs/COLLABORATION.md) §3，进入新阶段前 Copilot 必须先产出：
1. 阶段 1 **学习地图**（分几节、先后依赖）
2. **原理总览**（架构图 + 关键概念通俗解释）
3. **验收清单**（做完能演示什么）
4. **预计时间**（按每天 1-1.5h 折算）

等用户回复"确认，开始"后，才进入 1.1（板载 GPIO/LED/按键：libgpiod）。

**待办未清**：
- ⬜ `:%s///g` 练习（0.5 遗留）
- ⬜ 哪个 UART 引到排针？蓝牙 `hci0` 是否占用 `ttyS1`？
- ⬜ 下单：SG90 **180°** 舵机 ×2 + 二轴云台支架 + 杜邦线 + 独立 5V 电源（阶段 2 前到货即可）

**经验库累计**：13 条【实测】+ 15 条【通用】

---

## 2026-09-20 · 阶段 1 开场：学习地图 + 1.0 环境探测

### 产出：阶段 1 学习地图（`stage1-rk3566/LEARNING-MAP.md`）

按 `COLLABORATION.md` §3 产出四项（地图 / 原理总览 / 验收清单 / 预计时间），**用户已确认**。

**处理了一处文档不一致**：`PLAN.md` 与 `SPEC.md` §7 对阶段 1 的划分不同，取并集重排为 6 节：

| # | 小节 | 交付物 | 预计 |
|---|------|--------|------|
| 1.0 | 环境探测收尾 | 硬件清单 | 30 min |
| 1.1 | GPIO 与 LED（libgpiod + sysfs + udev） | LED 闪烁 + udev 规则 | 2 h |
| 1.2 | **字符设备驱动**（内核模块 + ioctl/read） | `edge_ai_evt.ko` | 6 h |
| 1.3 | 摄像头 V4L2 | `camera.cpp` 存 JPEG | 4 h |
| 1.4 | 串口与协议 | `serial.cpp` + `protocol.cpp`（自环 1000 帧 0 误码） | 5 h |
| 1.5 | 应用工程化（多线程 + 日志 + Makefile） | 多线程骨架 | 6 h |

**关键决策：1.2 的驱动不碰设备树**
- 理由：规避「改设备树 → 重编内核 → 刷 boot 分区」的高风险链路
- 教学用事件队列驱动用 `register_chrdev` + `class_create` 即可，且**可在板卡上原生编译**（`/lib/modules/6.1.141/build` 已确认存在）

### 解答用户两个关键疑问

**① 重编内核 + 烧录，板卡配置会不会丢？**

实测分区布局（Rockchip 风格）：
```
mmcblk0: p1(4M uboot) p2(4M) p3(64M boot) p4(128M recovery) p5(32M)
         p6(12G /) p7(128M /oem) p8(2.2G /userdata)
```
- **内核在 p3（boot 分区），所有配置在 p6（根文件系统）** → **只刷 boot 分区不会丢配置** ✅
- 真正的风险不是"配置丢"，而是"**新内核起不来**"→ 必须先备份 boot 分区（`sudo dd if=/dev/mmcblk0p3 of=...`）
- 结论：**优先用 `.ko` 内核模块**（零风险）；确需改内核时先备份

**② 服务器版无桌面，摄像头画面怎么看？**
- 核心认知：**本项目根本不需要"显示画面"**——系统输出是"有没有人"+ 执行动作，画面只是调试手段
- 方案分层：① 存 JPEG + **VS Code 直接预览**（阶段 1.3 够用）② MJPEG over HTTP 推流 → Windows 浏览器 ③ 报警快照落盘（FR-27）④ Web 控制台（P2 加分项）
- 明确**不装桌面环境**：省内存（板子仅 2GB，要留给 NPU 推理）

### 1.0 环境探测收尾 ✅（09-12 遗留问题全部闭环）

**① UART 分配彻底查清**（`pinmux-pins` + 设备树 status 双向验证）

| 控制器 | 地址 | 设备节点 | 占用引脚 | 用途 |
|--------|------|---------|---------|------|
| `uart1` | `fe650000` | `/dev/ttyS1` | `gpio2-11/12/13/14` | **蓝牙 hci0** ❌ 不可用 |
| `uart2` | `fe660000` | — | `gpio0-24/25` | **console（`fiq-debugger`）** ❌ 绝对不可碰 |
| `uart3` | `fe670000` | **`/dev/ttyS3`** | `gpio3-15/16` | ★ **排针 8(TX)/10(RX)** |

- ✅ **哪个 UART 引到排针** → **`ttyS3`**
- ✅ **蓝牙是否占用 ttyS1** → **是**（`wireless-bluetooth` 独占 uart1）

**② 40PIN 排针引脚表**（用户提供板卡原理图 + 内核实测，**交叉验证完全吻合**）
- 完整表见 `references/hardware.md` §2.7
- 可用 GPIO 11 个（gpio3 bank 大部分空闲）
- 已占用：`i2c2`(排针 3/5)、`i2c3`(27/28)、`uart3`(8/10)、`pwm8/14/15`(12/32/33/35)、`spi3`(19/21/23/24)

**③ 选定实验引脚**：**排针 11 号 = `GPIO3_A1` = 全局 97 = `gpiochip3:1`**（实测 `MUX UNCLAIMED`）

**④ 探测中的两个方法论收获**（→ **K-029**）

1. **`gpioinfo` 的 `unused` ≠ 引脚空闲**
   - UART/I2C/PWM 等子系统**不通过 gpiod 接口申请引脚**，在 `gpioinfo` 里显示 `unused`，但引脚已被 pinctrl 复用占用
   - 判断引脚可用性**必须看** `sudo cat /sys/kernel/debug/pinctrl/*/pinmux-pins` 的 `UNCLAIMED`
   - 用户一度想选排针 8/10（uart3 专用且已被配成串口复用）→ 已纠正为 11
2. **`grep -c "used"` 会把 `unused` 也算进去** → 统计关键词必须加 `-w` 全词匹配
3. 顺带纠正我自己的失误：给出的 grep 模式 `gpio3-(1|2|3):` 与实际输出格式 `(gpio3-1):` 不符（**写模式前必须先看实际输出**）

### 进度

| 小节 | 状态 |
|------|------|
| 阶段 0 | ✅ 全部完成 |
| **阶段 1 学习地图** | ✅ 产出并确认 |
| **1.0 环境探测收尾** | ✅ **完成** |
| 1.1 GPIO 与 LED | ⬜ **下一个** |
| 1.2 ~ 1.5 | ⬜ |

### ⏭️ 下次起点：**1.1 GPIO 与 LED**（2 h）

按五步法走。已知前置：
- 目标引脚：**排针 11 = `GPIO3_A1` = `gpiochip3` line 1**
- **第一个任务：解决 `/dev/gpiochip*` 权限**（当前 `root:root 600`，且系统**无 `gpio` 组** → 必须写 **udev 规则**）
- 板载 RGB LED 可用：`/sys/class/leds/rgb-led-{r,g,b}/brightness`（`max_brightness=1`）

**待办未清**：
- ⬜ `:%s/line/row/g` 练习（0.5 遗留，30 秒）
- ⬜ 排针 UART 电平实测（**阶段 2 接线前必须做**）
- ⬜ 下单：SG90 180° 舵机 ×2 + 二轴云台 + 杜邦线 + 独立 5V 电源

**经验库累计**：14 条【实测】+ 15 条【通用】

---

## 2026-09-20（下半场）· 1.1 GPIO 与 LED：原理已讲

**Step 1 + Step 2 完成**（用户要求"先讲原理，实验下次做"）。

完整笔记落盘：`stage1-rk3566/docs/1.1-gpio-notes.md`

**原理要点**：

1. **Linux 点灯比 MCU 多三层**：用户态 → gpio 子系统 → pinctrl 子系统 → 硬件
   - 三个原因：**硬件隔离**（MMU 挡住物理地址）、**资源统一管理**（160 个引脚是全系统共享资源，必须仲裁）、**权限控制**
2. **`pinctrl` vs `gpio` 的分工**（易混点）：
   - `pinctrl` 管引脚的**身份**（GPIO / UART_TX / I2C_SCL —— 复用）
   - `gpio` 管 GPIO 的**行为**（方向 / 电平 / 中断）
   - ⚠️ **K-029 的根因就在这**：`gpioinfo` 只是 gpio 子系统的视角，引脚被"改身份"给 UART 后它就不知道了
3. **两套用户态接口**：
   - sysfs（`/sys/class/gpio/...`）——**Linux 4.8 起废弃**，原因是 **check-then-act 竞态**（检查空闲→占用的间隙可能被抢）
   - **字符设备 + libgpiod**（`/dev/gpiochipN` + `ioctl`）—— 有**所有权**、**原子操作**，标准做法
4. **LED 子系统**：内核已接管的硬件**不要再通过 GPIO 去抢**（会破坏驱动功能）
   - `mmc0::` 那个灯是 **trigger** 自动驱动的（跟随磁盘活动），不需要任何程序
5. **udev = 设备节点的"出生登记处"**：
   - `/dev/video*` 属组是 `video`、`/dev/ttyS*` 是 `dialout` —— **都是 udev 规则配出来的**
   - 解决 GPIO 权限需**两步配合**：① udev 规则设属组 ② 用户加入该组
   - 推荐做法：新建 `gpio` 组（系统里没有）+ 写 udev 规则

### 进度

| 小节 | 状态 |
|------|------|
| 阶段 0 | ✅ 全部完成 |
| 1.0 环境探测收尾 | ✅ 完成 |
| **1.1 GPIO 与 LED** | 🟡 **原理已讲，实验待做** |
| 1.2 ~ 1.5 | ⬜ 未开始 |

### ⏭️ 下次起点：**1.1 实验**（约 1.5 h）

四个动作：
1. 写 **udev 规则**（新建 `gpio` 组 + `/etc/udev/rules.d/99-gpio.rules`）→ 验证免 `sudo`
2. **sysfs** 点亮板载 RGB LED（有视觉反馈）
3. **libgpiod** 操作排针 11（`gpiochip3:1`）+ **回读验证**
4. 对比已废弃的 sysfs gpio

**待办未清**：
- ⬜ `:%s/line/row/g` 练习（0.5 遗留）
- ⬜ 排针 UART 电平实测（阶段 2 接线前必须做）
- ⬜ 下单：SG90 舵机 ×2 + 二轴云台 + 杜邦线 + 独立 5V 电源（**杜邦线会影响 1.1 实验 3 的验证方式**）

**经验库累计**：14 条【实测】+ 15 条【通用】

---

## 2026-09-21 · 1.1 GPIO 与 LED（实验）✅ 结课

### 实验全过程

**① udev 规则解决 `/dev/gpiochip*` 权限**

```
SUBSYSTEM=="gpio", KERNEL=="gpiochip*", GROUP="gpio", MODE="0660"
```

- **首次失败**：MODE 生效（`600 → 660`），但**属组仍是 `root`**
- **根因**：**`gpio` 组不存在** → udev **静默忽略** GROUP 参数
  - 证据：`systemd-udevd: /etc/udev/rules.d/99-gpiochip.rules:1 Unknown group 'gpio', ignoring`
- **解决**：`groupadd gpio` → `usermod -aG gpio lihaihui` → `udevadm control --reload-rules` → `udevadm trigger`
- 结果：`crw-rw---- 1 root gpio 254, 3 /dev/gpiochip3` ✅ 免 sudo 可用
- ⚠️ **`newgrp gpio` 只影响当前 shell**；VS Code 分屏新开的终端不继承（表现为 `gpioset: Permission denied`）→ 需重启 VS Code Server 才能永久生效
- → **K-030**

**② sysfs 点亮板载 RGB LED**

```bash
echo 1 | sudo tee /sys/class/leds/rgb-led-r/brightness
```

用户**答对关键问题**：为什么 LED 要 `sudo` 而 GPIO 不用 → **udev 只管 `/dev`，不管 `/sys`**（sysfs 权限由驱动创建时决定，无法用 udev 改）✅

**③ ★ trigger 机制**

```bash
echo heartbeat | sudo tee /sys/class/leds/rgb-led-r/trigger
```
**LED 自己按心跳闪，而没有任何用户态程序在运行** ✅

- `[方括号]` = **当前激活的触发器**
- 支持的触发器：`none / timer / heartbeat / mmc0 / cpu0-3 / default-on / rfkill-*` 等

**④ libgpiod 与"所有权"**

```bash
# A 终端占住
gpioset --mode=wait gpiochip3 1=1
# B 终端观察
gpioinfo gpiochip3 | grep "line   1:"
        line   1:  unnamed  "gpioset"  output  active-high [used]   ← ★
# B 终端抢线
gpioget gpiochip3 1
gpioget: error reading GPIO values: Device or resource busy        ← ★ 所有权生效
```

- 所有权粒度 = **单条 line**（line 2 依然可申请）
- ⚠️ `Ctrl+C` 强杀时**方向不重置**（退出后仍 `output`）

**⑤ ★★ 电平传递验证（杜邦线）**

- 接线：排针 **11**（输出）↔ 排针 **13**（输入）
- 输出 1 → 读到 **1**；输出 0 → 读到 **0**（**稳定且相反**）✅
- **完整链路被证明**：
  ```
  程序 → /dev/gpiochip3 → gpio 子系统 → pinctrl → 排针 11 →（杜邦线）→ 排针 13 → 另一程序
  ```
- **副产品**：中途拔线导致读数**随机跳变** → 意外验证了 **「稳定 = 有驱动源；跳变 = 悬空」** 这条 GPIO 输入经典判据

### 答案 review（三处纠正）

| 问题 | 用户答案 | 纠正 |
|------|---------|------|
| `trigger` 里方括号的含义 | "当前被调用的接口" | 应为「**当前激活的触发器**」 |
| 为什么 line 1 被拒、line 2 却成功 | （答成了第 7 问） | **所有权粒度 = 单条 line** |
| 所有权能否避免 K-029 的坑 | "要看 UART 是否正在使用" | **不能**——两者在**不同层**：所有权在 gpio 子系统，UART 占用在 **pinctrl 层**，互不知情 |

**✅ 答得最好的**：udev 只管 `/dev` 不管 `/sys`——说明权限模型的认知已经打通。

### 新经验

- **K-030**：udev 规则的 `GROUP` 依赖组存在（不存在则静默忽略）+ `newgrp` 的作用域
- **K-031**：改 sysfs 状态前必须先记录原始值（LED trigger 改坏不易还原）

### 进度

| 小节 | 状态 |
|------|------|
| 阶段 0 | ✅ 全部完成 |
| 1.0 环境探测收尾 | ✅ 完成 |
| **1.1 GPIO 与 LED** | ✅ **结课** |
| 1.2 字符设备驱动 | ⬜ **下一个** |
| 1.3 ~ 1.5 | ⬜ |

### ⏭️ 下次起点：**1.2 字符设备驱动**（6 h，分 3 次）

**已就绪**：
- 编译环境：`/lib/modules/6.1.141/build` 存在 → **板卡上原生编译**
- **不碰设备树**（规避"改设备树 → 重编内核 → 刷 boot 分区"的高风险链路）
- 第一次目标：**最小可加载模块**（`insmod` 打印 hello / `rmmod` 打印 bye）

**待办未清**：
- ⬜ **LED `trigger` 还原**为开机默认（`[timer]` + `brightness=1`），或直接重启板子
- ⬜ 每次开新终端需 `newgrp gpio`（或重启 VS Code Server 永久解决）
- ⬜ `:%s/line/row/g` 练习（0.5 遗留）
- ⬜ 排针 UART 电平实测（阶段 2 接线前必须做）
- ⬜ 下单：SG90 舵机 ×2 + 二轴云台 + 独立 5V 电源

**经验库累计**：16 条【实测】+ 15 条【通用】
