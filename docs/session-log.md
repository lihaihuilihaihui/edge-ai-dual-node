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

---

## 2026-09-27 · 1.2 字符设备驱动（第 1 次）✅ 最小内核模块跑通

### 🔥 途中撞上重大障碍：交叉编译的内核树不能在目标板上编模块

**现象**：板卡上 `make` 失败
```
warning: the compiler differs from the one used to build the kernel
  The kernel was built by: aarch64-linux-gnu-gcc ...
  You are using:           gcc ...
/bin/sh: 1: scripts/basic/fixdep: Exec format error
make[2]: *** Error 126
```

**根因**：`/usr/src/linux-headers-6.1.141/scripts/basic/fixdep` 是 **x86-64 二进制**
```
file → ELF 64-bit LSB pie executable, x86-64, interpreter /lib64/ld-linux-x86-64.so.2
```

- 内核构建产物分两类：**目标代码**（aarch64）+ **host 工具**（**构建机架构**）
- 内核在 x86 上交叉编译 → host 工具（`fixdep`/`conf`/`modpost`）是 x86 的
- 把它们拷到 aarch64 板卡 → **跑不了**

**尝试就地重建 → 死锁**：
```
make scripts → 需要 syncconfig → 需要 scripts/kconfig/conf → 它也是 x86 的 💀
```
且 `make modules_prepare` 有**副作用**：删除了 `include/generated/autoconf.h`

**✅ 正解：回到"编内核的那台机器"上编模块**（标准嵌入式工作流）

**用户的重要疑问 + 解答**：

> 「我的板卡自己无法编译代码？它自己不是有 Ubuntu 系统吗？」

**答**：不是"板卡不能编译"——**板卡能编普通程序**（0.4 的 make-lab 就是板卡上编的 ✅）。
真正受限的是"**依赖外来内核构建树**"的东西：编译模块需要 host 工具，而那套是 x86 的。
→ **类比**：把 Keil 工程拷到 STM32 上编译（工程里有 `armcc.exe` 这种 Windows 程序）
→ 详见 **K-032**

### 完整链路走通 ✅

| 步骤 | 在哪 | 命令 / 结果 |
|------|------|------------|
| 1 | 板卡 | 写 `edge_ai_evt.c` + `Makefile` |
| 2 | 虚拟机 | `scp -r lihaihui@192.168.31.151:~/.../edge_ai_evt ~/` |
| 3 | 虚拟机 | `make -C ~/tspi_sdk/kernel-6.1 M=$PWD ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- modules` |
| 4 | 虚拟机 | `file edge_ai_evt.ko` → **ELF 64-bit LSB relocatable, ARM aarch64** ✅ |
| 5 | 虚拟机 | `scp edge_ai_evt.ko lihaihui@192.168.31.151:~/.../edge_ai_evt/` |
| 6 | 板卡 | `sudo insmod edge_ai_evt.ko` → `lsmod` 可见 ✅ |
| 7 | 板卡 | `dmesg` → `edge_ai_evt init` ✅ |
| 8 | 板卡 | `sudo rmmod edge_ai_evt` → `dmesg` → `edge_ai_evt exit` ✅ |

**关键**：虚拟机上**没有** `warning: the compiler differs`——因为在编内核的那棵树、那台机器上编 ✅

### 关键环境事实（已记录）

| 项 | 值 |
|---|---|
| **内核源码树（x86）** | **`~/tspi_sdk/kernel-6.1/`**（含 `.config`、`Module.symvers`、`vmlinux`、host 工具） |
| 同目录还有 | `~/tspi_sdk/u-boot/`、`~/tspi_sdk/output/recovery-kernel/` |
| **编译命令** | `make -C ~/tspi_sdk/kernel-6.1 M=$PWD ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- modules` |
| **传输通路** | 虚拟机 ↔ 板卡 **双向 scp**（虚拟机可拉可推）✅ |

### 学到的三件事

