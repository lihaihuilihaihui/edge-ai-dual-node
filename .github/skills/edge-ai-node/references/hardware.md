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
# ⚡ 不需要 sudo：节点为 root:video 660，用户在 video 组
v4l2-ctl -d /dev/video10 \
  --set-fmt-video=width=640,height=480,pixelformat=MJPG \
  --stream-mmap --stream-count=1 --stream-to=/tmp/cam_test.jpg
# 实测（2026-09-10）：36585 字节，魔数 ff d8 ff（JPEG SOI）✅
```

**⚠️ 用 `v4l2-ctl` 必须带 `-d /dev/video10`**

不带 `-d` 时默认操作 **`/dev/video0`**（RK3566 内置 ISP，MIPI CSI 路径），输出**看起来正常但看的是另一个设备**。
另：`video0`~`video9` 全部属于 RKISP，本项目**不用**（没接 MIPI 摄像头）。→ K-040、K-041

**⭐ 抓帧实测数据（2026-10-03，自写 `capture.c`）**

| 项 | 值 |
|----|-----|
| `REQBUFS` 返回 | 4 个 buffer |
| `length`（buffer 容量） | **614400** = 640×480×2（**按未压缩尺寸分配**） |
| `bytesused`（实际帧） | **54716** 字节 |
| 压缩比 | `614400 / 54716 ≈ **11.2 倍**` |
| 带宽 | `54716 × 30 ≈ **1.64 MB/s**` |
| 同分辨率 YUYV（理论） | 18.4 MB/s（**差 11 倍**） |
| 平面类型 | `V4L2_BUF_TYPE_VIDEO_CAPTURE`（**单平面**，不是 MPLANE） |

> 📌 写文件必须用 `buf.bytesused`，**不能用 `buf.length`**（否则尾部有近 560KB 垃圾）→ K-042
> 📌 `S_FMT` / `REQBUFS` 是**传入传出**，必须用返回的实际值 → K-043

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
> ✅ 2026-09 已加入 `dialout`，实测**普通用户可直接访问 `/dev/ttyS3`，不需要 `sudo`**。

**⭐ 内部回环可用（2026-10-08 实测）—— 不需要外部导线就能做自环测试**

| 项 | 结果 |
|----|------|
| 芯片类型 | `16550A`（8250 兼容，见 `/proc/tty/driver/serial`） |
| 内部回环 | ✅ **可用** —— `ioctl(fd, TIOCMBIS, &TIOCM_LOOP)` 调用成功 |
| 实测回环 | 发出 `"HELLO"`（5 字节）→ 读回 `"HELLO"`，**内容完全一致** |
| **意义** | **1.4 的"1000 帧 0 误码"验收可在这块板子上直接完成**，不需要杜邦线 |

**原理**：8250 系列 UART 的 MCR 寄存器有 `LOOP` 位，置位后**芯片内部把 TX 直接接回 RX**。
`TIOCM_LOOP` **不在** glibc 头文件里（它是内核扩展宏，定义在 `<asm-generic/termios.h>`），
需要手动 `#define TIOCM_LOOP 0x8000` → 详见 **K-050**。

**`/proc/tty/driver/serial` 实测（2026-10-08）**

```
1: uart:16550A mmio:0xFE650000 irq:35 tx:52304 rx:2447 RTS|CTS|DTR   ← Bluetooth 在用
3: uart:16550A mmio:0xFE670000 irq:36 tx:0     rx:0     CTS          ← 干净，从未使用
```

> ⭐ `tx:0 rx:0` 证明 ttyS3 **自开机以来一个字节都没收发过** —— 排针 UART 是独占可用的。
> （对比 ttyS1 的 52304/2447，那是 Bluetooth 的流量。）

**开机默认 termios 配置（2026-10-08 实测）**

```
speed 9600 baud
cs8 -cstopb -parenb cread clocal -crtscts          ← 硬件参数默认就是对的 ✅
-igncr icrnl ixon ...                              ← ⚠️ 输入转换/软件流控开着
opost ... onlcr ...                                ← ⚠️ 输出转换开着
isig icanon echo ...                               ← ⚠️ 终端行为全开着
```

