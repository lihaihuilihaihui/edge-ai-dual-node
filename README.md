# 边缘 AI 双芯感知节点 (Edge AI Dual-Node)

> 智能电表 MCU 工程师 → 嵌入式 Linux + 边缘 AI 的进阶作品
> 由 GitHub Copilot 全程辅助开发（AI 辅助开发实战）

## 项目一句话

**低功耗 MCU 常驻侦测 + Linux NPU 按需重推理**的区域智能安防节点。
ESP32 用 PIR 常驻感知（μA 级功耗）→ 有人接近则唤醒 RK3566 → 摄像头 + NPU 推理确认 → UART 下发指令 → ESP32 蜂鸣器报警 / 舵机云台转向 / OLED 显示。

**双芯分工的道理**：侦测是低功耗、低算力场景，MCU 胜任；视觉推理需 ~1 TOPS，必须上 NPU。
这不是「为了用两个芯片而用两个芯片」，而是**功耗与算力的分层设计**。

## 系统架构

```
┌────────────────────┐   UART 帧协议   ┌─────────────────────┐  RKNN  ┌────────┐
│  ESP32 (ESP-IDF)   │◄───────────────►│  RK3566 (Linux)     │◄──────►│  NPU   │
│  常驻低功耗层       │  115200 8N1     │  推理决策层          │        │ ~1TOPS │
│  · PIR 中断侦测    │  CRC16 校验      │  · V4L2 采集        │        │        │
│  · 温湿度采集      │                 │  · 字符驱动/设备树   │        └────────┘
│  · 蜂鸣器/LED      │                 │  · 多线程应用        │
│  · 舵机云台/OLED   │                 │  · 状态机决策        │
└────────────────────┘                 └─────────────────────┘
        感知端 + 执行端                        网关 + 推理 + 决策
```

## 开发平台

- **主控**：立创开发板（Lckfb TSPI V10），Rockchip RK3566
  - 4× Cortex-A55 @1.8GHz / 2GB RAM / RKNN NPU(~1TOPS)
  - Ubuntu 22.04 定制镜像，内核 6.1.141（自编译）
  - WiFi(bcm43438)、eMMC 12G + TF 卡 128G(/mnt/tfcard)
- **前端**：ESP32（已持有），ESP-IDF v5.x + FreeRTOS
- **摄像头**：USB UVC 免驱（避开 MIPI CSI 驱动风险）

## 学习路线（分阶段）

| 阶段 | 内容 | 状态 |
|------|------|------|
| 0 | 工具链补课：git / 交叉编译 / 工程化 | 🟡 进行中 |
| 1 | RK3566 Linux 底层：GPIO → 字符驱动 → UART → 多线程应用 | ⬜ 未开始 |
| 2 | ESP32 采集 + 自定义 UART 协议 + 双芯双向通信 | ⬜ 未开始 |
| 3 | 边缘 AI：RKNN 模型转换 → 摄像头目标检测 → AI 驱动执行 | ⬜ 未开始 |
| 4 | 作品收尾：文档 / 演示 / 博客 / 简历 | ⬜ 未开始 |

详细计划见 [`docs/PLAN.md`](docs/PLAN.md)；具体功能/指标/技术选型见 [`docs/SPEC.md`](docs/SPEC.md)。

## 文档导览

| 文档 | 内容 | 什么时候看 |
|------|------|-----------|
| [`docs/SPEC.md`](docs/SPEC.md) | **实现契约**：功能需求、量化指标、技术选型、协议定义、BOM、风险 | 想知道「做成什么样」 |
| [`docs/PLAN.md`](docs/PLAN.md) | 分阶段学习计划与里程碑 | 想知道「学到哪了」 |
| [`docs/COLLABORATION.md`](docs/COLLABORATION.md) | **人机分工原则**（教练模式五步法） | 每次开始新单元 |
| [`docs/session-log.md`](docs/session-log.md) | 跨会话上下文、设备状态、已完成配置 | **每次新会话先读** |
| [`.github/skills/edge-ai-node/`](.github/skills/edge-ai-node/SKILL.md) | **项目技能库**：经验沉淀，Copilot 自动加载 | 排错前查 `references/knowledge-log.md` |

## 目录结构

```
edge-ai-dual-node/
├── README.md              ← 本文件（作品门面）
├── .github/skills/        ← 项目技能库（Copilot 自动加载 + 人可读经验库）
│   └── edge-ai-node/
│       ├── SKILL.md
│       └── references/    ← 硬件事实 / 环境配置 / 踩坑记录
├── docs/
│   ├── SPEC.md            ← 实现契约（功能/指标/方案）
│   ├── PLAN.md            ← 完整项目计划书
│   ├── COLLABORATION.md   ← 协作原则（教练模式）
│   └── session-log.md     ← 会话/设备状态记录（延续上下文）
├── stage0-toolchain/      ← 阶段0：git/编译工具链练习
├── stage1-rk3566/
│   ├── drivers/           ← 阶段1：Linux 字符设备驱动
│   ├── apps/              ← 阶段1：用户态应用
│   └── docs/              ← 阶段1 学习笔记
├── stage2-esp32/          ← 阶段2：ESP32 前端固件
├── stage3-ai/             ← 阶段3：RKNN 推理部署
└── scripts/               ← 辅助脚本
```

## 环境备忘

- 数据大头放 TF 卡：`/mnt/tfcard/{models,datasets,workspace,backup,Downloads}`
- `~/Downloads` 已软链到 TF 卡
- zram 1G 交换已启用（服务 `zram-swap.service`）

## 验证指标

端到端延迟（PIR触发→蜂鸣器响）：冷启动 < 2.0s，稳态 < 800ms ｜ 推理帧率 ≥ 8 FPS @640×640 INT8
检测召回率 ≥ 90% ｜ 误报率 ≤ 5% ｜ 连续 24h 长稳，RSS 增长 < 10% ｜ 串口 1000 帧 0 误码

阶段检查点：
- [ ] 阶段 1 末：自写驱动 + 应用与硬件交互
- [ ] 阶段 2 末：ESP32 ↔ RK3566 串口双向闭环
- [ ] 阶段 3 末：摄像头实时识别并驱动 ESP32 执行完整 Demo Script

完整指标定义与测量方法见 [`docs/SPEC.md`](docs/SPEC.md) §3。