1. **`taints kernel` 是正常提示，不是错误** → **K-033**
   - `loading out-of-tree module taints kernel` = 内核标记"加载过非官方代码"
   - 影响：内核崩溃报告**不被官方受理**（无法确定是否你的模块引起）
   - `MODULE_LICENSE("GPL")` 避免了"专有 license"的额外 taint
2. **`.o.d` 文件 = 0.4 学的 `-MMD` 的产物**（知识串联 ✅）
   - kbuild 用 **`fixdep`** 生成 `.o.d`（列出 `.c` 依赖的所有 `.h`）
   - `.o.cmd` 记录编译命令（kbuild 判断是否重编的依据）
   - **内核构建系统做的事，和 0.4 手写 Makefile 本质相同**，只是规模大得多
3. **编译产物必须验证架构**：`file xxx.ko` → `ARM aarch64`（0.3 的习惯在这里复用）

### 待改进

- ⬜ `printk` 未加日志级别（建议 `printk(KERN_INFO "edge_ai_evt: ...\n")`）
- ✅ README 阶段进度表已更新（用户提醒得对）
- ⬜ 可考虑在虚拟机上配 GitHub SSH key（以后 git 同步比 scp 优雅）

### 进度

| 小节 | 状态 |
|------|------|
| 1.0 环境探测 | ✅ |
| 1.1 GPIO 与 LED | ✅ 结课 |
| **1.2 字符设备驱动** | 🟡 **第 1 次完成**（最小模块跑通），第 2/3 次待做 |
| 1.3 ~ 1.5 | ⬜ |

### ⏭️ 下次起点：**1.2 第 2 次 —— `file_operations` + 自动创建设备节点**

目标：`insmod` 后 **`/dev/edge_ai0` 自动出现**，用户态能 `open` 它。

**经验库累计**：18 条【实测】+ 15 条【通用】

---

## 2026-09-27（下半场）· 1.2 第 2 次完成 ✅ `file_operations` + 自动创建设备节点

### 一、构建/部署工具链建立（基础设施，以后一直用）

**问题**：虚拟机 ↔ 板卡来回 `scp` + 手打长命令太繁琐

**解决（三件套）**：

| # | 做什么 | 效果 |
|---|--------|------|
| 1 | **SSH 免密**：虚拟机 `ssh-copy-id lihaihui@lckfb` | 不用每次输密码 |
| 2 | **SSH 别名**：虚拟机 `~/.ssh/config` 定义 `Host board`（`HostName lckfb`） | 命令里写 `board` 而不是 IP；IP 变了只改一处 |
| 3 | **一键脚本** `~/bin/module-deploy.sh <模块名>` | **rsync 同步源码 → 交叉编译 → 回传 .ko** |

**脚本关键点**：
- 用 `rsync -av --delete` **删除目标端多余文件**（实测有效：清掉了虚拟机上残留的 `.o`/`modules.order`，避免旧产物干扰）
- 配置区与逻辑分离（换模块只传参，换板卡只改 `BOARD_BASE`）

**用法**：`module-deploy.sh edge_ai_evt`

**环境事实**：
- 板卡主机名 `lckfb`，由虚拟机 `/etc/hosts` 解析（`192.168.31.151 lckfb.localdomain`）
- 板卡镜像为**最小化镜像**（登录时提示 `This system has been minimized...`）→ 缺命令属正常，缺什么装什么（已装 `file`、`rsync`）
- `lckfb.local`（mDNS）**不可用**——虚拟机在 NAT 网段、板卡在 LAN，**不同广播域**；要用 mDNS 须先把 VMware 改桥接

### 二、1.2 第 2 次：核心成果

**新增**：`file_operations` + `register_chrdev` + `class_create` + `device_create`

