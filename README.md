# 边缘 AI 双芯感知节点 (Edge AI Dual-Node)

> 智能电表 MCU 工程师 → 嵌入式 Linux + 边缘 AI 的进阶作品
> 由 GitHub Copilot 全程辅助开发（AI 辅助开发实战）

## 项目一句话

用 **ESP32(MCU)** + **RK3566(Linux + NPU)** 双芯通过 UART 组成端侧智能闭环：
感知 → 边缘推理 → 执行，形态贴近智能电表/工业网关的"采集端 MCU + 边缘 Linux 网关"架构。

## 系统架构

```
┌───────────────┐  UART 帧协议(自定义/CRC)  ┌───────────────────┐   RKNN    ┌────────┐
│  ESP32 MCU     │◄──────────────────────────►│  RK3566 Linux     │◄─────────►│  NPU    │
│  读传感器       │                            │  驱动+应用+协议解析  │  推理转换  │ 检测    │
│  控 LED/舵机    │                            │ (字符驱动/设备树)   │          │ (YOLO)  │
└───────────────┘                            └───────────────────┘          └────────┘
```

## 开发平台

- **主控**：立创开发板（Lckfb TSPI V10），Rockchip RK3566
  - 4× Cortex-A55 @1.8GHz / 2GB RAM / RKNN NPU(~1TOPS)
  - Ubuntu 22.04 定制镜像，内核 6.1.141（自编译）
  - WiFi(bcm43438)、eMMC 12G + TF 卡 128G(/mnt/tfcard)
- **前端**：ESP32（已持有）

## 学习路线（分阶段）

| 阶段 | 内容 | 状态 |
|------|------|------|
| 0 | 工具链补课：git / 交叉编译 / 工程化 | 🟡 进行中 |
| 1 | RK3566 Linux 底层：GPIO → 字符驱动 → UART → 多线程应用 | ⬜ 未开始 |
| 2 | ESP32 采集 + 自定义 UART 协议 + 双芯双向通信 | ⬜ 未开始 |
| 3 | 边缘 AI：RKNN 模型转换 → 摄像头目标检测 → AI 驱动执行 | ⬜ 未开始 |
| 4 | 作品收尾：文档 / 演示 / 博客 / 简历 | ⬜ 未开始 |

详见 [`docs/PLAN.md`](docs/PLAN.md)（完整计划）与 [`docs/session-log.md`](docs/session-log.md)（会话与设备记录）。

## 目录结构

```
edge-ai-dual-node/
├── README.md              ← 本文件（作品门面）
├── docs/
│   ├── PLAN.md            ← 完整项目计划书
│   └── session-log.md     ← 会话/设备状态记录（延续上下文）
├── stage0-toolchain/      ← 阶段0：git/编译工具链练习
├── stage1-rk3566/
│   ├── drivers/           ← 阶段1：Linux 字符设备驱动
│   └── apps/              ← 阶段1：用户态应用
├── stage2-esp32/          ← 阶段2：ESP32 前端固件
├── stage3-ai/             ← 阶段3：RKNN 推理部署
└── scripts/               ← 辅助脚本
```

## 环境备忘

- 数据大头放 TF 卡：`/mnt/tfcard/{models,datasets,workspace,backup,Downloads}`
- `~/Downloads` 已软链到 TF 卡
- zram 1G 交换已启用（服务 `zram-swap.service`）

## 验证指标（阶段目标）

- [ ] 阶段1末：自写驱动 + 应用与硬件交互
- [ ] 阶段2末：ESP32 ↔ RK3566 串口双向闭环
- [ ] 阶段3末：摄像头实时识别并驱动 ESP32 执行
