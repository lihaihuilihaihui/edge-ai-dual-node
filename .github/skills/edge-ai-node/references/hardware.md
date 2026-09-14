# 硬件事实库

> 原则：**只记录实测或查证过的信息**，并标注来源与日期。未确认的写进「待确认」。
> 这里的信息会被反复引用，错误会导致接线烧板，务必谨慎。

---

## 1. 主控板：Lckfb TSPI V10（Rockchip RK3566）

| 项 | 值 | 来源 / 日期 |
|----|-----|-----------|
| SoC | Rockchip RK3566 | 实测 2026-09-06 |
| CPU | 4× Cortex-A55，最高 1.8GHz / 最低 408MHz | 实测 2026-09-06 |
| 内存 | 2GB | 实测 2026-09-06 |
| NPU | RKNN ~1 TOPS | 规格 |
| 板载 WiFi | bcm43438（SDIO，mmc2） | 实测 2026-09-06 |
| 存储 | eMMC 14.6G：`/`=12G、`/userdata`=2.2G、`/oem` | 实测 2026-09-06 |
| 启动链 | U-Boot(2026-09-04) / ATF bl31-v1.46 / OP-TEE bl32-v2.16 / ddr-v1.25 | 实测 |
| 操作系统 | Ubuntu 22.04.5 定制镜像 | 实测 |
| 内核 | 6.1.141（2026-09-04 自编译） | 实测 |

### 外接存储

| 项 | 值 |
|----|-----|
| 设备 | `/dev/mmcblk1p1` |
| 文件系统 | ext4，标签 `lckfb_data` |
| UUID | `465007ea-1b9d-4554-9f94-5c8903745f05` |
| 挂载点 | `/mnt/tfcard`（fstab，`defaults,noatime,nofail`） |
| 读写速度 | 写 ~50MB/s，读 ~57MB/s（实测） |
| 目录 | `models/ datasets/ workspace/ backup/ Downloads/` |

---

## 2. 已确认的设备节点与权限（2026-09-10 实测）

### 2.1 设备节点全景

| 类别 | 节点 | 说明 | 权限/属组 |
|------|------|------|----------|
| **ISP（平台）** | `/dev/video0` ~ `/dev/video9` | ⚠️ 被 rkisp 驱动**预先占用**，与 USB 摄像头无关 | `root:video` 660 |
| **USB 摄像头** | `/dev/video10` 起（插上后） | 编号不固定，**绝不能硬编码** | `root:video` 660 |
| **NPU** | `/dev/dri/renderD128` (还有 renderD129) | ⚠️ 新版 rknpu2 用 DRM 暴露，**不是 `/dev/rknpu`** | `root:render` 660 |
| 显示 | `/dev/dri/card0`, `card1` | HDMI 输出 | `root:video` 660 |
| 视频编解码 | `/dev/mpp_service` | ⚠️ 这是 VPU（硬件编解码），**不是 NPU** | `root:video` 660 |
| 2D 加速 | `/dev/rga` | 硬件光栅/缩放加速 | `root:video` 660 |

实测记录（`/sys/class/video4linux/video*/name`）：
```
video0 rkisp_mainpath      video1 rkisp_selfpath       video2 rkisp_rawwr0
video3 rkisp_rawwr2        video4 rkisp_rawwr3         video5 rkisp_iqtool
video6 rkisp_rawrd0_m      video7 rkisp_rawrd2_s       video8 rkisp-statistics
video9 rkisp-input-params
```

### 2.2 用户组（✅ 2026-09-10 已完成）

```
groups = lihaihui, sudo, video(44), render(111)
```

| 节点类 | 需要的组 | 缺了会怎样 | 状态 |
|--------|---------|-----------|------|
| `/dev/video*`、`/dev/mpp_service`、`/dev/rga` | `video` | 摄像头打不开 | ✅ 已加入 |
| `/dev/dri/renderD*` | `render` | NPU 推理失败 | ✅ 已加入 |

已执行：`sudo usermod -aG video,render lihaihui` + 重新登录
实测验证（免 sudo）：`v4l2-ctl -d /dev/video10 --list-formats-ext` ✅；读写 `/dev/dri/renderD128` ✅