| 验证项 | 结果 |
|--------|------|
| 设备节点 | ✅ `/dev/edge_ai0` **自动出现**（无需 `mknod`） |
| 主设备号 | **510**（内核动态分配，从高位倒着找） |
| 权限 | `root:root`（**udev 默认规则**给的 —— 呼应 1.1） |
| 设备号在 sysfs 的落地 | `cat /sys/class/edge_ai/edge_ai0/dev` → **`510:0`** |
| class 软链接 | `/sys/class/edge_ai/edge_ai0` → `../../devices/virtual/edge_ai/edge_ai0`（parent 传 NULL → 归 virtual 总线） |
| **注释掉 class/device** | ✅ `/dev/edge_ai0` 不出现，但 `/proc/devices` 仍有驱动 → **证明"设备号 ≠ 设备节点"** |
| **引用计数保护** | ✅ 终端 A 挂着 `open`，终端 B `rmmod` → **`Module edge_ai_evt is in use`** |
| `open`/`release` 回调 | ✅ `dmesg` 可见 |

**代码 review 结论**：错误处理**全部正确** ✅
（`register_chrdev` 用 `< 0` 判断、`IS_ERR`/`PTR_ERR` 用法正确、**每级失败都回滚已申请的资源**、`exit` 严格逆序释放、`.owner = THIS_MODULE` 到位）

### 三、新知识点

**① `EACCES` 与 `EINVAL` 来自不同的层** → **K-034**

| 错误 | 场景 | 来自哪层 | 说明 |
|------|------|---------|------|
| `Permission denied`（`EACCES`） | 普通用户 `open()` | **VFS 权限检查** | 设备节点权限拒绝，**驱动代码根本没执行** |
| `Invalid argument`（`EINVAL`） | root `read()` | **VFS 调用 fops** | 权限过了，但 `.read` 是 `NULL` → 返回 `-EINVAL` |

**排错指引**：见 `EACCES` 查权限/udev 规则；见 `EINVAL` 查 fops 实现了哪些回调。

**② 为什么 `cat /dev/edge_ai0` 报 `Invalid argument`**

`cat` = `open` + 循环 `read` + `close`。当前 fops 只有 `.open`/`.release`，**`.read` 是 `NULL`** → VFS 返回 `-EINVAL` → `cat` 打印错误并退出。
（这也解释了为什么"引用计数实验"里 `cat` 挂不住——它立刻退出了；改用 Python 只 `open` 不 `read` 才挂住。）

### 四、⏭️ 下次起点：1.2 第 3 次（最后一次）—— `.read` + `.ioctl`

> ⚡ **开工前必须先按下面的改进意见调整代码**（用户明确要求提醒）

| # | 改进项 | 具体做法 |
|---|--------|---------|
| 1 | **`printk` 宏与字符串之间加空格** | `printk(KERN_INFO "…")`（现在写成了 `KERN_INFO"…"`，能编译但可读性差） |
| 2 | **日志加模块名前缀** | `"edge_ai_evt: device opened\n"` |
| 3 | ⭐ **改用 `pr_fmt` + `pr_info`/`pr_err`**（现代内核标准写法） | **`#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt` 必须写在所有 `#include` 之前！**（写在后面会失效） |
| 4 | **`MODULE_VERSION` 升到 `0.3`** | 功能变化时同步版本号 |

**第 3 次要实现**：

| # | 内容 | 核心 API ⭐ |
|---|------|-----------|
| 1 | `.read` 回调 | **`copy_to_user()`** ← **跨用户态/内核态边界，本单元最重要的概念** |
| 2 | 内核内部数据缓冲区 | `kmalloc` / `kfree` |
| 3 | `.unlocked_ioctl` | 自定义命令（配置参数 / 清空队列） |
| 4 | （可选）等待队列 | 无数据时阻塞 `read`，而不是立刻返回 0 |

**做完第 3 次，`edge_ai_evt` 就是功能完整的教学驱动**（SPEC FR-28 达成），**阶段 1.2 全部结课**。

---

## 2026-09-28 · 1.2 第 3 次 —— `.read` + `copy_to_user` ✅

### 一、先修正了昨天笔记里的一个错误（我的锅）

昨天笔记把设备注册写成 `alloc_chrdev_region` + `cdev_init`/`cdev_add`，**实际用的是 `register_chrdev`**（一步到位的老接口）。已修正 `1.2-driver-notes.md` §7.1b，并补了新旧接口对比表。

