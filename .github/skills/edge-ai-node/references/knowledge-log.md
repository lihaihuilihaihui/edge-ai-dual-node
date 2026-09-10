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

---

## 七、待填充区域

> 以下是本项目**尚未遇到但很可能遇到**的领域，踩坑后追加。

### 设备树与 GPIO 复用
<!-- 新增条目从这里开始，编号接着 K-022 -->

### 内存与性能调优
<!-- ... -->

### 长时间运行的稳定性
<!-- ... -->

---

## 统计

| 来源 | 条数 |
|------|------|
| 【实测】（本项目亲历） | 6 |
| 【通用】（已知陷阱，未亲历） | 15 |

> 💡 **启发式观察**：当一个坑被踩过两次，说明它不是"坑"而是"知识盲区"，应该把根因写进 `SKILL.md` 或阶段学习笔记，而不只是记在这里。