> 💡 **要点**：`usermod` 改的是 `/etc/group`，但**已运行的进程仍持有旧凭据**。
> 必须重新登录（或重启服务）才能生效。VS Code Server 这类常驻进程也需重启。

### 2.3 NPU 状态（实测）

- 内核配置：`CONFIG_ROCKCHIP_RKNPU=y`（已内置）
- 驱动版本：**rknpu 0.9.8 / 20240828**
- 平台设备：`/sys/bus/platform/devices/fde40000.npu`
- IOMMU：已启用（`iommu mode`）
- ⚠️ 启动日志有两条警告：`can't request region for resource [mem 0xfde40000-...]`、`error -ENXIO: IRQ npu_irq not found`，但驱动仍报告初始化成功。**待阶段 3 用 SDK demo 实测确认**

### 2.4 USB 拓扑与物理接口（2026-09-10 实测 + 用户核实）

> ⚠️ **重要教训**：`/sys/bus/usb/devices/usbN/speed` 只反映**控制器（HCD）的能力**，
> **不代表板上有对应的物理座子**。控制器存在 ≠ 座子存在。详见 `knowledge-log.md` K-019。

**控制器拓扑（实测）**

| 根 hub | 速率 | 驱动 | 父控制器节点 | dr_mode |
|--------|------|------|-------------|---------|
| usb2 | 5000 Mbps (3.00) | xhci-hcd | `usbdrd/usb@fcc00000` | `otg` |
| usb1 | 480 Mbps (2.00) | xhci-hcd | `usbhost/usb@fd000000` | `host` |
| usb3 / usb5 | 480 Mbps (2.00) | ehci-platform | `fd800000.usb` / `fd880000.usb` | host |
| usb4 / usb6 | 12 Mbps (1.10) | ohci-platform | `fd840000.usb` / `fd8c0000.usb` | host |

**物理接口（板卡资料确认）**

| 接口 | 版本 | 位置 | 备注 |
|------|------|------|------|
| Type-A 座子 | **USB 2.0** | 板边（丝印 `USB2.0`） | ✅ **摄像头插这里** |
| USB 3.0 | 3.0 | **39PIN 高速信号扩展接口**（未接出座子） | 需专用转接板，**不建议**为摄像头折腾 |
| Type-C | — | 板边 | ⚠️ **仅供电（5V 3A）**，不是数据口 |

> 📌 **结论：本项目摄像头走 USB 2.0。** 这使 **MJPEG 支持从「建议」升级为「必需」**。

**带宽估算（USB 2.0，UVC 等时传输实际可用约 24-30 MB/s）**

| 分辨率 / 格式 | 带宽 | 可行性 |
|--------------|------|--------|
| 640×480 YUYV@30 | 18.4 MB/s | ⚠️ 勉强，占了 60%+ 带宽 |
| 640×480 MJPEG@30 | ~2-3 MB/s | ✅ 轻松 |
| 1280×720 YUYV@30 | 55 MB/s | ❌ **不可能** |
| 1280×720 MJPEG@30 | ~3-6 MB/s | ✅ 可行 |

**如何确认设备挂在哪个根 hub**
```bash
lsusb -t          # 树状图，能看出设备挂在哪个根 hub 下
```

> ⬜ **待实测**：插上摄像头后用 `lsusb -t` 确认实际挂载点，并记入本表。

---

### 2.5 已持有的 USB 摄像头（2026-09-10 实测）

**结论：完全可用，无需采购。本项目摄像头就此确定。**

| 项 | 值 |
|----|-----|
| 设备名 | `USB Camera`（标识 `DHZJ-221209-X`） |
| VID:PID | `b249:b081` |
| USB 版本 | 2.00（High-Speed，480Mbps） |
| 挂载点 | `usb3` 根 hub（`ehci-platform` / `fd800000.usb`），即**板载 Type-A** |
| 取电 | `bMaxPower = 500mA`（单口上限，**不宜再同口挂其他耗电设备**） |
| 驱动 | `uvcvideo`，自动识别 |
| USB 类 | `bInterfaceClass=14 (Video)` + `bInterfaceSubClass=1/2` → **标准 UVC** ✅ |
| 设备节点 | **`/dev/video10`（视频流）** / `/dev/video11`（metadata，无格式） |

**支持的格式（`v4l2-ctl --list-formats-ext`）**