**顺带一个坑点**：`register_chrdev` **成功返回主设备号本身（正数）**，不是 0 → 必须 `if (major < 0)`，不能 `if (!major)`（主设备号 0 是合法值）→ **K-039**

### 二、开工前按 4 条改进意见改了代码

| # | 改进项 | 结果 |
|---|--------|------|
| 1 | `printk` 宏与字符串间加空格 | ✅ |
| 2 | 日志加模块名前缀 | ✅ |
| 3 | ⭐ 改用 `pr_fmt` + `pr_info`/`pr_err` | ✅ |
| 4 | `MODULE_VERSION` → `0.3` | ✅ |

**`pr_fmt` 机制（查 `printk.h` 实证）**：

```c
/* printk.h:346 */  #ifndef pr_fmt
                   #define pr_fmt(fmt) fmt
                   #endif
/* printk.h:529 */  #define pr_info(fmt, ...) printk(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)
```

`pr_info` 只是**薄封装**，把 `pr_fmt(fmt)` 塞进 `printk`。展开链：
`pr_info("x")` → `printk(KERN_INFO pr_fmt("x"))` → `printk(KERN_INFO KBUILD_MODNAME ": " "x")` → 字符串自动拼接。

**为什么必须写在所有 `#include` 之前**：`printk.h` 用 `#ifndef pr_fmt` 做兜底。写在前面 → 内核不定义默认版，干净；写在后面 → 触发 `"pr_fmt" redefined` 警告，且头文件里已展开过的 `pr_*` 调用用的是旧前缀。

**dmesg 实测**：

```
[ 2926.560635] edge_ai_evt: loading out-of-tree module taints kernel.   ← 无前缀（内核打的）
[ 2926.561276] edge_ai_evt: registered, major=510
[ 2926.561604] edge_ai_evt: module loaded
```

> 第一行**没有**前缀恰好反证了前缀确实来自我们的 `pr_info`。

### 三、IDE 红线排查（→ K-035）

**现象**：交叉编译完全成功、模块运行正常，但 VS Code 里 8 个 `pr_info`/`pr_err` 全飘红，错误是 **`应输入 ")"`（语法错）**，**不是**"未定义标识符"。

**推理链**：

| 报什么 | 说明 |
|---|---|
| `identifier "pr_info" is undefined` | 找不到 `printk.h` → includePath 没配好 |
| **`应输入 ")"`** | 头文件找到了、宏也展开了，只是展开结果里有**未定义的宏** |

**根因**：`KBUILD_MODNAME` 未定义 → 展开成 `printk("\001" "6" KBUILD_MODNAME ": " "x")` → 裸标识符夹在字符串字面量中间 → 语法错。
`KBUILD_MODNAME` **不在任何头文件里定义**（`printk.h` 只在注释里出现过），它是 **kbuild 用 `-D` 注入的**。

**解法**：`.vscode/c_cpp_properties.json` 的 `defines` 加 `"KBUILD_MODNAME=\"edge_ai_evt\""`（值只是占位，真实值永远由 kbuild 给）。

> **MCU 类比**：等价于 Keil 的 `Options → C/C++ → Define`——Keil 认识，VSCode 插件不认识，就飘红。

### 四、`.read`：三个动手前的问题（含答案）

| # | 问题 | 答案 |
|---|------|------|
| 1 | 缓冲区空了返回 `0` 还是 `-EAGAIN`？ | `0` = EOF（以后也没有了）→ `cat` 正常退出；`-EAGAIN` = **暂时**没数据、以后可能有（非阻塞模式）→ **不是错误**。本设备是固定消息 → `return 0` 正确 |
| 2 | `copy_to_user` 返回 `5` 是什么意思？ | 是**"还剩 5 字节没搬完"**，不是错误码 → `if (ret) return -EFAULT;`。❌ 直接 `return ret` 会让调用者以为"读到了 5 字节"，**结论完全反了** |
| 3 | `GFP_KERNEL` 是什么？ | **GFP = Get Free Pages**，是 `kmalloc` 的**标志位**，不是地址。含义是"**允许睡眠**"。中断上下文**绝对不能睡眠**（处理程序不属于任何任务，睡了没人唤醒）→ 中断里只能用 `GFP_ATOMIC` |