> ⚠️ **做二进制协议时这 7 个开关必须全部关掉**（`cfmakeraw()` 一行搞定），
> 否则会出现"帧内容被篡改""帧长度变了""程序被 Ctrl+C 杀掉""通信卡死"等诡异现象。
> 原理与案例见 `stage1-rk3566/docs/1.4-serial-notes.md` §二。

**console 不会占用户串口**

```
Kernel command line: ... console=ttyFIQ0
```
console 是 **`ttyFIQ0`**（Rockchip 专用 FIQ 调试串口，主设备号 253），**不占用 ttyS1/ttyS3**。

**✅ 已确认（2026-09-20 实测，09-12 遗留问题闭环）**

| 控制器 | 地址 | 设备节点 | 占用的引脚 | 用途 | 可用？ |
|--------|------|---------|-----------|------|--------|
| `uart1` | `fe650000` | `/dev/ttyS1` | `gpio2-11/12/13/14` | **蓝牙 hci0**（`wireless-bluetooth`，含 RTS/CTS） | ❌ 不可用 |
| `uart2` | `fe660000` | — | `gpio0-24/25` | **console（`fiq-debugger`）** | ❌ 绝对不可碰 |
| `uart3` | `fe670000` | **`/dev/ttyS3`** | `gpio3-15/16` | ★ **排针引脚 8(TX) / 10(RX)** | ✅ **阶段 2 专用** |

- ✅ **哪个 UART 引到排针** → **`ttyS3`**（对应排针引脚 8 / 10）——09-12 遗留问题解决
- ✅ **蓝牙是否占用 ttyS1** → **是**（`wireless-bluetooth` 独占 uart1）——09-12 遗留问题解决

**⬜ 仍未确认**
- [ ] `ttyS1` 的 `failed to request DMA, use interrupt mode` 警告是否影响高波特率
- [ ] 排针 UART 电平（**必须万用表实测**；决定是否需要电平转换）

---

### 2.7 40PIN 排针引脚分配（2026-09-20 实测 + 原理图交叉验证）

> **数据来源**：板卡原理图引脚功能表 + 内核实测（`/sys/kernel/debug/pinctrl/*/pinmux-pins`）——**两者交叉验证一致** ✅

#### ★ 本项目选定 / 保留的引脚

| 排针引脚 | 引脚名 | 全局编号 | gpiochip:line | 分配 |
|---|---|---|---|---|
| **11** | `GPIO3_A1` | 97 | **`gpiochip3:1`** | ★ **本项目 GPIO 实验/自定义 IO** |
| **8 / 10** | `GPIO3_B7` / `GPIO3_C0` | 111 / 112 | `gpiochip3:15 / 16` | ★ **阶段 2 串口（`/dev/ttyS3`）——保留禁用** |

#### 可用 GPIO（实测 `MUX UNCLAIMED` + `GPIO UNCLAIMED`）

| 排针 | 引脚名 | 全局编号 | `gpiochip3` line |
|---|---|---|---|
| **11** | `GPIO3_A1` | 97 | **1** ★ |
| 13 | `GPIO3_A2` | 98 | 2 |
| 15 | `GPIO3_A3` | 99 | 3 |
| 16 | `GPIO3_A4` | 100 | 4 |
| 18 | `GPIO3_A5` | 101 | 5 |
| 22 | `GPIO3_A6` | 102 | 6 |
| 26 | `GPIO3_A7` | 103 | 7 |
| 29 | `GPIO3_B0` | 104 | 8 |
| 38 | `GPIO3_B3` | 107 | 11 |
| 40 | `GPIO3_B4` | 108 | 12 |
| 36 | `GPIO3_C3` | 115 | 19 |

> 其它可用的非 gpio3 引脚：**7 = `GPIO1_A4`**、**37 = `GPIO0_B7`**

