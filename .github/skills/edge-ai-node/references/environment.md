# 开发环境与工具链事实库

> 记录**已验证可用**的命令与环境版本。凡未验证的写进「待确认」。

---

## 1. 开发主机布局

| 主机 | 用途 | 说明 |
|------|------|------|
| **RK3566 板卡**（`lckfb`） | 板端开发、运行、调试 | VS Code Server 直连，每个终端都在板子上跑 |
| **x86 主机**（`lihaihui-virtual-machine`） | 内核交叉编译（已完成 2026-09-04）；后续 RKNN 模型转换 | 性能强，适合跑 RKNN-Toolkit2 |

> ⚠️ 当前 VS Code 直接连在板卡上，**终端命令默认在板端执行**。
> 涉及交叉编译的命令需要 SSH 到 x86 主机。

---

## 2. 板端环境（RK3566）

| 项 | 值 | 验证命令 |
|----|-----|---------|
| 系统 | Ubuntu 22.04.5 LTS (aarch64) | `lsb_release -a` |
| 内核 | 6.1.141 | `uname -a` |
| 编译器 | **gcc 11.4.0** (Ubuntu 11.4.0-1ubuntu1~22.04.3) | `gcc --version` |
| make | ✅ 已装 | `make --version` |
| 内核头文件 | ✅ `/lib/modules/6.1.141/build` 存在 | `ls -d /lib/modules/$(uname -r)/build` |
| git | 已安装（2026-09-09） | `git --version` |
| lsusb | ✅ 已装 (`usbutils`) | `command -v lsusb` |
| v4l2-ctl | ✅ 已装 (`v4l-utils`) | `command -v v4l2-ctl` |
| uvcvideo | ✅ 内置已注册（`CONFIG_USB_VIDEO_CLASS=y`） | `ls -d /sys/module/uvcvideo` |
| NPU 驱动 | ✅ rknpu 0.9.8 已加载（`CONFIG_ROCKCHIP_RKNPU=y`） | `dmesg \| grep -i rknpu` |

> ✅ = 2026-09-10 实测确认。未标记者尚未验证。

### 已完成的系统配置（勿重复操作）

1. **zram 1G 交换** — systemd 服务 `/etc/systemd/system/zram-swap.service`（zstd，1G），已 enabled
   ```bash
   # 手动启用（若服务失效）
   sudo zramctl -a zstd -s 1024M /dev/zram0
   sudo mkswap /dev/zram0
   sudo swapon -p 100 /dev/zram0
   ```
2. **TF 卡自动挂载** — fstab 条目：
   ```
   UUID=465007ea-1b9d-4554-9f94-5c8903745f05 /mnt/tfcard ext4 defaults,noatime,nofail 0 2
   ```
3. **`~/Downloads` 软链** → `/mnt/tfcard/Downloads`

### 磁盘空间

- `/` 12G，剩余约 6.3G
- 占用大头：`/usr/lib/aarch64-linux-gnu` 1.7G、`.vscode-server` 1.2G、`/var/lib` 656M
- 💡 优化项：把 `~/.vscode-server` 迁到 TF 卡可释放 1.2G（阶段 3 内存紧张时再考虑）

---

## 3. 内核编译（已完成，供复查）

| 项 | 值 |
|----|-----|
| 编译主机 | `lihaihui-virtual-machine`（x86） |
| 内核版本 | 6.1.141 |
| 完成日期 | 2026-09-04 |
| 交叉工具链 | 待补充（记录具体前缀，如 `aarch64-linux-gnu-`） |
| 配置来源 | 待补充（defconfig 文件路径） |

> 📌 **阶段 3 前置**：确认内核配置包含 NPU 支持
> ```bash
> zcat /proc/config.gz | grep -i rknpu   # 或 /boot/config-$(uname -r)
> ```
> ✅ **2026-09-10 实测：`CONFIG_ROCKCHIP_RKNPU=y` 已内置，驱动 0.9.8 已加载。**
> ⚠️ 但设备节点是 `/dev/dri/renderD128`（不是 `/dev/rknpu`），且 `dmesg` 有两条警告，待阶段 3 实测。