> **MCU 类比（Q3）**：`GFP_KERNEL` ≈ 阻塞式发送（可等）；`GFP_ATOMIC` ≈ ISR 里只能读寄存器不能等。**ISR 里不能阻塞 —— MCU 和 Linux 完全一致。**

### 五、⭐ 用户自己发现的改进：用 `*ppos` 而不是全局 `kpos`

计划里给的是"全局 `kpos` 记录读到哪了"，用户改用 `*ppos`（`struct file::f_pos`）。**这个改动更正确**：

| 方案 | 进程 A `cat` 读完后，进程 B 再 open | 结果 |
|---|---|---|
| 全局 `kpos` | `kpos` 已 == `klen` | B **读到空** ❌ |
| **`*ppos`** | B 的 `f_pos` 从 **0** 开始 | B **能重新读到全文** ✅ |

**原因**：`struct file` 是每次 `open` 创建的，所以 `f_pos` **每 fd 独立**。内核里所有正规驱动（`/proc`、sysfs）都这么做。

**实测证据**：

```bash
sudo cat /dev/edge_ai0        # → Hello from edge_ai_evt module!
sudo cat /dev/edge_ai0        # → 又读到全文（若用全局 kpos 这里会是空）
sudo head -c 5 /dev/edge_ai0  # → Hello
```

### 六、code review

✅ **错误处理全部正确**：`if (count > klen - pos)` 截断（`pos < klen` 保证不下溢）、失败时不推进 `*ppos`、`kmalloc` 失败回滚链完整、`kfree` + 置 `NULL`。

⚠️ **清理**：`kpos` 改用 `*ppos` 后成了**死变量**，已删除（换方案后要清残留）。

**关于 `exit` 顺序**：用户把 `kfree` 放在最后（先断对外入口，后放内部数据）。这是**主流做法**，安全性依据是 **`rmmod` 先检查引用计数**——非 0 直接失败，所以 `exit` 执行时保证无用户。

---

## 2026-09-29 · 1.2 第 4 次 —— `.write` + `.unlocked_ioctl` ✅ **1.2 结课**

### 一、原理：为什么还需要 ioctl

`read`/`write` 只能传**字节流**，但设备需要**控制动作**（清空缓冲区、设参数、查状态）。`ioctl` 是"**带外命令**"的通用通道。

> **MCU 类比**：串口既有 `HAL_UART_Transmit()` 传数据，也有 `HAL_UART_Init()` 做配置——`ioctl` 就是 Linux 给所有设备提供的**通用配置入口**。

**三种回调**：`.ioctl`（已废弃，持大内核锁）→ **`.unlocked_ioctl`（用这个）** → `.compat_ioctl`（32 位程序跑在 64 位内核时的转换）

### 二、⭐ 命令号为什么必须是"魔数"

不能写 `cmd = 1`。`cmd` 是个 `unsigned int`，里面塞了 **4 个字段**：

```
 31   30 29 ............ 16 15 14 ............ 8 7 ....... 0
┌─────┬───────────────────┬────────┬────────────────┬──────────┐
│ 方向 │      size         │  type  │                │   nr     │
│(2位)│     (14位)        │ (8位)  │                │  (8位)   │
└─────┴───────────────────┴────────┴────────────────┴──────────┘
```

**为什么需要 type（魔术字）**：所有驱动的 `ioctl` **共用同一个系统调用入口**。驱动 A 用 `cmd=1` 表示"清空设备"、驱动 B 也用 `cmd=1` 表示"格式化硬盘"，用户程序**传错 fd** 就是灾难。魔术字隔离开这个空间。

**size 字段的意义**：让内核能校验 32/64 位兼容性 → 这也正是 **`compat_ioctl` 存在的原因**（32 位用户态的指针是 4 字节）。

### 三、工程结构：拆出共享头文件

