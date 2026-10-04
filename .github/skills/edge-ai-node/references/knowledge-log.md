# 踩坑记录库（Knowledge Log）

> **排错前先来这里搜**。解决非平凡问题后必须追加一条。
> Copilot 每次新会话应先浏览本文件，避免重复踩坑。

---

## 使用方法

1. **排错前**：用关键词搜本文件（`grep -i "EAGAIN" knowledge-log.md`）
2. **解决后**：按下面模板追加，编号连续
3. **区分来源**：`【实测】`= 本项目亲自踩过；`【通用】`= 已知的常见陷阱，尚未在本项目遇到

### 条目模板

```markdown
### K-xxx 【实测】一句话概括问题
- **日期**：YYYY-MM-DD
- **现象**：具体的错误输出/异常行为（贴原始报错）
- **根因**：为什么会这样（讲机制，不是"改了就好了"）
- **解法**：具体怎么改
- **验证**：怎么确认修好了
- **关联**：涉及的文件/文档
```

---

## 一、串口 / UART 通信

### K-001 【通用】串口收到的帧偶尔错位，字段值莫名其妙

- **现象**：解析出的 CMD 是随机值，CRC 校验失败，但下一帧又对了
- **根因**：**粘包**。串口是字节流，不是消息流。一次 `read()` 可能返回 1.5 个帧，也可能返回半个帧。如果按"读一次 = 一帧"处理，必然错位
- **解法**：改成**逐字节喂状态机**。维护 `parser_state_t`，每来 1 字节推进一次状态，凑满完整帧才投递。CRC 失败时回退到 `ST_SOF0` 并从当前字节重新扫描 `0xAA`
- **验证**：写压测脚本一次性连发 5 帧，全部正确解析
- **关联**：`docs/SPEC.md` §5.3

### K-002 【通用】串口读不到裸字节，每次要按回车才有数据

- **现象**：`read()` 阻塞，只有发送方发换行符时才返回
- **根因**：termios 默认工作在**规范模式（canonical mode）**，行缓冲，且会处理特殊字符。这是给终端用的，不是给设备通信用的
- **解法**：
  ```c
  tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);  // 关掉规范模式和回显
  tty.c_iflag &= ~(IXON | IXOFF | IXANY);          // 关流控
  cfmakeraw(&tty);                                  // 或者直接用这个宏
  ```
- **验证**：发送单字节 `0xAA`，`read()` 立即返回 1 字节
- **关联**：`serial.cpp`

### K-003 【通用】RK3566 与 ESP32 串口通信全是乱码

- **现象**：能收到数据但全是乱码
- **根因**：三个可能，按概率排序：① **没共地** ② 波特率不匹配 ③ TX/RX 没交叉
- **解法**：按顺序排查——先用万用表量两地 GND 是否导通；再确认两端都是 115200；最后确认 TX↔RX 交叉
- **验证**：`cat /dev/ttyS3 | xxd` 能看到规律的帧头 `AA 55`
- **关联**：`references/hardware.md` §5 接线检查清单

---

## 二、摄像头 / V4L2

### K-004 【通用】`VIDIOC_DQBUF` 返回 -1，errno=11 (EAGAIN)

- **现象**：连续采集若干帧后取帧失败
- **根因**：V4L2 用**缓冲队列**。应用 `VIDIOC_QBUF` 把 buffer 交给驱动，驱动填好数据后应用 `VIDIOC_DQBUF` 取回。**如果不把取回的 buffer 重新 QBUF 归还，队列会耗尽**
- **解法**：典型流程是 申请 N 个 buffer → 全部 QBUF（入队）→ `STREAMON` → 循环 `DQBUF` 处理后立刻 `QBUF` 归还
- **验证**：连续采集 1000 帧无失败
- **关联**：`camera.cpp`

### K-005 【通用】摄像头打不开：`Device or resource busy`

- **现象**：`open("/dev/video0")` 返回 EBUSY
- **根因**：已有进程占用（比如残留的上次测试进程，或桌面环境的摄像头服务）
- **解法**：`sudo fuser -v /dev/video0` 找出占用进程；`ls -l /proc/<pid>/fd | grep video` 确认；杀掉后重试
- **验证**：`v4l2-ctl --list-devices` 能正常枚举

### K-006 【通用】采集帧率远低于预期

- **现象**：设置 30fps，实际只有 5fps
- **根因**：常见原因：① YUYV 格式 640×480@30 需要 ~18MB/s 带宽，USB 2.0 可能不够 ② 用了 `read()` 而非 `mmap`，多一次拷贝 ③ `VIDIOC_DQBUF` 用了阻塞模式但只有一个 buffer
- **解法**：优先用 MJPEG（压缩，带宽小）；用 `mmap` 内存映射避免拷贝；增加 buffer 数量到 4；确认 USB 是 2.0 还是 3.0
- **验证**：打印每帧间隔时间戳，确认稳定在 33ms 附近

---

## 三、RKNN / NPU

### K-007 【通用】RKNN 初始化失败或模型加载报错

- **现象**：`rknn_init` 返回非 0，或提示找不到 librknnrt
- **根因**：**RKNN-Toolkit2（PC 端转换工具的版本）必须与板端 runtime（librknnrt.so）版本匹配**。版本错配是最常见原因
- **解法**：`find / -name "librknnrt*"` 查板端版本；查 PC 端 `rknn-toolkit2` 版本；两者需在同一大版本内。必要时用 `LD_LIBRARY_PATH` 指定
- **验证**：跑通 SDK 自带的 `rknn_yolov5_demo`
- **关联**：`docs/SPEC.md` §10 风险表

### K-008 【通用】量化后精度掉得厉害

- **现象**：FP16 效果正常，INT8 量化后大量漏检
- **根因**：量化需要**校准数据集（calibration dataset）**代表真实分布。随便找几张图，或校准集太小（<100 张），量化参数会失真
- **解法**：用与部署场景一致的图片做校准集，200-300 张，覆盖不同光照/角度；必要时对特定层保持 FP16
- **验证**：在固定测试集上对比量化前后的 mAP

---

## 四、Linux 驱动 / 内核

### K-009 【通用】insmod 报 `Invalid module format`

- **现象**：模块编译成功但加载失败
- **根因**：模块与当前运行内核**版本不匹配**。内核模块没有稳定 ABI，必须针对目标内核编译
- **解法**：确认 `uname -r` 与 `/lib/modules/$(uname -r)/build` 指向的内核源码版本一致。自编译内核后要重新编译模块
- **验证**：`modinfo edge_ai_evt.ko | grep vermagic`，与 `uname -r` 对比
- **关联**：本项目内核是自编译的 6.1.141，改内核后必须重编模块

### K-010 【通用】驱动加载了但 `/dev` 下没有设备节点

- **现象**：`insmod` 成功，`dmesg` 打印正常，但 `/dev/edge_ai0` 不存在
- **根因**：老式做法是手动 `mknod`；正确做法是在驱动里创建 class + device，让 udev 自动建节点
- **解法**：驱动中用 `class_create()` + `device_create()`；确保运行了 udev
- **验证**：`insmod` 后立刻 `ls -l /dev/edge_ai*` 出现节点
- **关联**：`stage1-rk3566/drivers/edge_ai_evt/`

### K-011 【通用】用户态 read 驱动时阻塞不返回 / 返回错误字节数

- **现象**：`read()` 一直挂着，或返回 -1 且 errno=EINVAL（提示 `copy_to_user` 相关）
- **根因**：`file_operations.read` 的语义是**返回实际拷贝的字节数**，返回正值表示成功。常见错误是 `return 0`（应用会认为是 EOF），或用 `size_t` 接负数返回值
- **解法**：正确返回类型是 `ssize_t`；无数据时应返回 `-EAGAIN`（非阻塞）或使用等待队列睡眠
- **验证**：用户态测试程序能收到预期长度的数据

---