---

## 4. 常用命令速查

### 内核模块开发
```bash
# 编译（板端原生编译，需内核头文件）
make -C /lib/modules/$(uname -r)/build M=$PWD modules

# 加载 / 查看 / 卸载
sudo insmod edge_ai_evt.ko
lsmod | grep edge_ai
sudo rmmod edge_ai_evt

# 查看模块打印
dmesg -w | tail -n 30
```

### 串口调试
```bash
# 配置并监听
stty -F /dev/ttyS3 115200 raw -echo
cat /dev/ttyS3 | xxd

# 发送测试数据
echo -ne '\xAA\x55...' > /dev/ttyS3

# 查看串口是否被占用
sudo fuser -v /dev/ttyS3
ls -l /proc/$(pgrep -f edge_ai_node)/fd | grep ttyS
```

### 摄像头
```bash
v4l2-ctl --list-devices
v4l2-ctl -d /dev/video0 --list-formats-ext
v4l2-ctl -d /dev/video0 --all
# 抓一帧测试
v4l2-ctl -d /dev/video0 --set-fmt-video=width=640,height=480,pixelformat=MJPG \
         --stream-mmap --stream-count=1 --stream-to=test.jpg
```

### 交叉编译（x86 主机上）
```bash
aarch64-linux-gnu-gcc -o hello hello.c
file hello          # 确认是 ARM aarch64
```

### 性能测量
```bash
top -H -p $(pgrep edge_ai_node)      # 线程级 CPU 占用
ps -o rss= -p <pid>                   # 内存 RSS (KB)
time ./edge_ai_node                   # 总耗时
perf stat ./edge_ai_node              # 更细的计数器（若已安装）
```

---

## 5. 待确认 / 待安装

| # | 项 | 检查命令 | 何时需要 |
|---|-----|---------|---------|
| 1 | 交叉工具链具体前缀与路径（在 x86 主机） | `which aarch64-linux-gnu-gcc` | 阶段 0.3 |
| 2 | `libgpiod` / `gpiod` 工具 | `which gpiodetect gpioinfo` | 阶段 1.1 |
| 3 | `librknnrt.so` 与版本 | `find / -name "librknnrt*" 2>/dev/null` | 阶段 3 |
| 4 | RKNN-Toolkit2 环境（x86） | `python3 -c "import rknn"` | 阶段 3 |
| 5 | ESP-IDF 是否安装 | `idf.py --version` | 阶段 2 |
| 6 | ~~用户是否已加入 video/render 组~~ | ✅ **2026-09-10 已完成** | — |
| 7 | 用户是否已加入 **`dialout`** 组（串口访问用，注意不是 `tty`） | `groups` | **阶段 1.3 前必须做** |

> 2026-09-10 已确认：gcc 11.4.0、make、内核头文件、lsusb、v4l2-ctl 均已就绪（见 §2 表）。

---

## 6. 环境变更日志

| 日期 | 变更 | 影响文件 |
|------|------|---------|
| 2026-09-04 | 内核 6.1.141 自编译完成 | 启动链 |
| 2026-09-06 | zram 1G 启用；TF 卡格式化并自动挂载 | fstab、systemd |
| 2026-09-09 | 安装 git | — |
| 2026-09-10 | 建立项目文档体系与 skill；实测确认 uvcvideo / RKNPU 0.9.8 已就绪；发现 NPU 节点为 `/dev/dri/renderD128` | `docs/`、`.github/skills/` |

---

## 维护规则

- 环境**任何变更**后，追加到 §6 变更日志
- 记录准确版本号，不写「最新版」
- 新装工具后，把「待确认」项移到对应小节并注明日期