```
drivers/edge_ai_evt/
├── edge_ai_evt.h      ← 契约：命令号 + 结构体 + 常量（内核态【与】用户态共用）
├── edge_ai_evt.c
├── test_edge_ai.c     ← 用户态测试程序（自写自编）
├── Makefile
└── README.md
```

`#ifdef __KERNEL__` 分支切 `<linux/ioctl.h>` / `<sys/ioctl.h>`（两处 `_IO*` 宏定义**兼容**）。

**为什么必须共享**：命令号是**内核与用户态的契约（ABI）**；两边各写一份 → 改了一边忘了另一边 → 用户程序发"过时"命令号 → 行为错乱且极难排查。

### 四、三个命令

| 命令 | 类型 | `arg` 语义 |
|---|---|---|
| `EDGE_AI_IOC_CLEAR` | `_IO` | 无参（`klen = 0`） |
| `EDGE_AI_IOC_PUT` | `_IOW` | 指向 `struct edge_ai_put_arg` 的用户指针 |
| `EDGE_AI_IOC_LEN` | `_IOR` | 指向 `int` 的用户指针 |

### 五、⚠️ `PUT` 的两次迭代（本次最有价值的教训 → K-037）

**v1（有"运气型 bug"）**：

```c
char user_buf[BUF_SIZE];
copy_from_user(user_buf, (char __user *)arg, BUF_SIZE);   /* 无条件拷 256 字节 */
```

用户传的是 23 字节的字符串，驱动却读 256 字节。**这次没崩是运气**（字符串在 `.rodata` 段，后面同页内有数据）。若字符串正好在**页尾** → 后面是未映射页 → `-EFAULT`，而且**时灵时不灵**（取决于链接器布局）。**这类 bug 最难查。**

**v2（正确）—— 结构体方案**：

```c
struct edge_ai_put_arg put_arg;
if (copy_from_user(&put_arg, (void __user *)arg, sizeof(put_arg)))  /* 大小固定 */
    { ret = -EFAULT; break; }
if (put_arg.len < 0 || put_arg.len > BUF_SIZE)                      /* 边界校验 */
    { ret = -EINVAL; break; }
mutex_lock(&kbuf_lock);
memcpy(kbuf, put_arg.data, put_arg.len);
klen = put_arg.len;
mutex_unlock(&kbuf_lock);
```

**结构体方案优于 `strnlen_user`**：契约**显式**（必须传完整结构体）；**能传二进制数据**（可含 `\0`），不像字符串方案受 `\0` 限制。内核里 `struct i2c_msg`、`struct v4l2_buffer` 都是这个套路。

⚠️ **`len < 0` 那半边判断是生死线**：`memcpy` 第三个参数是 `size_t`（无符号），`(size_t)-1` 会拷到天荒地老 → **内核堆溢出 → 可被提权**。

### 六、code review 要点

| 项 | 结论 |
|---|---|
| ⭐ `_IOC_TYPE(cmd) != MAGIC` **前置检查** | ✅ 用户自己加的（我没提示过），很专业——避免魔术字撞车 |
| 未知命令 → **`-ENOTTY`** | ✅ 不是 `-EINVAL`（用户程序靠它区分"命令不支持" vs "参数不合法"） |
| `case` 里声明变量 | ⚠️ **`case` 不构成作用域**！实测：两个 case 都写 `int len;` → `redeclaration` 错误 → 要加 `{}` → **K-036** |
| 锁的作用域 | ✅ `copy_from_user` 在**锁外**（可能睡眠，**持锁睡眠是内核大忌**） |
| `size_t → int` 转 ABI | ✅ `LEN` 用中间变量，没直接拷 `&klen`（64→32 位会语义混乱） |

### 七、实测结果（✅ 全过）

```
Buffer length: 22                          ← LEN
After CLEAR, buffer length: 0              ← CLEAR + LEN
PUT succeeded, len = 22                    ← PUT
After PUT, buffer length: 22               ← LEN
ioctl returned ENOTTY as expected for invalid command
EINVAL as expected for len > BUF_SIZE      ← 边界：超长
EINVAL as expected for len < 0             ← 边界：负数
```