| 格式 | 分辨率 | 帧率 |
|------|--------|------|
| **MJPG** | 1600×1200 / 1280×720 / **640×480** | **均 30 fps** |
| YUYV | 1600×1200 | 5 fps |
| YUYV | 1280×720 | 10 fps |
| YUYV | 640×480 | 30 fps |

> 📌 YUYV 高分辨率下帧率暴跌，是 **USB 2.0 带宽瓶颈**的直观体现；MJPG 不受影响。

**实测帧率（`--stream-mmap --stream-count=N`）**

| 格式/分辨率 | 100 帧短测 | 300 帧长测（稳态） |
|------------|-----------|-------------------|
| MJPG 640×480 | 25.72 fps | **30.23 fps** |
| YUYV 640×480 | 27.67 fps | — |
| MJPG 1280×720 | 29.96 fps | — |

> ⚠️ **短测帧率偏低是正常现象**，见 `knowledge-log.md` K-020（UVC 预热期）。

**可调控制项（阶段 3 调优的关键资本）**

```
User Controls : brightness, contrast, saturation, hue, gamma, sharpness,
                backlight_compensation, white_balance_automatic,
                white_balance_temperature(2800-6500), power_line_frequency(50Hz)
Camera Controls: auto_exposure(当前=3 Aperture Priority)
                exposure_time_absolute(78-10000, 当前=312 ≈ 31.2ms)
                focus_absolute + focus_automatic_continuous(当前=1 → 自动对焦)
```

> ✅ 控制项完整，**含自动对焦**（比同价位定焦摄像头更好）。
> 阶段 3 可通过**锁定曝光**（切 `auto_exposure=1` 手动模式）消除画面忽明忽暗导致的检测框抖动。

**抓帧验证**
```bash
sudo v4l2-ctl -d /dev/video10 \
  --set-fmt-video=width=640,height=480,pixelformat=MJPG \
  --stream-mmap --stream-count=1 --stream-to=/tmp/cam_test.jpg
# 实测：36585 字节，魔数 ff d8 ff（JPEG SOI）✅
```

---

### 2.6 串口（UART）（2026-09-12 实测）

> 这一节直接决定阶段 1.3（UART 编程）与阶段 2（ESP32 通信）怎么做。

**设备树中 10 个 UART 控制器，只启用了 2 个**

| 设备树节点 | MMIO 地址 | status | 对应设备节点 | 次设备号 |
|-----------|----------|--------|-------------|---------|
| `serial@fe650000` | 0xfe650000 | **okay** | `/dev/ttyS1` | 65 |
| `serial@fe670000` | 0xfe670000 | **okay** | `/dev/ttyS3` | 67 |
| `serial@fdd50000` | — | disabled | — | — |
| `serial@fe660000` | — | disabled | — | — |
| `serial@fe680000` ~ `fe6d0000` | — | disabled | — | — |

> ⚠️ **`/dev/ttyS0`、`/dev/ttyS2` 不存在**——对应控制器没启用，没有驱动去创建设备节点。

**属性**

| 项 | 值 |
|----|-----|
| 驱动 | `dw-apb-uart`（DesignWare APB UART），兼容 16550A |
| 属主/属组 | `root:dialout`，权限 `crw-rw----` |
| 次设备号规则 | `次设备号 = 64 + 端口号`（8250 驱动惯例） |

⚠️ **访问串口需要加入 `dialout` 组**（不是 `tty`！）：
```bash
sudo usermod -aG dialout lihaihui   # 之后需重新登录
```

**console 不会占用户串口**

```
Kernel command line: ... console=ttyFIQ0
```
console 是 **`ttyFIQ0`**（Rockchip 专用 FIQ 调试串口，主设备号 253），**不占用 ttyS1/ttyS3**。

**⬜ 待确认（阶段 1.1）**
- [ ] **哪个 UART 引到了 40PIN/排针？** 需查板卡原理图
- [ ] 蓝牙 `hci0` 是否占用 ttyS1（`BT_RFKILL` 日志提到 `uart_rts_gpios=77`）
- [ ] `ttyS1` 的 `failed to request DMA, use interrupt mode` 警告是否影响高波特率
- [ ] 排针 UART 电平（**必须万用表实测**）

---

## 3. 待确认的硬件信息（阶段 1.1 必须查清）