## 五、ESP32 / ESP-IDF

### K-012 【通用】GPIO 中断反复触发（抖动）

- **现象**：PIR 触发一次，中断服务函数进入几十次
- **根因**：机械/电气抖动，或 PIR 模块本身输出在阈值附近震荡
- **解法**：三管齐下——① 硬件上 PIR 有调节电位器（灵敏度/延时）② 中断里**只投递队列**，不做事 ③ 应用层加 50-100ms 去抖时间窗
- **验证**：手挥一次，日志只出现一条事件
- **关联**：FR-01

### K-013 【通用】舵机一动，ESP32 就复位

- **现象**：舵机转动瞬间板子重启
- **根因**：舵机堵转/启动电流可达 1A+，从板子 5V 取电会拉低电压导致欠压复位
- **解法**：**独立 5V 电源给舵机**，且与 ESP32 **共地**；电源就近加 470μF~1000μF 电解电容
- **验证**：舵机连续摆动 100 次不复位
- **关联**：`references/hardware.md` §4

### K-014 【通用】ISR 里不能做的事

- **现象**：中断里调用 `printf` 导致崩溃或看门狗复位
- **根因**：ESP-IDF 中断服务函数（ISR）里**禁止调用非 IRAM 安全的函数**（如 `printf`、`malloc`、大部分 FreeRTOS 非 `FromISR` 版本 API）
- **解法**：ISR 只做最少的事——用 `xQueueSendFromISR()` 或 `xTaskNotifyFromISR()` 把事件丢给任务，实际处理在任务里做
- **验证**：连续触发 1000 次不崩溃

---

## 六、工程 / 工具链

### K-015 【通用】交叉编译出的程序在板子上跑不了（`No such file or directory`）

- **现象**：文件明明存在，执行却说找不到
- **根因**：这是**动态链接器缺失**的误导性报错。板子上没有程序依赖的 `ld-linux-aarch64.so` 或对应版本的 libc
- **解法**：`file ./prog` 确认架构；`aarch64-linux-gnu-readelf -d ./prog` 看依赖；对比板端 toolchain 版本；必要时静态编译 `-static`（调试期可接受）
- **验证**：`file` 显示 `ARM aarch64`，且 `ldd` 在板端无缺项

### K-016 【实测】USB 摄像头插上后，硬编码 `/dev/video0` 拿不到图像
- **日期**：2026-09-10
- **现象**：`v4l2-ctl -d /dev/video0` 无法使用；且即便有权限，`/dev/video0` 也不是 USB 摄像头
- **根因**：RK3566 的 **ISP 平台驱动（rkisp）预先占用了 `/dev/video0`~`/dev/video9`**。实测这 10 个节点的真实身份：
  ```
  video0 rkisp_mainpath    video1 rkisp_selfpath     video2 rkisp_rawwr0
  video3 rkisp_rawwr2      video4 rkisp_rawwr3       video5 rkisp_iqtool
  video6 rkisp_rawrd0_m    video7 rkisp_rawrd2_s     video8 rkisp-statistics
  video9 rkisp-input-params
  ```
  USB 摄像头是后注册的，编号从 **video10 起**
- **解法**：**永远不要硬编码设备节点**。三个层次——
  1. 配置项指定 + 运行时 `VIDIOC_QUERYCAP` 校验 `capabilities & V4L2_CAP_VIDEO_CAPTURE`
  2. 遍历 `/dev/video*`，用 `VIDIOC_QUERYCAP` 的 `bus_info` 判断是否为 `usb-*`
  3. 最稳：udev 规则建固定软链
     ```
     SUBSYSTEM=="video4linux", ATTRS{idVendor}=="046d", SYMLINK+="camera0"
     ```
- **验证**：`v4l2-ctl --list-devices` 能看到摄像头被标注为 USB 设备
- **关联**：`references/hardware.md` §2.1

### K-017 【实测】普通用户访问 `/dev/video*`、`/dev/dri/renderD*` 报 Permission denied
- **日期**：2026-09-10
- **现象**：`v4l2-ctl --list-devices` → `Failed to open /dev/video0: Permission denied`
- **根因**：节点权限是 `crw-rw---- root:video`（video 类）和 `crw-rw---- root:render`（DRM 类）。当前用户只属于 `lihaihui` 和 `sudo`
- **解法**：
  ```bash
  sudo usermod -aG video,render lihaihui
  # 需重新登录（或 newgrp video）使组生效
  ```
  ⚠️ 不要用 `sudo` 跑日常开发——代码以 root 运行会把产物文件属主搞成 root，后续非 sudo 就改不了
- **验证**：`groups` 输出含 video/render，命令不再报权限错误
- **关联**：`references/hardware.md` §2.2

### K-018 【实测】NPU 的设备节点不是 `/dev/rknpu`
- **日期**：2026-09-10
- **现象**：内核 `CONFIG_ROCKCHIP_RKNPU=y`，`dmesg` 显示驱动初始化成功，但 `/dev/` 下没有 `/dev/rknpu`
- **根因**：**新版 rknpu2 驱动（本机 0.9.8 / 20240828）改用 DRM 框架暴露设备**（`CONFIG_ROCKCHIP_RKNPU_DRM_GEM=y`）。实际节点是 **`/dev/dri/renderD128`**
- **解法**：librknnrt 会自动打开 render 节点，无需手动指定。排查时应查 `/dev/dri/renderD*` 而非 `/dev/rknpu*`
- **遗留问题**：`dmesg` 有两条警告——
  ```
  RKNPU fde40000.npu: can't request region for resource [mem 0xfde40000-0xfde4ffff]
  RKNPU fde40000.npu: error -ENXIO: IRQ npu_irq not found
  ```
  但驱动随后仍报告 `[drm] Initialized rknpu 0.9.8 20240828`。**是否影响实际推理，待阶段 3 实测确认**（跑通 SDK demo 才能下结论）
- **关联**：`docs/SPEC.md` §11、`references/hardware.md` §2.1

### K-019 【实测】不要从 sysfs 的 `speed` 反推板上有对应的物理接口
- **日期**：2026-09-10
- **现象**：`/sys/bus/usb/devices/usb2/speed` 显示 `5000`（USB 3.0），据此判断「板载有 USB 3.0 座子」——**结论错误**。实际板载 Type-A 是 USB 2.0，USB 3.0 的 PHY 被引到了 **39PIN 高速扩展接口**，根本没接出座子
- **根因**：`/sys/bus/usb/devices/usbN` 是**控制器（HCD）**的抽象，反映的是 **SoC 的能力**。板厂可以把任意控制器的 PHY 接到任意连接器，也可能完全不做引出。**控制器存在 ≠ 座子存在**
- **解法**：判断物理接口必须三条证据交叉验证——
  1. **板卡原理图 / 板面丝印**（最权威）
  2. `lsusb -t` 看**真实设备**挂在哪个根 hub 下
  3. `/proc/device-tree/*/dr_mode` + `/sys/bus/platform/drivers/{xhci-hcd,ehci-platform,ohci-platform,dwc3}/` 看控制器拓扑
- **验证**：插上一个 USB 设备，用 `lsusb -t` 能看出它挂在哪条总线上
- **教训**：**硬件事实宁可查原理图/问用户，也不要从间接证据推断。** 推断错误会直接导致选型决策出错（本例中差点选错摄像头格式）
- **关联**：`references/hardware.md` §2.4

### K-020 【实测】UVC 摄像头刚启动时帧率明显偏低
- **日期**：2026-09-10
- **现象**：`v4l2-ctl --stream-count=100` 测 MJPG 640×480 只得 **25.72 fps**（逐段输出：16.80 → 23.45 → 25.72，持续爬升）；改成 `--stream-count=300` 长测则稳定在 **30.23 fps**
- **根因**：两个因素叠加——
  1. **自动曝光/自动白平衡收敛需要时间**。UVC 摄像头刚上电时画面偏暗，`exposure_time_absolute` 处在较大值（实测 312 ≈ 31.2ms，已接近 33.3ms 的帧周期），曝光时间长直接拖低帧率；收敛后曝光时间下降，帧率回归
  2. 短测样本太少（100 帧 ≈ 3-4 秒），爬升段占比过大
