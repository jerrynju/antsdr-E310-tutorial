# ANTSDR E310 故障排查手册

---

## 目录

1. [构建环境问题](#1-构建环境问题)
2. [补丁应用失败](#2-补丁应用失败)
3. [编译错误](#3-编译错误)
4. [DFU 烧录问题](#4-dfu-烧录问题)
5. [设备启动问题](#5-设备启动问题)
6. [网络连接问题](#6-网络连接问题)
7. [RF 功能问题](#7-rf-功能问题)

---

## 1. 构建环境问题

### 错误：`arm-linux-gnueabihf-gcc: command not found`

**原因**：工具链路径未加入 PATH，或工具链未安装。

```sh
# 检查工具链是否存在
ls ~/toolchain/gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf/bin/

# 重新添加到 PATH
export PATH=$PATH:$HOME/toolchain/gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf/bin

# 验证
which arm-linux-gnueabihf-gcc
```

---

### 错误：`Vivado not found` 或 `vivado: command not found`

**原因**：Vivado settings 未 source，或安装路径不对。

```sh
# 检查 Vivado 是否安装
ls /opt/Xilinx/Vivado/2023.2/settings64.sh

# Source 环境
source /opt/Xilinx/Vivado/2023.2/settings64.sh

# 如果安装在其他路径
find /opt -name "settings64.sh" 2>/dev/null
```

---

### 错误：`sudo: preserve environment failed`

**原因**：sudo 默认不保留用户环境变量，导致 `CROSS_COMPILE` 等变量丢失。

```sh
# 正确方式：使用 -E 参数
sudo -E make

# 或在 sudo 会话中重新设置变量
sudo bash -c "export CROSS_COMPILE=arm-linux-gnueabihf- && \
export PATH=\$PATH:$HOME/toolchain/gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf/bin && \
export VIVADO_SETTINGS=/opt/Xilinx/Vivado/2023.2/settings64.sh && \
make"
```

---

### 错误：`python: command not found`

**原因**：Ubuntu 22.04 默认只有 `python3`，部分脚本调用 `python`。

```sh
# 创建软链接
sudo ln -sf /usr/bin/python3 /usr/local/bin/python

# 或安装兼容包
sudo apt-get install python-is-python3
```

---

## 2. 补丁应用失败

### 错误：`error: patch failed: Makefile:XX`

**原因**：子模块源码已被修改，与补丁期望的基础版本不符。

```sh
# 重置子模块到干净状态
cd ~/work/antsdr-fw-patch

# 方法1：使用仓库提供的重置脚本
sh resetGit.sh

# 方法2：手动重置
cd plutosdr-fw
git submodule foreach 'git checkout -- . && git clean -fd'
git checkout -- .
cd ..

# 重新应用补丁
sh patch.sh ant
```

---

### 错误：`error: No such file or directory`（补丁引用的文件不存在）

**原因**：子模块版本不对，或子模块未正确初始化。

```sh
# 确认子模块已初始化
git submodule status

# 如有 - 号开头（未初始化），执行：
git submodule update --init --recursive

# 确认子模块版本正确（应为 v0.39 对应的 commit）
cd plutosdr-fw && git log --oneline -3
```

---

### 错误：补丁应用后，patch.sh 再次运行报错

**原因**：补丁只能应用一次，已经应用后再次运行会冲突。

```sh
# 检查是否已经应用过
cd plutosdr-fw && git status
# 如果显示文件已修改，说明补丁已应用

# 如果需要重新应用，先重置
sh resetGit.sh
sh patch.sh ant
```

---

## 3. 编译错误

### 错误：Vivado 综合 License 错误

```
ERROR: [Common 17-69] Command failed: This design contains cells for
which bitstream generation is not permitted
```

**原因**：Vivado License 不支持 Zynq-7020，或 License 已过期。

**解决**：
1. 使用 Vivado ML Standard（免费版，支持 Zynq-7000 系列的一部分）
2. 或申请 30 天评估 License：访问 AMD 官网

---

### 错误：`make[2]: *** [linux/Makefile] Error 2`（Linux 内核编译失败）

```sh
# 查看详细错误
sudo -E make V=1 2>&1 | grep -A5 "error:"

# 常见原因1：工具链版本不对
arm-linux-gnueabihf-gcc --version  # 必须是 7.3.1

# 常见原因2：内核配置文件缺失
ls plutosdr-fw/linux/arch/arm/configs/ | grep ant
# 应该有 zynq_ant_defconfig 文件

# 尝试单独编译内核定位问题
cd plutosdr-fw/linux
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- zynq_ant_defconfig
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- zImage -j4
```

---

### 错误：Buildroot 下载包失败（网络超时）

**原因**：Buildroot 需要从互联网下载源码包。

```sh
# 为 Buildroot 配置下载镜像或代理
export BR2_WGET="wget --timeout=30 --tries=3"

# 或预先下载所需包（BR2_DL_DIR）
export BR2_DL_DIR=/path/to/downloaded/packages

# 如果某个包下载失败，手动下载后放到 dl/ 目录
ls plutosdr-fw/buildroot/dl/
```

---

### 错误：磁盘空间不足

```sh
# 检查可用空间
df -h

# 清理构建缓存（重新构建更耗时）
cd plutosdr-fw
sudo make clean

# 或只清理特定部分
sudo make linux-clean
sudo make uboot-clean
```

---

## 4. DFU 烧录问题

### 问题：DFU 设备未识别

**检查步骤：**

```sh
# 1. 确认设备已进入 DFU 模式（两个 LED 同时绿色）

# 2. 检查 USB 设备列表
lsusb | grep -i "0456"
# 应看到：Bus XXX Device XXX: ID 0456:b673 Analog Devices, Inc.

# 3. 检查内核日志
dmesg | tail -20 | grep -i "usb\|DFU"

# 4. 检查 udev 权限
ls -la /dev/bus/usb/**/* | grep -i "0456\|b673"
```

---

### 问题：烧录过程中断连

**原因**：USB 线质量差，或 DFU 超时。

```sh
# 使用更短、质量好的 USB 线
# 避免使用 USB Hub，直连电脑端口

# 如果中途断开，重新进入 DFU 模式后继续（从头烧录）
sudo dfu-util -a firmware.dfu -D ./ant.dfu

# 增加超时时间（如有 --timeout 参数）
sudo dfu-util -a firmware.dfu -D ./ant.dfu -t 65535
```

---

### 问题：烧录 `boot.dfu` 后设备无法启动

**原因**：boot.dfu 与固件版本不匹配，或烧录不完整。

**恢复步骤：**
1. 将启动模式切换到 SD 卡模式
2. 制作一张有效的 SD 卡启动盘
3. 从 SD 卡启动后，重新通过 DFU 烧录正确的 `boot.dfu`

---

## 5. 设备启动问题

### 问题：串口无输出

**检查：**
```sh
# 确认串口设备节点
ls /dev/ttyUSB* /dev/ttyACM*

# 确认波特率 115200
sudo minicom -D /dev/ttyUSB0 -b 115200

# 检查用户是否有权限访问串口
sudo usermod -aG dialout $USER
# 注销重新登录后生效
```

---

### 问题：U-Boot 启动卡住

**串口输出示例：**
```
U-Boot 2022.04 (...)
...
Starting kernel ...
(此处卡住)
```

**可能原因和解决：**
```sh
# 在 U-Boot 命令行检查 FIT 镜像
bootm 0x3000000#conf@zynq-ant.dtb  # 指定正确的配置节点

# 检查设备树是否正确加载
fdt addr 0x2A00000
fdt print /  # 打印设备树内容

# 检查内存地址是否冲突
bdinfo  # 查看内存布局
```

---

### 问题：Linux 启动后网络不通

```sh
# 在设备串口端执行
ifconfig -a     # 查看网络接口
ip addr         # 查看 IP 配置
ip link set eth0 up  # 确保接口已激活

# 查看以太网 PHY 状态
dmesg | grep -i "eth\|phy\|mac"
```

---

## 6. 网络连接问题

### 问题：USB RNDIS 接口不出现在主机

```sh
# Ubuntu 通常自动加载 RNDIS 驱动
lsmod | grep rndis

# 手动加载
sudo modprobe rndis_host

# 检查接口名（可能是 usb0 或 enxXXXXXX）
ip addr show | grep -E "usb|enx"

# 手动配置 IP
sudo ip addr add 192.168.1.1/24 dev usb0
sudo ip link set usb0 up
```

---

### 问题：SSH 连接超时

```sh
# 确认 IP 可达
ping 192.168.1.10

# 确认 SSH 服务在运行（设备串口端执行）
ps aux | grep sshd
/etc/init.d/sshd start  # 或 systemctl start sshd

# 检查防火墙
iptables -L -n | grep 22
```

---

## 7. RF 功能问题

### 问题：iio_info 找不到 ad9361-phy

```sh
# 在设备端检查驱动加载
dmesg | grep -i "ad9361\|spi"
lsmod | grep -i "ad9361"

# 检查 SPI 设备节点
ls /dev/spi*

# 检查 IIO 设备
ls /sys/bus/iio/devices/

# 重新加载驱动（如果支持）
rmmod cf_axi_adc cf_axi_dds_core_v2 ad9361 2>/dev/null
modprobe ad9361
```

---

### 问题：2R2T 模式配置后仍然是 1R1T

```sh
# 在设备端验证环境变量是否写入
fw_printenv mode    # 应显示 mode=2r2t
fw_printenv compatible  # 应显示 compatible=ad9361

# 如果变量正确但模式仍然是 1R1T，检查内核启动参数
cat /proc/cmdline | grep mode

# 强制验证 AD9361 通道数
iio_attr -u ip:localhost -d ad9361-phy -g mode
```

---

### 问题：RF 性能异常（信噪比差、频率偏移大）

```sh
# 检查采样率和带宽设置是否合理
iio_attr -u ip:192.168.1.10 -c ad9361-phy voltage0 sampling_frequency
iio_attr -u ip:192.168.1.10 -c ad9361-phy voltage0 rf_bandwidth

# 检查 LO 频率设置
iio_attr -u ip:192.168.1.10 -c ad9361-phy altvoltage0 frequency  # RX LO

# 确认天线已正确连接（SMA 接口）
# 确认增益设置合理（过高会导致饱和）
iio_attr -u ip:192.168.1.10 -c ad9361-phy voltage0 hardwaregain
```

---

## 附录：日志收集

遇到问题时，提供以下信息有助于分析：

```sh
# 1. 系统版本
uname -a
cat /etc/issue 2>/dev/null

# 2. 固件版本
cat /etc/build_date 2>/dev/null
cat /etc/VERSIONS 2>/dev/null

# 3. 内核启动日志
dmesg > /tmp/dmesg.log

# 4. IIO 设备信息
iio_info -u ip:localhost > /tmp/iio_info.log 2>&1

# 5. 网络状态
ip addr > /tmp/network.log
ip route >> /tmp/network.log
```
