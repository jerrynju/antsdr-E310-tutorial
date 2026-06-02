# ANTSDR E310 工具操作手册

> 本手册覆盖固件开发和调试所需的全部工具：DFU-util、U-Boot CLI、Vivado 基础操作、Git 补丁管理、IIO 工具、串口连接。

---

## 目录

1. [DFU-util — USB 固件烧录工具](#1-dfu-util--usb-固件烧录工具)
2. [U-Boot CLI — 引导程序命令行](#2-u-boot-cli--引导程序命令行)
3. [串口连接工具](#3-串口连接工具)
4. [IIO 工具集 — RF 调试](#4-iio-工具集--rf-调试)
5. [Git Patch 操作](#5-git-patch-操作)
6. [Vivado 基础操作](#6-vivado-基础操作)
7. [SD 卡操作工具](#7-sd-卡操作工具)
8. [网络连接与 SSH](#8-网络连接与-ssh)

---

## 1. DFU-util — USB 固件烧录工具

`dfu-util` 是通过 USB 进行设备固件更新（DFU）的命令行工具。

### 1.1 安装

```sh
sudo apt-get install dfu-util
dfu-util --version
```

### 1.2 核心命令

#### 列出 DFU 设备

```sh
sudo dfu-util -l
```

**输出示例：**
```
Found DFU: [0456:b673] ver=0200, devnum=3, cfg=1, intf=0, path="1-3.1", alt=4,
  name="uboot-extra-env.dfu", serial="..."
Found DFU: [0456:b673] ver=0200, devnum=3, cfg=1, intf=0, path="1-3.1", alt=3,
  name="uboot-env.dfu", serial="..."
Found DFU: [0456:b673] ver=0200, devnum=3, cfg=1, intf=0, path="1-3.1", alt=2,
  name="boot.dfu", serial="..."
Found DFU: [0456:b673] ver=0200, devnum=3, cfg=1, intf=0, path="1-3.1", alt=1,
  name="firmware.dfu", serial="..."
Found DFU: [0456:b673] ver=0200, devnum=3, cfg=1, intf=0, path="1-3.1", alt=0,
  name="reset", serial="..."
```

#### 下载（烧录）固件

```sh
# 语法：dfu-util -a <alt名称或编号> -D <本地文件>
sudo dfu-util -a firmware.dfu -D ./ant.dfu
sudo dfu-util -a boot.dfu -D ./boot.dfu
sudo dfu-util -a uboot-env.dfu -D ./uboot-env.dfu
```

**参数说明：**

| 参数 | 说明 |
|------|------|
| `-a <name>` | 指定 alternate interface（烧录目标分区，用名称匹配） |
| `-D <file>` | Download：从电脑写入设备 |
| `-U <file>` | Upload：从设备读取到电脑（备份） |
| `-l` | 列出所有可用 DFU 设备 |
| `-d <vid:pid>` | 指定 USB VID:PID（默认自动检测） |
| `-t <size>` | 设置传输块大小（默认 4096） |
| `-R` | 传输完成后复位设备 |

#### 备份当前固件

```sh
# 备份额外环境变量
sudo dfu-util -a uboot-extra-env.dfu -U ./backup-uboot-extra-env.dfu

# 备份主固件（视设备支持情况）
sudo dfu-util -a firmware.dfu -U ./backup-firmware.dfu
```

### 1.3 常见问题

**问题：`dfu-util: No DFU capable USB device available`**
```sh
# 解决方案 1：确认设备已进入 DFU 模式（两灯变绿）
# 解决方案 2：检查 USB udev 规则
cat /etc/udev/rules.d/99-dfu.rules 2>/dev/null || \
sudo tee /etc/udev/rules.d/99-dfu.rules << 'EOF'
SUBSYSTEM=="usb", ATTRS{idVendor}=="0456", ATTRS{idProduct}=="b673", MODE="0666"
EOF
sudo udevadm control --reload-rules
sudo udevadm trigger
```

**问题：烧录中途断开**
```sh
# 重新进入 DFU 模式（重启设备并按 DFU 按钮），然后重新烧录
# 不要中途拔掉 USB 线，可能导致 Flash 损坏
```

**问题：`dfu-util: cannot open DFU device 0456:b673`**
```sh
# 使用 sudo 或添加用户到 plugdev 组
sudo usermod -aG plugdev $USER
# 注销并重新登录后生效
```

---

## 2. U-Boot CLI — 引导程序命令行

U-Boot 在系统启动时提供命令行接口，用于调试和配置。

### 2.1 进入 U-Boot 命令行

通过串口连接（波特率 115200），在设备上电后看到倒计时时按任意键：

```
Hit any key to stop autoboot: 3...2...1...
ant =>
```

### 2.2 常用命令

#### 查看环境变量

```sh
# 列出所有环境变量
printenv

# 查看特定变量
printenv bootcmd
printenv ipaddr
printenv serverip
```

#### 设置环境变量

```sh
# 设置变量
setenv mode 2r2t
setenv ipaddr 192.168.1.10
setenv serverip 192.168.1.100

# 删除变量
setenv myvar   # 值为空即删除

# 保存到 Flash
saveenv
```

#### 网络操作

```sh
# 测试网络连通性
ping 192.168.1.100

# 通过 TFTP 下载文件到内存
tftp 0x3000000 zImage
tftp 0x2A00000 zynq-ant.dtb

# 查看 MAC 地址
printenv ethaddr
```

#### 内存操作

```sh
# 读取内存（十六进制显示）
md 0x3000000 0x20        # 从地址 0x3000000 显示 32 个字

# 写入内存
mw 0x3000000 0xDEADBEEF  # 写入一个字

# 内存比较
cmp.b 0x3000000 0x4000000 0x1000
```

#### Flash 操作（QSPI）

```sh
# 查看 Flash 信息
sf probe

# 读取 Flash 到内存
sf read 0x3000000 0x0 0x800000   # 从 Flash 偏移 0 读 8MB 到内存

# 擦除 Flash
sf erase 0x0 0x800000            # 从偏移 0 擦除 8MB

# 写入内存到 Flash
sf write 0x3000000 0x0 0x800000  # 将内存 8MB 写到 Flash 偏移 0
```

#### 启动命令

```sh
# 手动启动 Linux（内核在内存地址 0x3000000）
bootm 0x3000000

# 使用 FIT 镜像启动
bootm 0x3000000#conf@zynq-ant.dtb

# 从 SD 卡加载并启动
load mmc 0 0x3000000 uImage
load mmc 0 0x2A00000 system.dtb
load mmc 0 0x2000000 rootfs.cpio.gz
bootm 0x3000000 0x2000000 0x2A00000
```

#### 系统信息

```sh
# 查看板级信息
bdinfo

# 查看 CPU 信息
cpu info

# 重启
reset

# 查看可用命令列表
help

# 查看特定命令帮助
help sf
help load
```

### 2.3 环境变量说明（E310 常用）

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `bootcmd` | — | 自动启动命令 |
| `ipaddr` | `192.168.1.10` | 设备 IP |
| `serverip` | `192.168.1.100` | TFTP 服务器 IP |
| `ethaddr` | 设备唯一 | MAC 地址 |
| `mode` | `1r1t` | 收发模式（1r1t / 2r2t） |
| `compatible` | — | 兼容性字符串 |
| `attr_name` | — | IIO 属性名称 |
| `attr_val` | — | IIO 属性值 |

### 2.4 配置 2R2T 的 U-Boot 方式

```sh
# 在 U-Boot 命令行执行
setenv attr_name compatible
setenv attr_val ad9361
setenv compatible ad9361
setenv mode 2r2t
saveenv
reset
```

---

## 3. 串口连接工具

### 3.1 确认串口设备

```sh
# E310 通过 USB 转串口连接时
ls /dev/ttyUSB*    # 通常是 /dev/ttyUSB0

# 通过 JTAG/USB 连接时
ls /dev/ttyACM*    # 可能是 /dev/ttyACM0
```

### 3.2 使用 minicom

```sh
# 安装
sudo apt-get install minicom

# 连接（波特率 115200，无流控）
sudo minicom -D /dev/ttyUSB0 -b 115200

# minicom 快捷键
# Ctrl+A Z  → 帮助菜单
# Ctrl+A X  → 退出
# Ctrl+A L  → 记录日志到文件
```

### 3.3 使用 screen

```sh
# 安装
sudo apt-get install screen

# 连接
sudo screen /dev/ttyUSB0 115200

# screen 快捷键
# Ctrl+A K  → 结束会话
# Ctrl+A D  → 分离（后台保持）
```

### 3.4 使用 picocom（最轻量）

```sh
# 安装
sudo apt-get install picocom

# 连接
sudo picocom -b 115200 /dev/ttyUSB0

# 退出：Ctrl+A Ctrl+X
```

### 3.5 串口参数

| 参数 | 值 |
|------|-----|
| 波特率 | 115200 |
| 数据位 | 8 |
| 停止位 | 1 |
| 奇偶校验 | 无 |
| 流控 | 无 |

---

## 4. IIO 工具集 — RF 调试

IIO（Industrial I/O）是 Linux 内核的传感器/RF 设备框架，AD9361 通过 IIO 接口暴露所有 RF 参数。

### 4.1 安装 IIO 工具

```sh
# 在主机端安装
sudo apt-get install libiio-utils

# 或从源码编译（获取最新版本）
git clone https://github.com/analogdevicesinc/libiio.git
cd libiio && mkdir build && cd build
cmake .. && make && sudo make install
```

### 4.2 核心命令

#### iio_info — 查看设备信息

```sh
# 本地设备
iio_info

# 通过网络连接 E310
iio_info -u ip:192.168.1.10

# 通过 USB 连接
iio_info -u usb:1.3.5
```

**输出示例：**
```
IIO context created with network backend.
Backend version: 0.21
Backend description string: ...
IIO context has 5 devices:
        iio:device0: ad9361-phy (buffer capable)
                11 channels found:
                        altvoltage0: TX1_LO (output)
                        ...
```

#### iio_attr — 读写设备属性

```sh
# 读取属性
iio_attr -u ip:192.168.1.10 -c ad9361-phy voltage0 sampling_frequency
# 输出：61440000 (61.44 MHz 采样率)

# 写入属性
iio_attr -u ip:192.168.1.10 -c ad9361-phy voltage0 sampling_frequency 30720000

# 读取发射频率
iio_attr -u ip:192.168.1.10 -c ad9361-phy altvoltage1 frequency
# 输出：2400000000 (2.4 GHz)

# 设置接收频率到 433 MHz
iio_attr -u ip:192.168.1.10 -c ad9361-phy altvoltage0 frequency 433000000
```

**常用 AD9361 属性：**

| 属性路径 | 说明 | 单位 |
|---------|------|------|
| `voltage0/sampling_frequency` | 采样率 | Hz |
| `altvoltage0/frequency` | RX LO 频率 | Hz |
| `altvoltage1/frequency` | TX LO 频率 | Hz |
| `voltage0/rf_bandwidth` | RX 带宽 | Hz |
| `voltage0/gain_control_mode` | AGC 模式 | — |
| `voltage0/hardwaregain` | RX 增益 | dB |
| `voltage1/hardwaregain` | TX 衰减 | dB（负值） |

#### iio_readdev — 读取数据流

```sh
# 读取 RX 数据（1024 个采样，循环输出）
iio_readdev -u ip:192.168.1.10 -b 1024 cf-ad9361-lpc

# 保存到文件（采集 100 个缓冲区）
iio_readdev -u ip:192.168.1.10 -b 1024 -s 102400 cf-ad9361-lpc > rx_data.bin
```

#### iio_writedev — 写入数据流（发射）

```sh
# 发送本地文件数据
cat tx_data.bin | iio_writedev -u ip:192.168.1.10 -b 1024 cf-ad9361-dds-core-lpc
```

### 4.3 设置 AD9361 RF 参数示例

```sh
TARGET_IP="192.168.1.10"

# 设置 RX 中心频率为 915 MHz
iio_attr -u ip:$TARGET_IP -c ad9361-phy altvoltage0 frequency 915000000

# 设置 TX 中心频率为 915 MHz  
iio_attr -u ip:$TARGET_IP -c ad9361-phy altvoltage1 frequency 915000000

# 设置采样率为 5 MHz
iio_attr -u ip:$TARGET_IP -c ad9361-phy voltage0 sampling_frequency 5000000
iio_attr -u ip:$TARGET_IP -c ad9361-phy voltage1 sampling_frequency 5000000

# 设置带宽为 4 MHz
iio_attr -u ip:$TARGET_IP -c ad9361-phy voltage0 rf_bandwidth 4000000
iio_attr -u ip:$TARGET_IP -c ad9361-phy voltage1 rf_bandwidth 4000000

# 关闭 AGC，设置固定增益 30 dB
iio_attr -u ip:$TARGET_IP -c ad9361-phy voltage0 gain_control_mode manual
iio_attr -u ip:$TARGET_IP -c ad9361-phy voltage0 hardwaregain 30
```

---

## 5. Git Patch 操作

### 5.1 理解 patch 文件格式

`git format-patch` 生成的 patch 文件包含 diff 信息和提交元数据：

```
From abc1234 Mon Sep 17 00:00:00 2001
From: Author <email>
Date: ...
Subject: [PATCH] add support for ANTSDR E310

--- a/Makefile
+++ b/Makefile
@@ -10,6 +10,8 @@ ...
 existing line
+new line added by patch
 another existing line
```

`+` 开头：新增行；`-` 开头：删除行；空格开头：上下文行（不变）

### 5.2 检查补丁（不实际应用）

```sh
# 显示补丁统计信息
git apply --stat mypatch.patch

# 检查是否可以干净地应用
git apply --check mypatch.patch

# 如果显示 error: patch failed，说明源文件已修改，无法直接应用
```

### 5.3 应用补丁

```sh
# 应用单个补丁
git apply mypatch.patch

# 应用时忽略空白差异（谨慎使用）
git apply --whitespace=nowarn mypatch.patch

# 三路合并方式应用（冲突时尝试）
git apply --3way mypatch.patch

# 应用目录下所有补丁
git apply *.patch
```

### 5.4 创建补丁

```sh
# 从最近 1 个提交创建补丁
git format-patch -1 HEAD

# 从最近 3 个提交创建补丁（生成 3 个文件）
git format-patch -3 HEAD

# 指定输出目录
git format-patch -1 HEAD -o ./patches/

# 创建包含二进制文件的补丁
git format-patch -1 HEAD --binary
```

### 5.5 撤销已应用的补丁

```sh
# 反向应用（撤销）补丁
git apply -R mypatch.patch

# 或重置整个工作区
git checkout .
git clean -fd
```

### 5.6 patch.sh 脚本解析

`patch.sh ant` 的执行流程：

```sh
# 1. 复制补丁文件到对应子目录
cp ./patch/*v0.39-v1.patch ./plutosdr-fw/hdl
cp ./patch/ant/*linux.patch ./plutosdr-fw/linux
# ... 其他补丁

# 2. 检查每个子模块的补丁是否可以干净应用
cd ./plutosdr-fw/hdl
git apply --stat *.patch   # 显示变更统计
git apply --check *.patch  # 验证可应用

# 3. 实际应用补丁并清理补丁文件
git apply *.patch
rm -rf *.patch
```

### 5.7 修改已有补丁

如果需要自定义 E310 的配置：

```sh
# 进入目标子模块（例如 linux）
cd ~/work/antsdr-fw-patch/plutosdr-fw/linux

# 应用原始补丁
git apply ../../patch/ant/*linux.patch

# 进行自己的修改
vim arch/arm/boot/dts/zynq-ant.dts

# 暂存修改
git add -A

# 生成包含所有修改的新补丁
git format-patch HEAD~1..HEAD -o /tmp/my-linux-patch/
# 或直接生成完整补丁覆盖原文件：
git diff HEAD > ../../patch/ant/0001-add-support-linux.patch
```

---

## 6. Vivado 基础操作

### 6.1 启动 Vivado

```sh
# 先 source 环境
source /opt/Xilinx/Vivado/2023.2/settings64.sh

# GUI 模式
vivado &

# 批处理模式（无 GUI）
vivado -mode batch -source script.tcl

# TCL 交互模式
vivado -mode tcl
```

### 6.2 打开 E310 工程

```sh
# 进入 HDL 工程目录
cd ~/work/antsdr-fw-patch/plutosdr-fw/hdl/projects/ant/

# 用 Vivado GUI 打开
vivado ant.xpr &
```

### 6.3 常用 Vivado TCL 命令

在 Vivado TCL Console 或批处理脚本中：

```tcl
# 打开工程
open_project ant.xpr

# 运行综合
launch_runs synth_1
wait_on_run synth_1

# 运行实现
launch_runs impl_1
wait_on_run impl_1

# 生成位流
launch_runs impl_1 -to_step write_bitstream
wait_on_run impl_1

# 导出硬件平台（用于 SDK/Vitis）
write_hw_platform -fixed -include_bit -force system_top.xsa

# 查看时序报告
report_timing_summary

# 查看资源利用率
report_utilization

# 关闭工程
close_project

# 退出 Vivado
exit
```

### 6.4 只重新生成位流（不重新综合）

在 Makefile 构建完成后，如果只修改了 HDL，可以在 Vivado 中只重新实现：

```sh
cd ~/work/antsdr-fw-patch/plutosdr-fw/hdl/projects/ant/

vivado -mode batch -source << 'EOF'
open_project ant.xpr
launch_runs impl_1 -to_step write_bitstream
wait_on_run impl_1
close_project
exit
EOF
```

### 6.5 查看 Vivado 综合日志

```sh
# 综合运行日志
cat ant.runs/synth_1/runme.log

# 实现运行日志
cat ant.runs/impl_1/runme.log

# 位流生成日志
cat ant.runs/impl_1/write_bitstream.pb
```

### 6.6 时序约束文件（XDC）

E310 的引脚约束在：
```sh
cat ~/work/antsdr-fw-patch/plutosdr-fw/hdl/projects/ant/system_constr.xdc
```

格式示例：
```tcl
# 设置引脚位置
set_property PACKAGE_PIN U18 [get_ports {gpio_bd[0]}]
# 设置 IO 标准
set_property IOSTANDARD LVCMOS33 [get_ports {gpio_bd[0]}]
```

---

## 7. SD 卡操作工具

### 7.1 查看磁盘信息

```sh
# 列出所有磁盘和分区
lsblk

# 查看 SD 卡详情（假设是 /dev/sdb）
sudo fdisk -l /dev/sdb

# 查看文件系统信息
sudo blkid /dev/sdb1
```

### 7.2 分区和格式化

```sh
# 清除分区表，重新分区
sudo fdisk /dev/sdb
# 输入：
# o  → 创建新的 MBR 分区表
# n  → 新建分区
# p  → 主分区
# 1  → 分区号 1
# (回车) → 默认起始扇区
# +256M → 大小 256 MB
# t  → 修改分区类型
# b  → W95 FAT32
# w  → 写入并退出

# 格式化为 FAT32
sudo mkfs.vfat -F 32 -n "ANTSDR" /dev/sdb1
```

### 7.3 挂载和复制文件

```sh
# 挂载
sudo mkdir -p /mnt/sdcard
sudo mount /dev/sdb1 /mnt/sdcard

# 复制 SD 卡启动文件
sudo cp ~/work/antsdr-fw-patch/plutosdr-fw/build_sdimg/* /mnt/sdcard/

# 查看已复制的文件
ls -la /mnt/sdcard/

# 卸载（重要：确保数据写入）
sudo umount /mnt/sdcard
sync
```

### 7.4 使用 dd 写入完整镜像

```sh
# 写入（⚠️ 会清除 SD 卡所有数据，务必确认设备节点）
sudo dd if=antsdr-sdimg.img of=/dev/sdb bs=4M status=progress oflag=sync

# 验证写入
sudo dd if=/dev/sdb bs=4M count=100 | md5sum
```

---

## 8. 网络连接与 SSH

### 8.1 USB RNDIS 网络（QSPI 启动）

E310 通过 USB OTG 提供虚拟以太网（RNDIS），默认 IP 为 `192.168.1.10`。

```sh
# 检查主机是否识别到 RNDIS 接口
ip addr show | grep -A2 "usb\|rndis"

# 为主机端 RNDIS 接口分配 IP
sudo ip addr add 192.168.1.1/24 dev usb0   # 接口名可能不同

# 测试连通性
ping 192.168.1.10

# SSH 连接
ssh root@192.168.1.10
# 默认密码：analog
```

### 8.2 以太网连接

```sh
# 确保主机和 E310 在同一网段
# E310 默认以太网 IP：通过 DHCP 或固定（参考 uEnv.txt）

# 扫描局域网查找 E310
arp-scan --localnet | grep -i "micro"

# 或检查路由器 DHCP 表
```

### 8.3 SSH 常用操作

```sh
# 连接
ssh root@192.168.1.10

# 无密码 SSH（配置公钥）
ssh-copy-id root@192.168.1.10

# 传文件到设备
scp ./myfile root@192.168.1.10:/tmp/

# 从设备下载文件
scp root@192.168.1.10:/etc/profile ./

# 远程执行命令（不进入交互模式）
ssh root@192.168.1.10 "uname -a && cat /proc/cpuinfo"

# 建立 SOCKS 代理（调试用）
ssh -D 1080 root@192.168.1.10
```

### 8.4 设备端常用命令

登录到 E310 后，常用诊断命令：

```sh
# 系统信息
uname -a
cat /proc/cpuinfo
cat /proc/meminfo
df -h

# 查看 AD9361 驱动加载状态
lsmod | grep ad9361
dmesg | grep -i "ad9361\|iio"

# 查看 IIO 设备
ls /sys/bus/iio/devices/
cat /sys/bus/iio/devices/iio:device0/name

# 查看 FPGA 时钟和配置
cat /sys/kernel/debug/clk/clk_summary 2>/dev/null

# 网络状态
ip addr
ip route

# 查看环境变量（Linux 端）
fw_printenv          # U-Boot 环境变量
fw_printenv mode     # 查看当前 RF 模式
```

---

## 附录：常用命令速查表

| 任务 | 命令 |
|------|------|
| 进入 DFU 模式后列出设备 | `sudo dfu-util -l` |
| 烧录完整固件 | `sudo dfu-util -a firmware.dfu -D ./ant.dfu` |
| 烧录 Bootloader | `sudo dfu-util -a boot.dfu -D ./boot.dfu` |
| 烧录 U-Boot 环境 | `sudo dfu-util -a uboot-env.dfu -D ./uboot-env.dfu` |
| 串口连接 115200 | `sudo minicom -D /dev/ttyUSB0 -b 115200` |
| 查看 IIO 设备 | `iio_info -u ip:192.168.1.10` |
| 设置 RX 频率 | `iio_attr -u ip:192.168.1.10 -c ad9361-phy altvoltage0 frequency <Hz>` |
| 检查补丁 | `git apply --check mypatch.patch` |
| 应用补丁 | `git apply mypatch.patch` |
| 生成补丁 | `git format-patch -1 HEAD` |
| 开启 Vivado | `source /opt/Xilinx/Vivado/2023.2/settings64.sh && vivado &` |
| SSH 登录 E310 | `ssh root@192.168.1.10` (密码: analog) |
| 设置 2R2T (设备端) | `fw_setenv mode 2r2t && reboot` |