> ⚠️ 以下**尚未确认**，动手接线前必须逐条实测。

| # | 待确认项 | 怎么查 | 风险 |
|---|---------|--------|------|
| 1 | 排针上可用的 UART 是哪一组？设备节点是 `/dev/ttyS?` | `ls /dev/ttyS*` + `sudo cat /sys/kernel/debug/gpio` + 查板卡原理图 | 用错节点收不到数据 |
| 2 | 该 UART 是否被调试串口占用 | `cat /proc/cmdline` 看 `console=`；`dmesg \| grep ttyS` | 冲突导致系统日志污染串口 |
| 3 | 排针 UART 电平（3.3V 还是 5V） | **万用表实测** | 5V 直连 ESP32 会损坏 GPIO |
| 4 | 摄像头买回后的设备节点与像素格式 | `v4l2-ctl --list-devices`；`v4l2-ctl -d /dev/videoN --list-formats-ext` | 格式不支持则无法采集 |
| 5 | 可用 GPIO 编号（用于驱动实验） | `sudo cat /sys/kernel/debug/gpio`；查原理图 | 复用冲突 |
| 6 | 板端是否有 `librknnrt.so` 及其版本 | `find / -name "librknnrt*" 2>/dev/null` | 与 PC 端 RKNN-Toolkit2 版本不匹配则推理失败 |

---

## 4. ESP32 侧（待实测）

| 项 | 值 | 备注 |
|----|-----|------|
| 板型 | 待确认具体型号 | 用户手头已有 |
| UART 引脚 | 计划 GPIO17(TX) / GPIO16(RX) | ⚠️ 避开 GPIO6-11（接 flash） |
| 下载/调试串口 | 通常是 UART0（GPIO1/GPIO3） | 不要占用 |
| 电平 | 3.3V | 与 RK3566 共地必需 |

### 外设接线规划（待实施）

| 外设 | 接口 | ESP32 引脚（计划） | 备注 |
|------|------|-------------------|------|
| HC-SR501 PIR | GPIO 输入 + 中断 | GPIO4 | 输出 3.3V，去抖 50ms |
| 有源蜂鸣器 | GPIO 输出 | GPIO5 | 有源=给电平就响 |
| LED 红/绿 | GPIO 输出 | GPIO18 / GPIO19 | 串 220Ω |
| SG90 舵机 | LEDC PWM 50Hz | GPIO13 | **必须独立 5V 供电** |
| SSD1306 OLED | I2C | SDA=GPIO21, SCL=GPIO22 | 0x3C 地址 |
| 与 RK3566 通信 | UART1 | TX=GPIO17, RX=GPIO16 | 115200 8N1 |

> ⚠️ **共地是必须的**：ESP32 GND ↔ RK3566 GND ↔ 独立 5V 电源 GND，三者共地。
> ⚠️ SC905 舵机堵转电流可达 1A+，**绝不能从板子 5V 取电**，会导致 ESP32 复位。

---

## 5. 接线图（UART 交叉连接）

```
┌─────────────┐                          ┌──────────────────┐
│   ESP32     │                          │    RK3566        │
│             │                          │                  │
│ GPIO17 (TX) ├─────────────────────────►│ UART_RX (排针)   │
│ GPIO16 (RX) ◄─────────────────────────┤ UART_TX (排针)   │
│ GND         ├─────────────────────────┤ GND              │
└─────────────┘                          └──────────────────┘
                          （共地）
```

**接线检查清单（每次接线前过一遍）**：
1. [ ] TX ↔ RX 交叉（不是 TX↔TX）
2. [ ] GND 已连接（不共地 = 乱码或无数据）
3. [ ] 电平匹配（3.3V vs 5V，不确定就实测）
4. [ ] 舵机/大电流外设独立供电
5. [ ] 上电前用万用表确认无短路

---

## 6. 采购清单状态

见 [`docs/SPEC.md`](../../../docs/SPEC.md) §4.3 BOM。

**当前进度**：全部外设待购（RK3566 板与 ESP32 已持有）。

---

## 维护规则

- 每确认一条信息，从「待确认」移到对应小节，**必须标注实测日期**
- 记录时写明**命令或工具**（便于复查）
- 凡是「听说」「大概」的信息，一律放「待确认」，不放进事实区