- **解法**：
  - 测帧率**必须用至少 300 帧**并看输出的**尾部**数值，不能看开头
  - 若需全程稳定帧率：**锁定曝光**（`auto_exposure=1` 手动模式 + 固定 `exposure_time_absolute`），或保证充足照明
- **工程影响**：本项目「端到端延迟 < 800ms」指标必须区分**冷启动**与**稳态**，冷启动需把预热期算进去
- **关联**：`references/hardware.md` §2.5

### K-021 【实测】「Windows 免驱」不等于 Linux 能用——判据是 USB 描述符里的 UVC 类
- **日期**：2026-09-10
- **现象**：手头摄像头商品页只写「Windows 免驱」，无任何 Linux 说明；罗技 C270 页面也不提 Linux 支持。无从判断能否用于 Linux
- **根因**：「Windows 免驱」有两种截然不同的来源——
  1. **真 UVC**：设备实现 USB Video Class 标准接口，Windows 10+ 自带 UVC 驱动
  2. **假免驱**：摄像头把自己枚举成 **U 盘**，里面放着 Windows 驱动安装包。Linux 下只会看到一个 U 盘
  商品页的文字无法区分二者。**真正的判据是 USB 描述符里的 `bInterfaceClass`**
- **解法**：插上后三项验证——
  ```bash
  lsusb                                  # 看是否出现新的 USB 设备
  lsusb -v -d <VID>:<PID> | grep -i bInterfaceClass
  #   bInterfaceClass 14 (Video)   → 标准 UVC，Linux 可用 ✅
  #   bInterfaceClass  8 (Mass Storage) → 驱动盘型，Linux 不可用 ❌
  v4l2-ctl --list-devices                # 看是否新增 video 节点
  ```
  更直接的旁证：`ls /sys/class/video4linux/` 里是否多出节点，节点名是否为摄像头型号
- **验证**：本项目摄像头实测 `bInterfaceClass=14 / SubClass=1,2` → 确认 UVC，`uvcvideo` 自动接管
- **教训**：**不要相信商品页的「免驱」措辞，要看设备描述符。** 罗技等大厂从不为 Linux 做官方背书，但产品通常是标准 UVC，照样能用
- **关联**：`references/hardware.md` §2.5

### K-022 【实测】主设备号对应驱动，次设备号对应设备实例——两者不可混淆
- **日期**：2026-09-12
- **现象**：容易误以为「主设备号对应驱动、次设备号对应第几个驱动」。实际**一个主设备号只对应一个驱动**，次设备号是该驱动管理的**设备实例**编号
- **根因**：内核的字符设备表 `chrdevs[主设备号]` 里**只存一个 `cdev`**（一个驱动）。驱动注册时拿到主设备号，之后自己决定如何把次设备号分配给旗下多个实例
- **证据链**：
  ```
  $ grep 81 /proc/devices
   81 video4linux          ← 主设备号 81 只有一个名字

  $ ls -l /dev/video10
  crw-rw---- 1 root video 81, 13 /dev/video10
  ```
  次设备号 13 是 video4linux 子系统内部按**注册顺序**发的流水号
- **⚠️ 推论：设备文件名里的数字 ≠ 次设备号**。实测：
  ```
  video0      → 81:0        video9      → 81:9
  v4l-subdev0 → 81:10   ← 插队
  v4l-subdev1 → 81:11
  v4l-subdev2 → 81:12
  video10     → 81:13   ← 名字叫 10，次号是 13
  video11     → 81:14
  ```
  更极端的例子：`/dev/ttyS1` 次设备号是 **65**，`/dev/ttyS3` 是 **67**（8250 驱动惯例：`次号 = 64 + 端口号`）
- **解法**：次设备号**只能从 `ls -l` 或 `/sys/class/<子系统>/<设备>/dev` 读**，绝不从文件名推断
- **MCU 类比**：主设备号 ≈ 外设基地址（哪个控制器），次设备号 ≈ 控制器下的第几路通道
- **关联**：`references/hardware.md` §2.6；阶段 1.4 写驱动时需自己 `alloc_chrdev_region()` 申请主次设备号

### K-023 【实测】串口的属组是 `dialout`，不是 `tty`
- **日期**：2026-09-12
- **现象**：`/dev/ttyS1` 的属组是 `dialout`，而 `/dev/ttyFIQ0` 的属组是 `tty`。凭直觉加 `tty` 组是无效的
- **根因**：Ubuntu/Debian 系用 **`dialout`** 组管理串口访问（历史原因，串口最早用于拨号 dial-out）；`tty` 组只管控制终端（console / pts）
- **解法**：
  ```bash
  sudo usermod -aG dialout lihaihui   # 然后重新登录
  ```
- **验证**：免 sudo 执行 `stty -F /dev/ttyS3 115200 raw -echo` 不报权限错
- **关联**：`references/hardware.md` §2.6

### K-024 【实测】`sudo` 也写不进 `/tmp` 里别人的文件——sticky bit + `fs.protected_regular`
- **日期**：2026-09-12
- **现象**：在 `/tmp` 做权限实验，`sudo sh -c 'echo SECRET > /tmp/perm-lab.txt'` 失败：
  ```
  sh: 1: cannot create /tmp/perm-lab.txt: Permission denied
  ```
  诡异点：执行者是 **root**，文件权限是 **666**（全开），仍然被拒
- **根因**：两道机制叠加——
  1. `/tmp` 权限位末尾是 `t`（`drwxrwxrwt`）= **sticky bit 目录**
  2. 内核 `fs.protected_regular = 2`（Ubuntu 22.04 默认）在 sticky 目录内以 `open(O_CREAT)` 打开**已存在且属主不是自己**的普通文件时，直接返回 `EACCES`
  - 该检查**独立于 DAC 权限位**（故 `666` 无效），**且不受 `CAP_DAC_OVERRIDE` 豁免**（故 root 也拦）
- **实测证据**（同一目录，权限均为 666）：
  | 文件属主 | 写入者 | `fs.protected_regular` | 结果 |
  |---|---|---|---|
  | `lckfb` | root | 2 | ❌ EACCES |
  | `root` | root | 2 | ✅ 成功 |
  | `lckfb` | root | 0（临时关闭） | ✅ 成功 |

  → 唯一决定因素是「**文件属主 == 写入者 fsuid**」，与权限位无关
- **设计意图**：防「共享目录投毒」——攻击者在 `/tmp` 预置同名文件，诱使 root 程序以 root 权限写入，从而污染 root 写出的内容
- **解法**：
  1. **权限/文件实验不要用 `/tmp`**（sticky 目录规则多、不干净），改用私有目录 `mkdir -p ~/perm-lab`
  2. 确需写他人文件：先 `sudo chown $USER <file>` 拿到属主身份
  3. 诊断顺序：`ls -ld <目录>`（有没有 `t`）+ `cat /proc/sys/fs/protected_regular`（是不是 2）
- **`sysctl` 取值**：`0`=不保护 / `1`=有限 / `2`=完全（Ubuntu 默认）
- **教训**：**权限模型不止一层**——
  ```
  第1层 DAC 九位（属主/属组/other）
  第2层 sticky bit（目录级：管删除 + 管 O_CREAT）
  第3层 capabilities（CAP_DAC_OVERRIDE 让 root 绕过 DAC）
  第4层 LSM / protected_* 加固（本坑，且不给 root 开后门）
  ```
  **上层检查会在下层放行后继续拦截**。遇到「权限都对却仍 Permission denied」，先查 sticky 位与 `protected_*`
- **关联**：`references/hardware.md` §2.2；K-017；阶段 1.2 驱动实验