✅ 内容验证：`sudo cat /dev/edge_ai0` → `Hello from user space!`

### 八、⚠️ 一个"假 bug"教训（→ K-038）

`cat` 的输出**没有换行符**，紧贴在 shell 提示符前面，看上去像"完全没输出"，**我一度误判为"驱动和用户态不一致"的严重 bug**，白排查一轮。

- **为什么没换行**：`klen = strlen(...)` 不含 `\0`，而 `"Hello from user space!"` 本身也没 `\n`
- **核心概念**：**设备文件不做任何格式化**，`cat` 只把字节**原样搬运**。有没有换行取决于你写进去的内容
- **教训**：验证"有没有数据"用 `wc -c` / `xxd`，**不要用眼睛**

> **MCU 类比**：串口发 `"OK"` 而不是 `"OK\r\n"`，上位机就一行连一行。

---

## 🎉 1.2 结课（2026-09-29）

### 验收（全部 ✅）

- [x] `insmod` 后 `/dev/edge_ai0` **自动出现**（无需 `mknod`）
- [x] 5 个 fops 回调全实现（`open`/`release`/`read`/`write`/`unlocked_ioctl`）
- [x] `copy_to_user` / `copy_from_user` 跨边界
- [x] `kmalloc` / `kfree` 内存生命周期
- [x] 3 个 ioctl 命令 + 未知命令 `-ENOTTY`
- [x] **用户态测试程序自写自编**
- [x] 长度边界校验（超长 + 负数 → `EINVAL`）
- [x] 引用计数保护（`Module is in use`）
- [x] 错误回滚链完整 + `exit` 释放干净
- [x] **对应 `SPEC.md` FR-28：达成**

### 产出

| 文件 | 内容 |
|---|---|
| `stage1-rk3566/drivers/edge_ai_evt/edge_ai_evt.c` | 驱动 |
| `stage1-rk3566/drivers/edge_ai_evt/edge_ai_evt.h` | 共享 ABI 头文件 |
| `stage1-rk3566/drivers/edge_ai_evt/test_edge_ai.c` | 用户态测试程序 |
| `stage1-rk3566/docs/1.2-driver-notes.md` | 学习笔记（§一~§十二） |
| 经验库 | 新增 K-034 ~ K-039（实测累计 **24** 条） |

### 进度

| 小节 | 状态 |
|------|------|
| 1.0 环境探测 | ✅ |
| 1.1 GPIO 与 LED | ✅ 结课 |
| **1.2 字符设备驱动** | ✅ **结课**（4 次完成） |
| 1.3 摄像头 V4L2 | ⬜ **下次起点** |
| 1.4 串口与协议 | ⬜ |
| 1.5 应用工程化 | ⬜ |

### ⏭️ 下次起点：1.3 摄像头 V4L2

**开工前准备**：
- ⬜ 确认摄像头到手；`ls -l /dev/video*` + `v4l2-ctl --list-devices`
- ⬜ `v4l2-ctl --list-formats-ext` 看格式与分辨率
- ⬜ `sudo apt install v4l-utils`（若缺）
- ⬜ **绝不硬编码 `/dev/video0`**（K-016）—— 运行时枚举校验

**核心概念**：V4L2 缓冲队列（`REQBUFS`→`mmap`→`QBUF`→`DQBUF`）、`mmap` 零拷贝取帧、格式协商

**目标**：抓一帧存成 JPEG

### 遗留

- ⬜ `Makefile` 的 `KDIR` 仍指向板卡路径（实际被 `-C $HOME/tspi_sdk/kernel-6.1` 覆盖，可加注释）
- ⬜ 可选补测：`len = BUF_SIZE`（正好满载）应成功
- ⬜ `/dev/edge_ai0` 的 udev 规则（普通用户可访问）—— 可选
- ⬜ 排针 UART 电压未量（阶段 2 接线前必做）
- ⬜ 舵机/云台采购（SG90 ×2、二轴支架、独立 5V 电源，约 50 元）

**经验库累计**：24 条【实测】+ 15 条【通用】