#### ⚠️ 已被内核占用（**不可动用**）

| 排针 | 引脚名 | 占用者 | 说明 |
|---|---|---|---|
| **8 / 10** | `GPIO3_B7` / `GPIO3_C0` | **`uart3` (ttyS3)** | ★ 阶段 2 专用，保留 |
| 3 / 5 | `GPIO0_B6` / `GPIO0_B5` | `i2c2` | I2C2 总线 |
| 27 / 28 | `GPIO3_B6` / `GPIO3_B5` | `i2c3` | I2C3 总线 |
| 12 | `GPIO3_C4` | `pwm14` | PWM14 |
| 32 | `GPIO3_C5` | `pwm15` | PWM15 |
| 33 / 35 | `GPIO3_B1` / `GPIO3_B2` | `pwm8` | PWM8 |
| 19 / 21 / 23 / 24 | `GPIO4_*` | `spi3` | SPI3 总线 |

#### 电源与地

| 引脚 | 功能 |
|---|---|
| 1 / 17 | 3.3V |
| 2 / 4 | 5V |
| 6 / 9 / 14 / 20 / 25 / 30 / 34 / 39 | GND |

#### Rockchip 引脚编号换算（实用）

```
GPIO3_A1  →  bank=3, 组=A(组序0), 组内序号=1
全局编号 = bank×32 + 组序×8 + 序号 = 3×32 + 0×8 + 1 = 97
gpiochip3 的 line = 97 − 96 = 1          （96 = bank3 的全局起始编号）
```
组序：`A=0, B=1, C=2, D=3`

#### 探测方法（可复现）

```bash
# ★ 每个引脚的复用归属（唯一能看出"真的空闲"的命令）
sudo cat /sys/kernel/debug/pinctrl/*/pinmux-pins

# 只看某个 bank
sudo cat /sys/kernel/debug/pinctrl/*/pinmux-pins | grep -E "\(gpio3-"

# GPIO 线的占用者（⚠️ unused ≠ 引脚空闲！UART/I2C 不走 gpiod 接口）
sudo gpioinfo gpiochip3

# 统计真正空闲的线数（-w 全词匹配，否则 "unused" 会被 "used" 误匹配）
sudo gpioinfo | grep -cw unused
```

> ⚠️ **判断引脚能否使用，必须看 `pinmux-pins` 里的 `UNCLAIMED`**；
> 只看 `gpioinfo` 的 `unused` 会得出错误结论（UART/I2C/PWM 子系统不通过 gpiod 接口申请引脚）。

---

## 3. 待确认的硬件信息

| # | 待确认项 | 怎么查 | 风险 | 何时必须做 |
|---|---------|--------|------|-----------|
| 1 | **排针 UART 电平（3.3V 还是 5V）** | **万用表实测** | 5V 直连 ESP32 会损坏 GPIO | ⚠️ **阶段 2 接线前** |
| 2 | `ttyS1` 的 DMA 警告是否影响高波特率 | 长时间压测收发 | 高波特率下丢数据 | 阶段 2 |
| 3 | 板端 `librknnrt.so` 版本 | `find / -name "librknnrt*" 2>/dev/null` | 与 PC 端 RKNN-Toolkit2 不匹配则推理失败 | 阶段 3 |

**✅ 已确认（移出本表）**：

| 原待确认项 | 结论 | 日期 |
|-----------|------|------|
| 排针上可用的 UART | **`/dev/ttyS3`**（对应排针引脚 8 / 10） | 2026-09-20 |
| 是否被 console 占用 | **否**——console 是 `ttyFIQ0`（uart2），不占 ttyS1/ttyS3 | 2026-09-12 |
| 摄像头设备节点与格式 | `/dev/video10`，MJPG 640×480@30fps 稳态 | 2026-09-10 |
| 可用 GPIO 编号与复用状态 | 见 §2.7；选定 **排针 11 = `GPIO3_A1` = `gpiochip3:1`** | 2026-09-20 |

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