### K-025 【实测】`git switch <commit>` 会被拒绝——进入 detached HEAD 必须显式 `--detach`
- **日期**：2026-09-16
- **现象**：想查看某个历史版本的代码，执行 `git switch 2086351` 失败：
  ```
  fatal: a branch is expected, got commit '2086351'
  ```
- **根因**：`git switch`（git 2.23 引入）**只接受分支名**。它被设计出来的一大动机就是防止用户在不明确的情况下进入 **detached HEAD**（HEAD 直接指向 commit、不属于任何分支）。报错原文里 git 自己就把 "branch" 和 "commit" 当作两种东西
- **解法**：显式声明意图
  ```bash
  git switch --detach 2086351     # 或简写 git switch -d
  ```
  验证已进入该状态：
  ```bash
  cat .git/HEAD      # 平时是 "ref: refs/heads/master"；此时变成【裸哈希】
  git status         # 首行变 "HEAD detached at <hash>"
  git log --oneline  # 标签从 "(HEAD -> master)" 变 "(HEAD)"，箭头消失
  ```
  退出：`git switch master`
- **风险**：detached 状态下的提交**不属于任何分支**，切走后即成孤儿（只能靠 `reflog` 捞回）。适合**只读地查看历史代码**，不适合在此开发
- **对比**：`git checkout <commit>` 会**默默**进入 detached 状态且不提示——这正是 `checkout` 被拆分为 `switch` / `restore` 的原因
- **关联**：`docs/session-log.md` 2026-09-16；阶段 1 排查旧版本代码时可能用到

### K-026 【实测】只有「曾被 commit 引用」的内容才能恢复——`git restore` 丢弃的改动永久消失
- **日期**：2026-09-18
- **现象**：撤销实验中，同一个文件里并存三类"待撤销"内容，`git reset --hard` 之后：
  - 曾经 `add` 并 **`commit`** 过的内容 → 用 `git reflog` + `git reset --hard HEAD@{n}` **成功找回**
  - 只在工作区用 `echo >>` 追加、**从未 `add`** 的内容 → **永久消失，无任何办法**
- **根因**：关键是分清"什么被 git 真正记录过"——
  - `git reflog` 记录的是 **HEAD / 分支指针的移动历史**，它只能定位到 **commit**
  - 一个对象若**从未被任何 commit 引用**，就没有入口能定位它（即使 `add` 时创建过 blob，也会在 gc 时被回收）
  - **`git restore <file>` 丢弃的是工作区内容，此时对象根本不存在**——连抢救的对象都没有
- **误区纠正**：不是「暂存区有记录就能找回」。**`add` 只是预备，`commit` 才真正让内容进入可恢复范围**
- **可恢复性对照表**：
  | 操作 | 丢的是什么 | 能恢复吗 |
  |------|-----------|---------|
  | `git restore <file>` | 工作区未暂存的改动 | ❌ **永久消失** |
  | `git restore --staged <file>` | 仅暂存登记（工作区不动） | ✅ 内容本就没丢 |
  | `git reset --hard` | 工作区 + 暂存区改动 | 已 commit 的部分 ✅（reflog）；未 commit 的 ❌ |
  | `git revert` | 什么都不丢 | ✅ 本就不需要恢复 |
- **解法 / 自保策略**：**舍不得的改动，先 `git commit`（不是先 `git add`）。** commit 是廉价的，丢掉的代码是昂贵的
- **关联**：`docs/session-log.md` 2026-09-18；`stage0-toolchain/docs/0.2-git-notes.md`

### K-027 【实测】解析工具输出必须加 `LC_ALL=C`——中文 locale 会翻译字段名
- **日期**：2026-09-19
- **现象**：中文 locale 的 Ubuntu 上执行
  ```bash
  readelf -h hello-arm64 | grep -E "Class|Machine|Type"
  ```
  **完全没有输出**，但单独跑 `readelf -h` 明明有内容
- **根因**：GNU binutils 自带中文翻译，`readelf` 会把字段名译成中文：
  ```
  Class:   →  类别:
  Machine: →  机器:
  Type:    →  类型:
  ```
  于是 grep 英文关键词全部匹配失败。同理 `ldd` 输出 `不是动态可执行文件`（英文为 `not a dynamic executable`）
- **解法**：**任何要解析工具输出的场合，一律强制英文 locale**
  ```bash
  LC_ALL=C readelf -h hello-arm64 | grep -E "Class|Machine|Type"
  ```
- **教训**：**脚本里永远不要依赖本机 locale**。要 grep 工具输出、比较报错字符串、写 CI/构建脚本时，先 `LC_ALL=C`
- **关联**：`docs/session-log.md` 2026-09-19；阶段 1 写构建脚本时会用到

### K-028 【实测】交叉编译的产物在开发机上"跑不了"是正常现象——别误判为编译失败
- **日期**：2026-09-19
- **现象**：x86 虚拟机上交叉编译出 aarch64 的 `hello-arm64`，直接执行报错：
  ```
  aarch64-binfmt-P: Could not open '/lib/ld-linux-aarch64.so.1': No such file or directory
  ```
  看起来像"编译失败"，但 `file` 显示产物架构完全正确；同一文件 `scp` 到板卡后**运行成功**
- **根因**：**「编译成功」与「能在某台机器上运行」是两个独立条件**
  - 交叉编译只保证**架构正确**（ELF 头的 `e_machine`）
  - 能否运行取决于**当前机器的运行时环境**：动态链接器（`/lib/ld-linux-aarch64.so.1`）+ 兼容版本的 libc
  - 开发机是 x86，`/lib/` 下只有 x86 链接器 → 自然跑不起来
  - （本机额外装了 QEMU user-mode + binfmt_misc，故内核没报 `Exec format error`，而是转交 QEMU 模拟、由 QEMU 报"找不到链接器"）
- **关键区分：sysroot vs 运行时库**

  | 位置 | 谁在用 | 何时 |
  |------|--------|------|
  | `/usr/aarch64-linux-gnu/lib/`（sysroot） | 交叉**链接器** | **编译时** |
  | `/lib/`（系统根） | 内核 / 动态链接器 | **运行时** |

- **解法 / 正确做法**：
  1. 验证产物是否正确 → `file` 看架构；`aarch64-linux-gnu-readelf -h` 看 `Machine`
  2. 验证能否运行 → **必须到目标机器上跑**（或 `qemu-aarch64-static -L /usr/aarch64-linux-gnu ./prog` 本地模拟）
  3. 跨架构诊断**不要用 `ldd`**（它是脚本，会尝试执行目标程序）→ 用 `aarch64-linux-gnu-readelf -d`
