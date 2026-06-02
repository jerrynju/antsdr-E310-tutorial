# ANTSDR E310 固件构建完整指南

> 面向新手的从零开始教程，涵盖环境搭建、源码编译、固件烧录全流程。

---

## 目录

1. [硬件概述](#1-硬件概述)
2. [固件架构](#2-固件架构)
3. [主机环境要求](#3-主机环境要求)
4. [安装依赖软件](#4-安装依赖软件)
5. [安装 Xilinx Vivado 2023.2](#5-安装-xilinx-vivado-20232)
6. [安装 ARM 交叉编译工具链](#6-安装-arm-交叉编译工具链)
7. [获取源代码](#7-获取源代码)
8. [配置环境变量](#8-配置环境变量)
9. [应用硬件补丁](#9-应用硬件补丁)
10. [编译固件](#10-编译固件)
11. [理解构建产物](#11-理解构建产物)
12. [SD 卡启动镜像制作](#12-sd-卡启动镜像制作)
13. [DFU 方式烧录固件](#13-dfu-方式烧录固件)
14. [配置 2R2T 模式](#14-配置-2r2t-模式)
15. [验证固件功能](#15-验证固件功能)

---

## 1. 硬件概述

### 1.1 ANTSDR E310 是什么

ANTSDR E310 是微相科技出品的软件无线电（SDR）平台，核心硬件由两颗芯片组成：

| 组件 | 型号 | 说明 |
|------|------|------|
| SoC | Xilinx Zynq-7020 | 双核 ARM Cortex-A9 + 可编程逻辑 (FPGA) |
| RF 收发器 | Analog Devices AD9361 | 覆盖 70 MHz–6 GHz，最大 2T2R |

**关键参数：**
- 内存：512 MB DDR3
- Flash：256 MB QSPI NOR Flash（存储固件）
- 接口：USB OTG（DFU 烧录）、千兆以太网、SMA 天线接口
- 启动模式：QSPI Flash 启动 / SD 卡启动（通过拨码开关切换）

### 1.2 拨码开关说明

E310 板上有跳线帽或拨码开关，用于选择启动模式：

```
BOOT MODE:
  QSPI Flash 启动 → 正常工作模式（固件已烧录到 Flash）
  SD Card 启动    → 开发调试模式（从 SD 卡加载固件）
```

> **新手建议**：先用 SD 卡启动模式测试新固件，确认无误后再烧录到 Flash。

---

## 2. 固件架构

### 2.1 分层结构

ANTSDR E310 固件基于 Analog Devices PlutoSDR 固件框架，由以下层次组成：

```
┌─────────────────────────────────────────┐
│          用户应用层 (GNU Radio / libiio) │
├─────────────────────────────────────────┤
│       根文件系统 (Buildroot rootfs)      │  ← Linux 用户空间
├─────────────────────────────────────────┤
│         Linux 内核 (zImage)             │  ← 操作系统
├─────────────────────────────────────────┤
│     设备树 (zynq-ant.dtb)               │  ← 硬件描述
├─────────────────────────────────────────┤
│         U-Boot 引导程序                  │  ← Bootloader
├─────────────────────────────────────────┤
│     FSBL (第一阶段引导程序)              │  ← Zynq PS 初始化
├─────────────────────────────────────────┤
│     FPGA 位流 (system_top.bit)          │  ← 可编程逻辑
└─────────────────────────────────────────┘
                    硬件
```

### 2.2 各组件职责

| 组件 | 文件 | 职责 |
|------|------|------|
| FPGA 位流 | `system_top.bit` | 实现 AD9361 高速数据接口、DMA 控制器 |
| FSBL | 内嵌于 `boot.bin` | 初始化 DDR，加载 U-Boot |
| U-Boot | `u-boot.elf` | 硬件初始化，加载 Linux 内核 |
| Linux 内核 | `zImage` | 操作系统，管理设备驱动 |
| 设备树 | `zynq-ant.dtb` | 描述板级硬件连接关系 |
| 根文件系统 | `rootfs.cpio.gz` | 用户空间工具（libiio、ad9361等） |

### 2.3 构建工具链

```
源码仓库 (antsdr-fw-patch)
    │
    ├── plutosdr-fw/        ← 主构建框架 (submodule)
    │   ├── hdl/            ← FPGA HDL 源码 (submodule)
    │   ├── linux/          ← Linux 内核源码 (submodule)
    │   ├── buildroot/      ← 根文件系统构建 (submodule)
    │   └── u-boot-xlnx/   ← U-Boot 源码 (submodule)
    │
    └── patch/              ← E310 专用补丁文件
        └── ant/
```

---

## 3. 主机环境要求

### 3.1 操作系统

**推荐：Ubuntu 20.04 LTS 或 Ubuntu 22.04 LTS（64位）**

> Vivado 2023.2 和交叉编译工具链在 Ubuntu 上支持最佳，Windows 不支持（Vivado 支持，但 Makefile 构建不支持）。

检查系统版本：
```sh
lsb_release -a
uname -m   # 应显示 x86_64
```

### 3.2 硬件要求

| 资源 | 最低配置 | 推荐配置 |
|------|----------|----------|
| CPU | 4 核 | 8 核以上 |
| 内存 | 16 GB | 32 GB |
| 磁盘空间 | 200 GB | 500 GB |
| 网络 | 需要访问 GitHub | 稳定宽带 |

> **磁盘说明**：Vivado 安装约 60 GB，源码约 20 GB，构建缓存约 50–100 GB。

---

## 4. 安装依赖软件

### 4.1 一键安装脚本

```sh
# 更新包列表
sudo apt-get update

# 基础构建工具
sudo apt-get install -y \
    git build-essential fakeroot \
    libncurses5-dev libssl-dev ccache \
    dfu-util u-boot-tools device-tree-compiler mtools \
    bc python3 cpio zip unzip rsync file wget curl \
    libtinfo5 bison flex libmpc-dev

# 移除可能冲突的包
sudo apt-get purge -y gcc-arm-linux-gnueabihf
sudo apt-get remove -y libfdt-dev 2>/dev/null || true

# 安装 Python 兼容层（部分脚本需要 python 命令）
sudo apt-get install -y python-is-python3 2>/dev/null || \
    sudo ln -sf /usr/bin/python3 /usr/local/bin/python
```

### 4.2 验证安装

```sh
# 验证关键工具
git --version        # 应显示 2.x.x
make --version       # 应显示 GNU Make 4.x
dfu-util --version   # 应显示版本信息
dtc --version        # Device Tree Compiler
```

---

## 5. 安装 Xilinx Vivado 2023.2

### 5.1 下载安装包

从 AMD/Xilinx 官网下载（需注册账号）：

```
文件名：FPGAs_AdaptiveSoCs_Unified_2023.2_1013_2256.tar.gz
大小：约 100 GB（完整安装包）
```

> **提示**：下载速度较慢，建议使用稳定网络或 Xilinx 下载管理器。

### 5.2 安装步骤

```sh
# 解压安装包
tar -xvf FPGAs_AdaptiveSoCs_Unified_2023.2_1013_2256.tar.gz
cd FPGAs_AdaptiveSoCs_Unified_2023.2_1013_2256

# 运行安装程序（GUI 模式）
./xsetup

# 或静默安装（需先配置 install_config.txt）
./xsetup --agree XilinxEULA,3rdPartyEULA --batch Install \
    --config install_config.txt
```

**安装时选择：**
1. 产品：**Vivado** (不需要 Vitis)
2. 版本：**Vivado ML Standard** 或 **Enterprise**（取决于 License）
3. 设备：勾选 **Zynq-7000** 系列
4. 安装路径：`/opt/Xilinx`（推荐）

### 5.3 安装 License

Vivado 需要有效的 License 才能综合 Zynq-7020 设计。
- 访问 AMD 官网申请 **Node-Locked License** 或 **Vivado ML Standard（免费版）**
- 将 License 文件放置到 `~/.Xilinx/Vivado/` 目录

### 5.4 验证 Vivado 安装

```sh
source /opt/Xilinx/Vivado/2023.2/settings64.sh
vivado -version
# 应输出：Vivado v2023.2 (64-bit)
```

---

## 6. 安装 ARM 交叉编译工具链

### 6.1 下载工具链

使用 **Linaro GCC 7.3-2018.05**（与 Buildroot 兼容版本）：

```sh
# 创建工具链目录
mkdir -p ~/toolchain
cd ~/toolchain

# 下载 (适用于 64位 Linux 主机)
wget https://releases.linaro.org/components/toolchain/binaries/7.3-2018.05/arm-linux-gnueabihf/gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf.tar.xz

# 解压
tar -xf gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf.tar.xz
```

> **注意**：如果主机是 32 位系统（少见），需下载 `i686` 版本的工具链。

### 6.2 验证工具链

```sh
~/toolchain/gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-gcc --version
# 应输出：arm-linux-gnueabihf-gcc (Linaro GCC 7.3-2018.05) 7.3.1 20180425
```

---

## 7. 获取源代码

### 7.1 克隆仓库

```sh
# 进入工作目录（根据自己习惯选择）
mkdir -p ~/work && cd ~/work

# 克隆仓库（v0.39 分支，包含所有子模块）
git clone -b v0.39 --recursive https://github.com/MicroPhase/antsdr-fw-patch.git

# 进入仓库目录
cd antsdr-fw-patch
```

> **耗时说明**：`--recursive` 会同时克隆 `plutosdr-fw`、`hdl`、`linux`、`buildroot`、`u-boot-xlnx` 等子模块，总体积约 10–20 GB，需要较长时间。

### 7.2 网络问题处理

如果 GitHub 访问不稳定，可以配置 Git 代理或分步克隆：

```sh
# 方法1：设置 Git HTTP 代理
git config --global http.proxy http://127.0.0.1:7890

# 方法2：先克隆主仓库，再逐步初始化子模块
git clone -b v0.39 https://github.com/MicroPhase/antsdr-fw-patch.git
cd antsdr-fw-patch
git submodule update --init plutosdr-fw
cd plutosdr-fw
git submodule update --init --recursive
```

### 7.3 仓库结构说明

```
antsdr-fw-patch/
├── README.md               # 快速参考文档
├── patch.sh                # 补丁应用脚本
├── resetGit.sh             # 重置 git 状态脚本
├── .gitmodules             # 子模块配置
│
├── patch/                  # E310 专用补丁
│   ├── *v0.39-v1.patch     # HDL 通用补丁
│   ├── ant/                # E310 (原版) 补丁
│   │   ├── *makefile*.patch
│   │   ├── *buildroot*.patch
│   │   ├── *linux*.patch
│   │   └── *uboot*.patch
│   ├── e200/               # E200 补丁
│   └── e310v2/             # E310V2 补丁
│
└── plutosdr-fw/            # 主构建框架（子模块）
    ├── Makefile            # 主 Makefile
    ├── hdl/                # FPGA HDL 工程
    ├── linux/              # Linux 内核源码
    ├── buildroot/          # 根文件系统
    └── u-boot-xlnx/        # U-Boot 源码
```

---

## 8. 配置环境变量

### 8.1 临时配置（当前终端有效）

每次打开新终端都需要重新执行：

```sh
# ARM 交叉编译器前缀
export CROSS_COMPILE=arm-linux-gnueabihf-

# 将工具链加入 PATH（根据实际解压路径修改）
export PATH=$PATH:$HOME/toolchain/gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf/bin

# Vivado 环境设置
export VIVADO_SETTINGS=/opt/Xilinx/Vivado/2023.2/settings64.sh

# 指定目标硬件（E310 原版）
export TARGET=ant
```

### 8.2 永久配置（推荐）

将变量写入 `~/.bashrc`，开机自动生效：

```sh
cat >> ~/.bashrc << 'EOF'

# ANTSDR E310 构建环境
export CROSS_COMPILE=arm-linux-gnueabihf-
export PATH=$PATH:$HOME/toolchain/gcc-linaro-7.3.1-2018.05-x86_64_arm-linux-gnueabihf/bin
export VIVADO_SETTINGS=/opt/Xilinx/Vivado/2023.2/settings64.sh
EOF

# 使配置立即生效
source ~/.bashrc
```

### 8.3 验证环境变量

```sh
which arm-linux-gnueabihf-gcc  # 应输出工具链路径
echo $VIVADO_SETTINGS          # 应输出 Vivado settings 路径
arm-linux-gnueabihf-gcc --version  # 应输出版本信息
```

---

## 9. 应用硬件补丁

### 9.1 为什么需要补丁

PlutoSDR 固件框架是为 AD-PLUTO 硬件设计的。ANTSDR E310 虽然使用相同的 AD9361 芯片，但板级连接（引脚分配、外设配置）不同，需要通过 Git patch 文件修改相关源码。

补丁内容包括：
- **HDL 补丁**：添加 E310 的 FPGA 引脚约束和设计
- **U-Boot 补丁**：修改引导参数和硬件初始化
- **Linux 补丁**：添加 E310 设备树和驱动配置
- **Buildroot 补丁**：定制根文件系统内容
- **Makefile 补丁**：添加 E310 目标构建规则

### 9.2 应用补丁

```sh
# 确保在仓库根目录
cd ~/work/antsdr-fw-patch

# 为 E310 应用补丁（TARGET=ant）
sh patch.sh ant
```

**成功输出示例：**
```
Patch check...
 Makefile | 29 ++++++++-
 scripts/antsdr.its | 174 +++++++++++++++...
 3 files changed, 211 insertions(+), 2 deletions(-)
Patch...
patch finish
```

### 9.3 补丁失败处理

如果遇到 `error: patch failed` 错误：

```sh
# 重置子模块到干净状态
sh resetGit.sh

# 或手动重置
cd plutosdr-fw
git submodule foreach 'git checkout .'
git checkout .
cd ..

# 重新应用补丁
sh patch.sh ant
```

---

## 10. 编译固件

### 10.1 进入构建目录

```sh
cd ~/work/antsdr-fw-patch/plutosdr-fw
```

### 10.2 启动构建

```sh
# 使用 sudo -E 保留当前用户的环境变量
sudo -E make
```

> **为什么需要 sudo？** Buildroot 在构建根文件系统时需要 root 权限来设置文件所有权和设备节点。`-E` 参数确保 sudo 继承 `CROSS_COMPILE`、`PATH` 等已配置的环境变量。

### 10.3 构建时间

| 阶段 | 预计时间 | 说明 |
|------|----------|------|
| FPGA 综合 | 30–90 分钟 | 取决于 CPU 核心数 |
| U-Boot 编译 | 5–10 分钟 | |
| Linux 内核编译 | 15–30 分钟 | |
| Buildroot 构建 | 30–60 分钟 | 首次构建较慢（需下载包） |
| **总计** | **1.5–3 小时** | |

### 10.4 使用多核加速编译

Buildroot 默认使用多核，但可以显式指定：

```sh
# 使用 8 核编译（根据实际 CPU 核心数调整）
sudo -E make -j8
```

### 10.5 查看构建日志

构建过程中如有错误，查看详细日志：

```sh
# 查看 Vivado 综合日志
cat plutosdr-fw/hdl/projects/ant/ant.runs/synth_1/runme.log | tail -50

# 查看 Linux 编译日志
sudo -E make V=1   # 显示详细编译命令
```

---

## 11. 理解构建产物

### 11.1 build 目录文件清单

编译成功后，所有产物在 `plutosdr-fw/build/` 目录：

```
build/
├── ant.dfu              # DFU 固件包（完整系统镜像）
├── ant.frm              # 固件升级包（通过网页升级使用）
├── ant.frm.md5          # 固件校验文件
├── ant.itb              # FIT 镜像（内核+设备树+rootfs）
├── boot.bin             # 启动镜像（FSBL+FPGA位流+U-Boot）
├── boot.dfu             # boot.bin 的 DFU 格式
├── boot.frm             # boot.bin 的固件升级格式
├── system_top.bit       # FPGA 位流文件
├── u-boot.elf           # U-Boot ELF 文件
├── uboot-env.bin        # U-Boot 环境变量
├── uboot-env.dfu        # U-Boot 环境变量 DFU 格式
├── uboot-env.txt        # U-Boot 环境变量文本格式
├── zImage               # Linux 内核压缩镜像
├── zynq-ant.dtb         # 设备树二进制
├── rootfs.cpio.gz       # 根文件系统压缩包
└── antsdr-fw-v*.zip     # 完整固件打包（用于分发）
```

### 11.2 文件格式说明

| 格式 | 说明 | 使用场景 |
|------|------|----------|
| `.dfu` | DFU 更新格式 | 通过 USB OTG + dfu-util 烧录 |
| `.frm` | 固件包 | 通过设备网页界面升级 |
| `.itb` | FIT 镜像 | U-Boot 启动用的统一镜像 |
| `.bit` | FPGA 位流 | JTAG 调试时直接下载 |
| `.bin` | 二进制启动包 | SD 卡 / QSPI 的第一段镜像 |

### 11.3 boot.bin 内部结构

`boot.bin` 由 Xilinx `bootgen` 工具打包，内含：
```
boot.bin = FSBL.elf + system_top.bit + u-boot.elf
```

---

## 12. SD 卡启动镜像制作

### 12.1 生成 SD 卡镜像

```sh
cd ~/work/antsdr-fw-patch/plutosdr-fw
sudo -E make sdimg
```

生成的文件在 `build_sdimg/` 目录：
```
build_sdimg/
├── boot.bin         # 放到 FAT32 分区
├── uImage           # Linux 内核（SD 卡格式）
├── system.dtb       # 设备树
├── rootfs.cpio.gz   # 根文件系统
└── uEnv.txt         # U-Boot 环境变量
```

### 12.2 制作 SD 卡

**方法一：使用 `dd` 写入镜像（如有完整镜像文件）**

```sh
# 确认 SD 卡设备节点（务必确认正确，错误会损坏数据！）
lsblk
# 假设 SD 卡是 /dev/sdb

sudo dd if=antsdr-sdimg.img of=/dev/sdb bs=4M status=progress
sync
```

**方法二：手动分区并复制文件**

```sh
# 1. 分区 SD 卡（至少需要一个 FAT32 分区）
sudo fdisk /dev/sdb
# 创建主分区，类型 b (W95 FAT32)，大小 ≥ 128 MB

# 2. 格式化
sudo mkfs.vfat -F 32 /dev/sdb1

# 3. 挂载
sudo mount /dev/sdb1 /mnt/sdcard

# 4. 复制文件
sudo cp build_sdimg/* /mnt/sdcard/

# 5. 卸载
sudo umount /mnt/sdcard
sync
```

### 12.3 设置 E310 从 SD 卡启动

1. 将制作好的 SD 卡插入 E310 的 SD 卡槽
2. 将启动模式跳线拨到 **SD 卡启动** 位置
3. 接通电源，等待系统启动（约 30–60 秒）
4. 通过串口或 SSH 连接验证启动是否成功

---

## 13. DFU 方式烧录固件

DFU（Device Firmware Upgrade）是通过 USB OTG 接口将固件直接写入 QSPI Flash 的方法。

> **适用设备**：E310（原版）和 E310V2（E316）。E200 不支持 DFU。

### 13.1 进入 DFU 模式

1. 确保 E310 处于 **QSPI Flash 启动** 模式（跳线设置）
2. 用 Micro USB 线连接 E310 的 OTG 接口到电脑
3. 接通 E310 电源
4. **在上电瞬间按下 DFU 按钮**（或上电前按住）
5. 观察指示灯：两个 LED 同时变为绿色，表示已进入 DFU 模式

### 13.2 验证 DFU 设备识别

```sh
# 列出 DFU 设备
sudo dfu-util -l

# 正常输出应包含：
# Found DFU: [0456:b673] ...
# Deduplicating alternate setting, value: ...
```

如果未识别到设备：
```sh
# 检查 USB 设备列表
lsusb | grep -i "Analog Devices\|AD936\|DFU"
```

### 13.3 执行烧录

```sh
# 进入构建目录
cd ~/work/antsdr-fw-patch/plutosdr-fw/build

# 步骤 1：烧录主固件
sudo dfu-util -a firmware.dfu -D ./ant.dfu

# 步骤 2：烧录引导程序
sudo dfu-util -a boot.dfu -D ./boot.dfu

# 步骤 3：烧录 U-Boot 环境变量
sudo dfu-util -a uboot-env.dfu -D ./uboot-env.dfu

# 步骤 4：备份当前额外环境变量（可选）
sudo dfu-util -a uboot-extra-env.dfu -U ./uboot-extra-env.dfu
```

> **顺序很重要**：建议按以上顺序逐步烧录，不要同时执行。

### 13.4 烧录完成后

烧录完成后断开 USB，重新上电。E310 将从新固件启动。

等待约 30–60 秒后，尝试通过网络连接：
```sh
# 默认 IP（QSPI 启动模式）
ssh root@192.168.1.10
# 默认密码：analog
```

---

## 14. 配置 2R2T 模式

AD9361 默认工作在 1R1T 模式（单收单发），配置 2R2T 可启用双通道收发。

### 14.1 QSPI 启动模式下配置

通过 SSH 登录 E310 后，执行：

```sh
fw_setenv attr_name compatible
fw_setenv attr_val ad9361
fw_setenv compatible ad9361
fw_setenv mode 2r2t
reboot
```

**验证配置是否生效：**
```sh
# 重启后检查
fw_printenv mode   # 应输出：mode=2r2t
iio_attr -u ip:localhost -d ad9361-phy compatible  # 应输出 ad9361
```

或者在 U-Boot 命令行下配置（在设备启动时按任意键中断）：
```sh
setenv attr_name compatible
setenv attr_val ad9361
setenv compatible ad9361
setenv mode 2r2t
saveenv
reset
```

### 14.2 SD 卡启动模式下配置

需要修改 SD 卡上的 `uEnv.txt` 文件。

**修改 1：更改 `adi_loadvals` 参数**

找到以下行：
```
adi_loadvals=fdt addr ${fit_load_address}...
```
改为：
```
adi_loadvals=fdt addr ${devicetree_load_address}...
```

**修改 2：将 mode 改为 2r2t**

找到：
```
mode=1r1t
```
改为：
```
mode=2r2t
```

**修改 3：在 `sdboot` 中添加 `run adi_loadvals`**

找到 `sdboot=` 这行，在 `bootm` 命令前添加 `&& run adi_loadvals;`，并在最后添加 `#{fit_config}`。

**修改 4：在文件末尾添加变量**

```
attr_name=compatible
attr_val=ad9361
compatible=ad9361
```

---

## 15. 验证固件功能

### 15.1 基本连通性测试

```sh
# 通过 USB 网络（RNDIS）连接
ssh root@192.168.1.10

# 查看系统信息
cat /etc/issue
uname -a
```

### 15.2 IIO 设备验证

```sh
# 列出 IIO 设备
iio_info -u ip:localhost

# 应看到 ad9361-phy 设备
# 在主机端也可以：
iio_info -u ip:<E310的IP>
```

### 15.3 RF 功能测试

```sh
# 在主机端用 GNU Radio 连接
# 或使用 iio_readdev 测试数据流
iio_readdev -u ip:192.168.1.10 -b 1024 cf-ad9361-lpc | head -c 1024 | xxd | head
```

### 15.4 2R2T 验证

```sh
# 在设备端检查模式
iio_attr -u ip:localhost -d ad9361-phy -g mode
# 输出应为：2r2t
```

---

## 附录 A：完整环境配置脚本

参见 [`../scripts/setup-env.sh`](../scripts/setup-env.sh) — 自动化安装所有依赖并配置环境变量。

## 附录 B：常见错误

参见 [`troubleshooting.md`](troubleshooting.md)。

## 附录 C：工具使用手册

参见 [`tool-manual.md`](tool-manual.md)。
