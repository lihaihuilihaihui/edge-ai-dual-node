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
- 会话里程碑：2026-09-06 设备分析/zram/TF卡配置；2026-09-09 立项+建仓