- **延伸风险**：即使架构对、能启动，若工具链 sysroot 里的 glibc 与板卡 glibc **版本不匹配**，仍会报
  ```
  ./app: /lib/aarch64-linux-gnu/libc.so.6: version `GLIBC_2.xx' not found
  ```
  本项目两套环境同为 Ubuntu 22.04 / gcc 11.4.0，暂未遇到；**阶段 1~3 需留意**
- **关联**：`docs/session-log.md` 2026-09-19；K-015；阶段 1.4 部署用户态应用

### K-029 【实测】`gpioinfo` 显示 `unused` ≠ 引脚空闲——判据是 `pinmux-pins`
- **日期**：2026-09-20
- **现象**：探测 RK3566 可用 GPIO 时，`sudo gpioinfo gpiochip3` 显示 line 0~31 **全部是 `unused`**，据此判断"整个 bank 都空闲可用"——**结论错误**
  - 实际上 `gpiochip3` 的 line 13/14 被 `i2c3` 占用、line 15/16 被 `uart3` 占用、line 21 被 `pwm15` 占用……
  - 差点把**阶段 2 要用的串口引脚**（排针 8/10 = `uart3`）当成普通 GPIO 拿去做实验
- **根因**：`gpioinfo` 的 `used/unused` 只表示**"有没有人通过 gpiod 字符设备接口 request 这条线"**
  - **UART / I2C / SPI / PWM 等子系统不通过 gpiod 接口申请引脚**——它们在 `pinctrl` 层就把引脚**复用**给了自己的控制器
  - 于是它们在 `gpioinfo` 里显示为 `unused`，但引脚实际已被占用
- **正确做法**：查 `pinctrl` 的 pinmux 归属
  ```bash
  sudo cat /sys/kernel/debug/pinctrl/*/pinmux-pins
  ```
  | 输出样例 | 含义 |
  |---|---|
  | `pin 97 (gpio3-1): (MUX UNCLAIMED) (GPIO UNCLAIMED)` | ✅ 真空闲 |
  | `pin 111 (gpio3-15): fe670000.serial ... function uart3 group uart3m1-xfer` | ❌ 已被串口占用 |
  | `pin 4 (gpio0-4): fe2b0000.mmc ... function sdmmc0 group sdmmc0-det` | ⚠️ 被 SD 卡检测占用 |
- **附带坑：`grep -c "used"` 会把 `unused` 也算进去**
  ```bash
  sudo gpioinfo | grep -c "used"      # ❌ 160（全部引脚，因为 "unused" 含子串 "used"）
  sudo gpioinfo | grep -cw unused     # ✅ 146（-w 全词匹配）
  ```
  **统计关键词出现次数时一律加 `-w`**，否则子串会污染结果
- **教训**：**"没被申请" ≠ "没被使用"**。判断资源是否可用，要查**真正负责分配它的那一层**（这里是 pinctrl），而不是某个上层接口的视图
- **关联**：`references/hardware.md` §2.7；K-019（不要从间接证据推断硬件事实）

### K-030 【实测】udev 规则的 `GROUP` 依赖组存在，不存在则**静默忽略**；且 `newgrp` 只影响当前 shell
- **日期**：2026-09-21
- **现象**：为 `/dev/gpiochip*` 写了 udev 规则
  ```
  SUBSYSTEM=="gpio", KERNEL=="gpiochip*", GROUP="gpio", MODE="0660"
  ```
  执行 `udevadm control --reload-rules && udevadm trigger` 后：
  ```
  crw-rw---- 1 root root    ← 权限 600→660 生效了，但属组还是 root
  ```
  **MODE 生效、GROUP 没生效**
- **根因**：**`gpio` 组不存在**（`getent group gpio` 为空）。udev 找不到该组时**只记一行日志就跳过 GROUP**，其余动作（如 MODE）照常执行
  ```bash
  sudo journalctl -b | grep -iE "udev.*(error|unknown|invalid)"
  # → systemd-udevd: /etc/udev/rules.d/99-gpiochip.rules:1 Unknown group 'gpio', ignoring
  ```
  **udev 的失败是静默的**（开机要处理成百上千设备，不能因一条规则卡住）→ **排查 udev 问题必须看日志**
- **解法**（顺序很重要）：
  ```bash
  sudo groupadd gpio                  # ① 先建组
  sudo usermod -aG gpio $USER         # ② 把自己加进去
  sudo udevadm control --reload-rules # ③ 重载规则
  sudo udevadm trigger                # ④ 重新触发设备创建
  ls -l /dev/gpiochip3                # ⑤ 验证：crw-rw---- 1 root gpio
  ```
- **附带的坑：`newgrp` 的作用域有限**
  - `newgrp gpio` 只在**当前 shell 及其子进程**生效
  - **VS Code 分屏新开的终端是独立会话，不继承组身份** → 表现为 `gpioset: Permission denied`
  - 长期解决：**重启 VS Code Server 或重连**（让组关系对新会话生效）
- **关联**：`references/hardware.md` §2.7；`stage1-rk3566/docs/1.1-gpio-notes.md`；K-017（权限模型）

### K-031 【实测】改动 sysfs 状态前必须先记录原始值——LED 触发器改坏了不容易还原
- **日期**：2026-09-21
- **现象**：做 LED 子系统实验时把 `trigger` 从开机的 `[timer]` 改成 `heartbeat`/`none`，实验后**不知道该还原成什么值**
- **根因**：sysfs 是可写的运行时接口，**改动立即生效且不持久化**（重启后由设备树/驱动恢复）。但**运行期间没有"撤销"功能**，只能靠事先记录
- **解法**：
  1. **动手前先存档**：`cat` 一遍要改的属性并留档（本次侥幸——日志里恰好有操作前的 `cat` 输出）
  2. 权威来源是**设备树**里的默认值：
     ```bash
     ls /proc/device-tree/leds/*/
     tr -d '\0' < /proc/device-tree/leds/<node>/linux,default-trigger
     tr -d '\0' < /proc/device-tree/leds/<node>/default-state
     ```
  3. 最彻底的还原：`sudo reboot`
- **教训**：**改系统状态前先备份"当前值"**（同一条规则适用于 `/proc/sys/*`、sysfs、内核参数、寄存器配置）
- **关联**：`stage1-rk3566/docs/1.1-gpio-notes.md`

### K-032 【实测】交叉编译的内核树不能搬到目标板上编模块——host 工具架构不匹配
- **日期**：2026-09-27
- **现象**：在 aarch64 板卡上编译内核模块失败：
  ```
  warning: the compiler differs from the one used to build the kernel
  /bin/sh: 1: scripts/basic/fixdep: Exec format error
  make[2]: *** [scripts/Makefile.build:250: ...] Error 126
  ```
- **根因**：`/usr/src/linux-headers-6.1.141/scripts/basic/fixdep` 是 **x86-64 二进制**
  ```
  file → ELF 64-bit LSB pie executable, x86-64, interpreter /lib64/ld-linux-x86-64.so.2
  ```
  **内核构建产物分两类**：

  | 类型 | 运行在 | 架构 |
  |------|--------|------|
  | 目标代码（kernel / modules） | 目标板 | 目标架构（aarch64） |
  | **host 工具**（`fixdep` / `conf` / `modpost`） | **构建机** | **构建机架构（x86_64）** |

  内核在 x86 上交叉编译 → host 工具是 x86 的 → **拷到 aarch64 板卡执行不了**
- **尝试就地重建 → 死锁**：
  ```
  make scripts → 需要 syncconfig → 需要 scripts/kconfig/conf → 它也是 x86 的 💀
  ```
  且 `make modules_prepare` 有**副作用**：删除了 `include/generated/autoconf.h`
- **解法（推荐）**：**回到编内核的那台机器上编模块**
  ```bash
  # 在 x86 构建机上（用编内核的那棵树）
  make -C <kernel-src> M=$PWD ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- modules
  file xxx.ko            # 验证：ARM aarch64
  scp xxx.ko <板卡>:/path/
  ```
  **这是标准嵌入式工作流**：构建机编译 → 目标板只负责运行/验证
- **备选方案**：若坚持"让板卡自己能编"，必须在板卡上**重新构建一遍内核树**（`make modules_prepare`），耗时数小时 + 需数 GB 空间（板卡仅剩 6.3G）—— 通常不划算
- **澄清一个常见误解**：**"板卡不能编译"是错的**——板卡能编普通程序（原生 gcc + 系统 libc 齐全，0.4 的 make-lab 就是在板卡上编的）。真正受限的是"**依赖外来内核构建树**"的东西
- **类比**：把 Keil 工程拷到 STM32 上编译——工程里的 `armcc.exe` 是 Windows 程序，跑不了
- **关联**：`docs/session-log.md` 2026-09-27；K-009（模块与内核版本强绑定）

### K-033 【实测】`loading out-of-tree module taints kernel` 是正常提示，不是错误
- **日期**：2026-09-27
- **现象**：`insmod` 自编模块后，`dmesg` 出现：
  ```
  edge_ai_evt: loading out-of-tree module taints kernel.
  edge_ai_evt init
  ```
- **根因**：**taint（污染）是内核的标记机制**——记录"当前内核加载过非官方 / 非 GPL 代码"
  - `out-of-tree module` = 模块不在内核源码树内（外部模块）
  - **影响**：内核崩溃报告（oops/panic）**不会被上游/发行版官方受理**——无法确定问题是否由第三方代码引起
  - 若 `MODULE_LICENSE` **不是 GPL 兼容**，会额外多一条 `module license 'xxx' taints kernel`
- **解法**：**无需处理**（预期行为）
  - 规范做法：写 `MODULE_LICENSE("GPL")`，避免额外的专有 license taint
  - 排查问题时：先 `rmmod` 自己的模块，看问题是否仍复现（排除干扰）
- **验证**：`cat /proc/sys/kernel/tainted` 返回非 0（各 bit 标记不同污染原因）
- **关联**：`docs/session-log.md` 2026-09-27；`stage1-rk3566/drivers/edge_ai_evt/`

### K-034 【实测】`EACCES` 和 `EINVAL` 来自不同的层——排错先分清是哪一层拒绝的
- **日期**：2026-09-27
- **现象**：同一个 `/dev/edge_ai0`，普通用户打开报 `Permission denied`，提权后 `cat` 报 `Invalid argument`。两个错误看起来都是"打不开"，实际来自完全不同的层
- **根因（分层）**：

  | 错误码 | 场景 | 来自哪一层 | 说明 |
  |--------|------|-----------|------|
  | **`EACCES`** → `Permission denied` | 普通用户 `open()` | **VFS 权限检查** | 设备节点权限位（`root:root 600`）拒绝——**驱动代码根本没被执行** |
  | **`EINVAL`** → `Invalid argument` | root `read()` | **VFS 调用 fops 时** | 权限过了，但 `file_operations.read` 是 `NULL` → 内核返回 `-EINVAL` |

- **排查指引**：
  - 见 `Permission denied` → 查 `/dev/<节点>` 权限、udev 规则；**不要去读驱动代码**
  - 见 `Invalid argument` → 查 `file_operations` 里**实现了哪些回调**；不要去查权限
- **验证方式**：
  ```bash
  ls -l /dev/edge_ai0        # 看权限位
  sudo cat /dev/edge_ai0     # 提权后再试，区分两层
  ```
- **教训**：**错误信息会告诉你它在哪一层失败——顺着那一层查，不要乱翻。** 这就是阶段 0 学的"分层定位"（硬件→驱动→设备节点→API→应用）的实际应用
- **关联**：`docs/session-log.md` 2026-09-27；K-017（权限模型）

---

### K-035 【实测】IDE 报"应输入 )"而不是"未定义"——语法错 = 宏展开出了非法代码
- **日期**：2026-09-28
- **现象**：板卡上交叉编译**完全成功**、模块运行正常，但 VS Code 里每个 `pr_info` / `pr_err` 都飘红，错误是 **`应输入 ")"`（语法错误）**，**不是** `未定义标识符`
- **根因**：`KBUILD_MODNAME` 未定义 → 宏展开出非法语法：
  ```
  pr_info("x")  →  printk(KERN_INFO pr_fmt("x"))
                →  printk(KERN_INFO KBUILD_MODNAME ": " "x")
                →  printk("\001" "6" KBUILD_MODNAME ": " "x")
                                        └──── 裸标识符夹在字符串字面量中间 ────┘  → 语法错
  ```
  - `KBUILD_MODNAME` **不在任何头文件里定义**（`printk.h` 里只在注释出现过）
  - 它是 **kbuild 编译时用 `-D` 注入的** → GCC 有、IntelliSense 没有 → 两者不一致
- **推理链（关键技巧）**：

  | IDE 报什么 | 说明什么 |
  |-----------|---------|
  | `identifier "pr_info" is undefined` | IntelliSense **找不到** `linux/printk.h` → includePath 没配好 |
  | **`应输入 ")"`（语法错）** | 头文件**找到了、宏也展开了**，只是展开结果里有未定义的宏 |

- **解法**：`.vscode/c_cpp_properties.json` 的 `defines` 里加
  ```json
  "KBUILD_MODNAME=\"edge_ai_evt\""
  ```
  值只是**占位**（真实值永远由 kbuild 给），所以以后加新模块不用改。
  改完执行 `C/C++: Reset IntelliSense Database` 或 `Developer: Reload Window`
- **验证**：`grep -o '\-DKBUILD_MODNAME=[^ ]*' .edge_ai_evt.o.cmd`
- **MCU 类比**：Keil 的 `Options for Target → C/C++ → Define` 里配的宏，Keil 编译时认识，但 VSCode + C/C++ 插件不知道 Keil 配了哪些 `-D` → 同样飘红
- **关联**：`.vscode/c_cpp_properties.json`；`stage1-rk3566/drivers/edge_ai_evt/`

---

### K-036 【实测】`case` 不构成作用域——里面声明的变量会"泄漏"到整个 switch 块
- **日期**：2026-09-29
- **现象**：想在 `case EDGE_AI_IOC_LEN:` 里直接写 `int len;`，不确定合不合法
- **根因**：`case X:` 只是**标签**，不是作用域。真正的块是 `switch` 后面那对 `{}`。所以 case 里声明的变量作用域是**整个 switch 块**
- **实测（板卡 GCC 11.4）**：

  | 情况 | 结果 |
  |------|------|
  | 单个 case 里 `int len;` | ✅ **能编译**（连 `-std=c11 -Werror` 都通过，退出码 0） |
  | **两个 case 都写 `int len;`** | ❌ `error: redeclaration of 'len' with no linkage`（退出码 1） |

- **解法**：case 里要声明变量就**加一对 `{}`** 包起来：
  ```c
  case FOO: {
      int len;
      ...
      break;
  }
  ```
- **教训**：**"能编译"≠"正确"**。单个 case 能过只是因为恰好没有同名冲突；等第二个 case 也声明同名变量时才爆——那时你已经忘了当初为什么这么写
- **关联**：`stage1-rk3566/drivers/edge_ai_evt/edge_ai_evt.c`

---

### K-037 【实测】`copy_from_user` 用"固定长度"拷用户数据 = 运气型 bug
- **日期**：2026-09-29
- **现象**：接口约定"`arg` 是指向用户字符串的指针"，驱动这样写：
  ```c
  char user_buf[BUF_SIZE];
  copy_from_user(user_buf, (char __user *)arg, BUF_SIZE);   /* 无条件拷 256 字节 */
  ```
  测试程序传 `"Hello from user space!"`（23 字节）—— **居然跑通了**
- **根因**：**驱动读的字节数超过了用户实际提供的**
  - 这次没崩，是因为字符串字面量在 `.rodata` 段、后面紧跟着别的数据（同页内），恰好都在映射范围 → **是运气**
  - 如果字符串正好落在**页尾**附近，后面是未映射页 → `copy_from_user` 返回非 0 → 用户看到莫名其妙的 `Bad address`
  - 最坑的是**时灵时不灵**（取决于链接器把数据放在哪）——改一行代码可能就从"能跑"变成"不能跑"
- **解法（两种）**：
  1. **结构体方案（推荐）**：`_IOW(MAGIC, nr, struct xxx_arg)`。先 `copy_from_user` 整个结构体（**大小固定已知**），取出 `len` 字段**校验后再用**
  2. `strnlen_user()` 先量长度：返回 `0` = 地址非法；`> maxlen` = 太长；`<= maxlen` = 长度（**含 `\0`**）
- **结构体方案为何更优**：契约**显式**（必须传完整结构体）；**能传二进制数据**（可含 `\0`），不像字符串方案受 `\0` 限制。内核里 `struct i2c_msg`、`struct v4l2_buffer` 都是这个套路
- ⚠️ **必须同时校验 `len` 的上下界**：
  ```c
  if (put_arg.len < 0 || put_arg.len > BUF_SIZE) return -EINVAL;
  ```
  **少了 `len < 0` 那半边 = 内核堆溢出漏洞**：`memcpy` 第三个参数是 `size_t`（无符号），`(size_t)-1` 会拷到天荒地老 → 可用于提权
- **MCU 类比**：串口收帧不能"假定对方一定发满 256 字节"，必须按 LEN 字段/结束符处理
- **关联**：`stage1-rk3566/docs/1.2-driver-notes.md` §10.4

---

### K-038 【实测】用眼睛判断"有没有读到数据"会被无换行输出骗
- **日期**：2026-09-29
- **现象**：`sudo ./test_edge_ai && sudo cat /dev/edge_ai0`，`cat` 那部分**看起来完全没输出**，一度被误判成"驱动认为有 22 字节、用户态却读不到"的严重 bug，白排查了一轮
- **根因**：输出的 `"Hello from user space!"` **没有换行符**，紧贴在 shell 提示符前面：
  ```
  EINVAL as expected for len > BUF_SIZE
  Hello from user space!lihaihui@lckfb:...$
  ```
  肉眼扫过去容易当成"提示符前面的一点噪声"而忽略
- **为什么没换行**：`klen = strlen(...)` 不含 `\0`，而 `"Hello from user space!"` 本身也没有 `\n`
  - 对比：`init` 里预填的 `"Hello from edge_ai_evt module!\n"` **带** `\n` → 有换行
- **核心概念**：**设备文件不做任何格式化**。`cat` 只把驱动的字节**原样搬运**到 stdout；有没有换行完全取决于你写进缓冲区的内容（驱动不认识 `\n`，它只是 `0x0A`）
- **习惯（解法）**：
  ```bash
  sudo cat /dev/edge_ai0 | wc -c      # 数字节，最直接
  sudo cat /dev/edge_ai0 | xxd        # 看十六进制，还能区分 空格 / \0 / 什么都没有
  ```
- **MCU 类比**：串口发 `"OK"` 而不是 `"OK\r\n"`，上位机串口助手就一行连一行 —— 传输层不管格式
- **教训**：**验证"有没有数据"要用工具，不要用眼睛。**
- **关联**：`stage1-rk3566/docs/1.2-driver-notes.md` §10.6

---

### K-039 【实测】`register_chrdev` 成功返回的是**主设备号本身**（正数），不是 0
- **日期**：2026-09-28
- **现象**：审代码时觉得 `if (major < 0)` 这个判断"看着怪"（一般习惯写 `if (ret)`）
- **根因**：**内核里"成功返回什么"因函数而异，完全不统一**：

  | 函数 | 成功返回 | 失败返回 | 判错方式 |
  |------|---------|---------|---------|
  | `register_chrdev` | ⚠️ **主设备号（正数）** | 负 errno | `major < 0` |
  | `alloc_chrdev_region` | `0` | 负 errno | `ret < 0` |
  | `class_create` / `device_create` | 指针 | `ERR_PTR(-errno)` | `IS_ERR(p)` + `PTR_ERR(p)` |
  | `copy_to_user` / `copy_from_user` | `0` | **未拷贝的字节数（正数！）** | `if (ret)` |

- **不能写 `if (!major)`**：主设备号 `0` 本身是合法值（虽然动态分配通常不会给 0）
- **顺带澄清两套接口**：`register_chrdev` 是**一步到位的老接口**（内部就是 `__register_chrdev(major, 0, 256, name, fops)`），**独占该主号下全部 256 个次设备号**；要"一个驱动管多个设备"必须用 `alloc_chrdev_region` + `cdev_init` / `cdev_add`
- **教训**：**内核 API 的返回值约定必须逐个查头文件注释，凭直觉必翻车**
- **关联**：`stage1-rk3566/docs/1.2-driver-notes.md` §7.1b、§7.3

---

### K-040 【实测】`v4l2-ctl` 不带 `-d` 会默认操作 `/dev/video0`——而且输出“看起来很正常”
- **日期**：2026-10-03
- **现象**：查摄像头支持的格式，忘了带 `-d`：
  ```bash
  $ v4l2-ctl --list-formats-ext        # ❌ 默认 /dev/video0
  ioctl: VIDIOC_ENUM_FMT
          Type: Video Capture Multiplanar
          [0]: 'UYVY' ...
  ```
  **命令没报错、输出完全正常**，很容易以为“我的摄像头就这能力”——其实看的是 RK3566 内置 ISP（MIPI CSI），**跟 USB 摄像头毫无关系**
- **根因**：`v4l2-ctl` 不指定 `-d` 时默认用 `/dev/video0`。而板卡上 `video0`~`video7` 是 RKISP（MIPI），USB 摄像头在 `video10`
- **解法**：**永远显式 `-d`**，且先用 `v4l2-ctl --list-devices` 确认设备身份
  ```bash
  v4l2-ctl --list-devices
  v4l2-ctl -d /dev/video10 --list-formats-ext
  ```
- **线索**：`Type: Video Capture Multiplanar`（多平面）vs `Type: Video Capture`（单平面）——**这个差异直接决定代码里 `type` 字段填什么**（K-043）
- **教训**：**“没有报错”不等于“看的对设备”。** 这是 K-016（绝不硬编码设备节点）的典型实践
- **关联**：`stage1-rk3566/docs/1.3-v4l2-notes.md` §1.3；`LEARNING-MAP.md` 1.3；K-016

---

### K-041 【实测】`Capabilities` vs `Device Caps`——判断节点能力必须看后者
- **日期**：2026-10-03
- **现象**：UVC 摄像头有两个 `/dev/video*` 节点，不知用哪个：
  ```bash
  $ v4l2-ctl -d /dev/video10 --info
          Capabilities : 0x84a00001    ← Video Capture + Metadata Capture + Streaming
          Device Caps  : 0x04200001    ← Video Capture + Streaming
  $ v4l2-ctl -d /dev/video11 --info
          Capabilities : 0x84a00001    ← 一模一样！
          Device Caps  : 0x04a00000    ← Metadata Capture + Streaming
  ```
- **根因**：`VIDIOC_QUERYCAP` 返回两个字段，含义不同：

  | 字段 | 含义 |
  |------|------|
  | `capabilities` | **整个驱动**的能力 |
  | **`device_caps`** | **这个具体节点**的能力 ← **判断依据** |

  两个节点 `capabilities` 相同，但 `device_caps` 不同。**只读 `capabilities` 会误判**
- **解法**：代码里检查 `cap.device_caps & V4L2_CAP_VIDEO_CAPTURE`（或看 `v4l2-ctl --list-devices` 的节点顺序）
- **验证**：`video10` = 视频流（`Video Capture`），`video11` = 元数据（`Metadata Capture`）；抓帧必须用 `video10`
- **关联**：`stage1-rk3566/docs/1.3-v4l2-notes.md` §1.4

---

### K-042 【实测】V4L2 的 `bytesused` ≠ `length`——写文件必须用 `bytesused`
- **日期**：2026-10-03
- **现象**：MJPG 640×480 抓帧，实测：
  ```
  QUERYBUF: length    = 614400      ← 614400 = 640×480×2（按【未压缩】尺寸分配）
  DQBUF:    bytesused =  54716      ← 实际帧只有 54.7 KB
  ```
- **根因**：`struct v4l2_buffer` 里两个长度字段含义不同：

  | 字段 | 含义 |
  |------|------|
  | `length` | buffer 的**总容量** |
  | **`bytesused`** | 这一帧**实际有效数据的字节数** |

  UVC 驱动按“**最大可能的帧大小**”保守分配（保证格式被换成 YUYV 也装得下）→ `length` 是**未压缩**尺寸
- **解法**：写文件/处理数据用 `buf.bytesused`
- **用错会怎样**：写出 614400 字节，其中 **559684 字节是垃圾**（未填的残留数据）→ 文件大概率打不开
- **附带数据**：压缩比 `614400 / 54716 ≈ 11.2`；带宽 `54716 × 30 ≈ 1.64 MB/s`（同分辨率 YUYV 需 18.4 MB/s，**差 11 倍**）
- **关联**：`stage1-rk3566/docs/1.3-v4l2-notes.md` §2.5、§3.4

---

### K-043 【实测】V4L2 的 `S_FMT` / `REQBUFS` 是“传入传出”——必须用返回的实际值
- **日期**：2026-10-03
- **现象**：请求 MJPG 640×480，打印时发现结构体里的值“看起来就是自己设的”；后来意识到**驱动可能改过它**
- **根因**：这类 ioctl 的参数结构体同时承担**输入和输出**：
  - `VIDIOC_S_FMT`：你传请求，**驱动把实际生效格式回填到同一个结构体**
  - `VIDIOC_REQBUFS`：你传想要的 buffer 数，**驱动回填实际给了几个**
- **解法**：
  ```c
  req.count = 4;                        /* 输入 */
  ioctl(fd, VIDIOC_REQBUFS, &req);
  for (i = 0; i < req.count; i++) { }   /* ✅ 用返回后的 count，不是写死的 4 */

  ioctl(fd, VIDIOC_S_FMT, &fmt);        /* 之后 fmt 里已经是实际值 */
  ```
  要打印“我请求了什么”，必须在调用**前**存一份副本
- **铁律**：**凡是你“请求”的东西，都要读回确认实际值**——和串口波特率、时钟树配置一个道理
- **关联**：`stage1-rk3566/docs/1.3-v4l2-notes.md` §2.5；`stage1-rk3566/apps/v4l2-capture/capture.c`

---

### K-044 【实测】`DQBUF` 之后 buffer 归用户——出错路径必须先 `QBUF` 再清理
- **日期**：2026-10-03
- **现象**：写抓帧程序时定义了 `requeue:` 标签但没用到，GCC 警告 `-Wunused-label`。当时本能想“删掉标签”，仔细一想：**这个标签本来就该用上**
- **根因**：V4L2 的 buffer 是**借还制**：
  - `DQBUF` 之后，该 buffer 的**归属权转到用户**，驱动在你 `QBUF` 还回去之前**不会碰它**
  - 所以 `DQBUF` 与 `QBUF` 之间的**任何错误路径**，如果直接 `goto cleanup` 走人，就是“**借了东西没还**”
- **解法**：`DQBUF` 后所有错误分支都 `goto requeue`（先还 buffer，再走清理）：
  ```c
  ioctl(fd, VIDIOC_DQBUF, &buf);
  fp = fopen(OUT_FILE, "wb");
  if (!fp) { perror("fopen"); goto requeue; }   /* ✅ 不是 cleanup */
  ```
- **为什么重要**：
  - **单帧场景**：`close(fd)` 会让内核强制回收所有 buffer（V4L2 规范保证），所以不会崩——**只是逻辑不严谨**
  - **多帧/连续采集场景**：buffer 泄漏 → 可用 buffer 耗尽 → 驱动采不到数据 → `DQBUF` **永远阻塞、画面卡死**
- **教训**：**编译器警告 `-Wunused-label` 不只是“删掉就好”——先想“为什么当初想写这个标签”**
- **关联**：`stage1-rk3566/docs/1.3-v4l2-notes.md` §2.3、§四；`LEARNING-MAP.md` 引用的 K-004（QBUF/DQBUF 配对）

---

### K-045 【通用】`/dev` 节点的时间戳能直接告诉你“哪个是刚插上的设备”
- **日期**：2026-10-03
- **现象**：板卡上有 12 个 `/dev/video*`，分不清哪个是 USB 摄像头：
  ```bash
  crw-rw---- 1 root video 81,  0 Jul 29 21:41 /dev/video0      ← 开机时创建
  crw-rw---- 1 root video 81, 13 Oct  3 11:33 /dev/video10     ← 11:33 刚出现
  ```
- **根因**：`/dev` 是 **devtmpfs**（内存文件系统），节点在**设备被驱动认领的瞬间**创建。所以时间戳 = 设备“上线”时间
- **用法**：插了新 USB 设备后 `ls -l /dev/xxx`，时间戳与“开机时间”不同的就是刚插上的
- **配合使用**：`v4l2-ctl --list-devices` 的“总线信息”（如 `usb-fd800000.usb-1`）能进一步确认设备归属
- **关联**：`stage1-rk3566/docs/1.3-v4l2-notes.md` §1.1

---

### K-046 【实测】性能数据必须连同“测试条件”一起记录——同一条命令会跑出两种帧率
- **日期**：2026-10-05
- **现象**：自写 `capture.c` 连续抓 100 帧，实测 **16.58 fps**；但 `references/hardware.md` §2.5 里 2026-09-10 的实测是 **30.23 fps**（MJPG 640×480）——**差了近一半**
- **排查过程**：先逐项审代码（`QBUF` 位置、避开磁盘 IO、`CLOCK_MONOTONIC`、预热帧）—— **全部正确，问题不在代码**
- **根因（假设，待验证）**：**自动曝光把帧率拖下来了**。曝光时间和帧率直接挂钩（最长曝光时间不能超过帧间隔）：

  | 曝光时间 | 理论帧率上限 | 对应观察 |
  |---------|-------------|---------|
  | **31.2 ms** | `1/0.0312 ≈ 32 fps` | 对应 09-10 白天测得的 **30.23 fps**（`exposure_time_absolute = 312`） |
  | **60 ms** | `1/0.060 ≈ 16.6 fps` | **正好对本次的 16.58 fps** |

  USB 摄像头默认跑**自动曝光**（`auto_exposure=3` Aperture Priority）→ **光线暗就拉长曝光时间** → 帧率被拖慢。
  旁证：本次平均帧 **44.7 KB** vs 之前 **54.7 KB**（暗画面噪声少，JPEG 压得更小）。
- **验证方法**：
  ```bash
  v4l2-ctl -d /dev/video10 --get-ctrl=auto_exposure,exposure_time_absolute
  # 锁手动曝光后再测（排除光照变量）
  v4l2-ctl -d /dev/video10 --set-ctrl=auto_exposure=1
  v4l2-ctl -d /dev/video10 --set-ctrl=exposure_time_absolute=100
  ```
- **教训**：**性能数据必须连同测试条件一起记录**（光照/自动曝光/预热/分辨率/格式）。
  只写“30.23 fps”而不写“白天、自动曝光、曝光 31ms”，下次看到 16.58 就会白排查半天。
- **额外收获**：
  - 枚举设备时要同时查**能力**（`device_caps & V4L2_CAP_VIDEO_CAPTURE`）和**身份**（`cap.driver == "uvcvideo"`）——只查一个都可能选错节点
  - `cap.driver` 是 `__u8[16]` 不是 `char*`，传 `strcmp` 要强转
  - 枚举时 `open` 失败要区分 `ENOENT`（节点不存在，跳过）和 `EACCES`（权限不足，**要报出来**）——否则“找不到设备”的真因被静默吞掉
- **关联**：`stage1-rk3566/docs/1.3b-continuous-capture.md` §2；`references/hardware.md` §2.5；K-020（UVC 预热期）

---

## 七、待填充区域

> 以下是本项目**尚未遇到但很可能遇到**的领域，踩坑后追加。

### 设备树与 GPIO 复用
<!-- 新增条目从这里开始，编号接着 K-024 -->

### 内存与性能调优
<!-- ... -->

### 长时间运行的稳定性
<!-- ... -->

---

## 统计

| 来源 | 条数 |
|------|------|
| 【实测】（本项目亲历） | 31 |
| 【通用】（已知陷阱，未亲历） | 15 |

> 💡 **启发式观察**：当一个坑被踩过两次，说明它不是"坑"而是"知识盲区"，应该把根因写进 `SKILL.md` 或阶段学习笔记，而不只是记在这里。
