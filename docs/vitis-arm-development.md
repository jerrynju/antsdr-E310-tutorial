# 在 Vitis 中为 ANTSDR E310 ARM 处理器开发算法

> Zynq-7020 内嵌双核 ARM Cortex-A9（PS 端）。本文讲解三条开发路径：
> Vitis 裸机开发、Linux 用户空间交叉编译、Vitis HLS FPGA 加速。

---

## 目录

1. [硬件架构理解](#1-硬件架构理解)
2. [三条开发路径对比](#2-三条开发路径对比)
3. [路径一：Vitis 裸机开发（Bare-metal）](#3-路径一vitis-裸机开发bare-metal)
4. [路径二：Linux 用户空间应用（推荐）](#4-路径二linux-用户空间应用推荐)
5. [路径三：Vitis HLS FPGA 加速](#5-路径三vitis-hls-fpga-加速)
6. [PS 与 PL 之间的数据交互](#6-ps-与-pl-之间的数据交互)

---

## 1. 硬件架构理解

### Zynq-7020 内部结构

```
┌──────────────────────────────────────────────────────────┐
│                    Zynq-7020 SoC                         │
│                                                          │
│  ┌──────────────────────┐  ┌───────────────────────────┐ │
│  │   PS (Processing      │  │   PL (Programmable Logic) │ │
│  │      System)          │  │        = FPGA             │ │
│  │                       │  │                           │ │
│  │  ARM Cortex-A9 × 2   │  │  ADI AD9361 接口 IP       │ │
│  │  (800 MHz)            │  │  DMA 控制器               │ │
│  │                       │  │  AXI 总线互联             │ │
│  │  512 MB DDR3          │◄►│  自定义数字信号处理 IP    │ │
│  │  256 MB QSPI Flash    │  │                           │ │
│  │                       │  │  ← 可在这里加 HLS IP ←   │ │
│  │  USB OTG              │  │                           │ │
│  │  GigE                 │  │                           │ │
│  └──────────────────────┘  └───────────────────────────┘ │
│                                                          │
│              AXI Interconnect（高速总线）                │
└──────────────────────────────────────────────────────────┘
                         │
                    AD9361 RF 芯片
                 (70 MHz – 6 GHz, 2T2R)
```

### 两个 ARM 核心的分工

| 核心 | 角色 | 运行内容 |
|------|------|----------|
| CPU0 | 主核 | Linux 内核 + 用户应用 |
| CPU1 | 可配置 | Linux SMP 第二核，或裸机 AMP 模式 |

**实际开发时你只需关注：**
- **写算法** → 在 CPU0 的 Linux 用户空间运行
- **极低延迟** → 裸机直接控制硬件寄存器
- **数据预处理** → 放到 FPGA 的 HLS IP 里

---

## 2. 三条开发路径对比

| 对比项 | 裸机 (Bare-metal) | Linux 用户空间 | Vitis HLS |
|--------|-------------------|----------------|-----------|
| 运行位置 | ARM，无 OS | ARM，Linux 上 | FPGA 逻辑 |
| 开发工具 | Vitis IDE | 任意编辑器 + 交叉编译器 | Vitis HLS IDE |
| 编程语言 | C/C++ | C/C++ / Python | C/C++ |
| 实时性 | 极高（无 OS 抖动） | 一般（受 Linux 调度影响） | 最高（硬件并行） |
| 开发难度 | 中（需理解 BSP） | **低（推荐新手）** | 高（需理解 HLS 综合） |
| 访问 AD9361 | 直接操作 SPI 寄存器 | 通过 libiio API | 不能直接访问（通过 AXI） |
| **适用场景** | FSBL 定制、底层调试 | **SDR 算法开发（主流）** | FFT、滤波器等高速处理 |

**新手推荐路径：路径二（Linux 用户空间 + libiio）**

---

## 3. 路径一：Vitis 裸机开发（Bare-metal）

裸机应用直接运行在 ARM 上，无操作系统，通过 Xilinx BSP 访问硬件。

### 3.1 前提：从 Vivado 导出硬件描述文件

固件编译完成后，`build/` 目录包含 `system_top.hdf`（旧版）或 `system_top.xsa`（Vivado 2019.2+）。

```sh
# 在 Vivado TCL Console 中导出 XSA
write_hw_platform -fixed -include_bit -force \
    /path/to/antsdr-fw-patch/plutosdr-fw/build/system_top.xsa
```

### 3.2 Vitis 创建平台工程

1. 启动 Vitis 2023.2：
   ```sh
   source /opt/Xilinx/Vitis/2023.2/settings64.sh
   vitis &
   ```

2. **File → New → Platform Project**
   - Platform name: `antsdr_e310_platform`
   - Hardware: 选择 `system_top.xsa`
   - OS: `standalone`（裸机）或 `freertos10_xilinx`（FreeRTOS）
   - CPU: `ps7_cortexa9_0`

3. **File → New → Application Project**
   - 选择刚创建的 Platform
   - 模板选 `Empty Application (C)`
   - 将示例代码（见下方）复制到 `src/` 目录

### 3.3 裸机开发常用 BSP API

```c
/* 头文件位于 BSP 的 include/ 目录 */
#include "xparameters.h"   // 硬件地址和参数（从 XSA 自动生成）
#include "xil_printf.h"    // 串口打印（替代 printf）
#include "xgpio.h"         // GPIO 控制
#include "xspi.h"          // SPI 总线
#include "xil_cache.h"     // Cache 控制
#include "sleep.h"         // usleep / sleep

/* 打印到串口 */
xil_printf("Hello from ARM!\r\n");

/* 读写内存映射寄存器 */
Xil_Out32(XPAR_AXI_GPIO_0_BASEADDR, 0x01);  // 写
u32 val = Xil_In32(XPAR_AXI_GPIO_0_BASEADDR);  // 读
```

### 3.4 裸机 Hello World 示例

参见 [`../examples/01-baremetal-hello/main.c`](../examples/01-baremetal-hello/main.c)

### 3.5 构建并通过 JTAG 运行

1. 连接 JTAG（Digilent JTAG-HS3 或 Platform Cable USB）
2. 在 Vitis 中：**Run → Run Configurations → Single Application Debug (GDB)**
3. 或生成 `boot.bin` 打包：**Xilinx → Create Boot Image**

```
boot.bif 内容：
img : {
    [bootloader] fsbl.elf
    system_top.bit
    your_app.elf
}
```

---

## 4. 路径二：Linux 用户空间应用（推荐）

这是 SDR 开发最常用的方式。固件里的 Linux 已经加载了 AD9361 的 IIO 驱动，通过 **libiio** C 库即可控制所有 RF 参数并读写 IQ 数据。

### 4.1 libiio 架构

```
你的 C 程序
    │  libiio API
    ▼
iio_context (网络/USB/本地)
    │
    ├── ad9361-phy      ← RF 参数控制（频率、增益、带宽）
    ├── cf-ad9361-lpc   ← RX IQ 数据流（ADC 输入）
    └── cf-ad9361-dds-core-lpc  ← TX IQ 数据流（DAC 输出）
```

### 4.2 安装 libiio（主机端，用于交叉编译）

```sh
# 在 Ubuntu 主机安装开发库
sudo apt-get install libiio-dev

# 或从源码编译（获取最新版）
git clone https://github.com/analogdevicesinc/libiio.git
cd libiio && mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/arm.cmake \
      -DCMAKE_INSTALL_PREFIX=/usr/arm-linux-gnueabihf ..
make -j4 && sudo make install
```

### 4.3 交叉编译工作流

```sh
# 设置编译器
export CC=arm-linux-gnueabihf-gcc
export CROSS_COMPILE=arm-linux-gnueabihf-

# 编译单个文件
${CC} -o my_app main.c -liio

# 传到设备
scp my_app root@192.168.1.10:/tmp/
ssh root@192.168.1.10 "/tmp/my_app"
```

### 4.4 libiio 核心 API 速查

```c
/* 1. 建立连接 */
struct iio_context *ctx = iio_create_network_context("192.168.1.10");
// 或本地：iio_create_local_context()
// 或 USB： iio_create_usb_context(NULL)

/* 2. 获取设备 */
struct iio_device *phy = iio_context_find_device(ctx, "ad9361-phy");
struct iio_device *rx  = iio_context_find_device(ctx, "cf-ad9361-lpc");

/* 3. 读写 RF 属性（通道属性） */
struct iio_channel *ch = iio_device_find_channel(phy, "voltage0", false); // RX
iio_channel_attr_write_longlong(ch, "sampling_frequency", 5000000);  // 5 MHz
iio_channel_attr_write_longlong(ch, "rf_bandwidth",       4000000);  // 4 MHz

/* LO 频率（局部振荡器）*/
struct iio_channel *lo = iio_device_find_channel(phy, "altvoltage0", true); // RX LO
iio_channel_attr_write_longlong(lo, "frequency", 433000000LL); // 433 MHz

/* 4. 创建 IQ 数据缓冲区 */
iio_channel_enable(iio_device_find_channel(rx, "voltage0", false)); // I
iio_channel_enable(iio_device_find_channel(rx, "voltage1", false)); // Q
struct iio_buffer *buf = iio_device_create_buffer(rx, 1024, false);

/* 5. 读取 IQ 数据 */
ssize_t nbytes = iio_buffer_refill(buf);
int16_t *ptr = iio_buffer_start(buf);
for (int i = 0; i < 1024; i++) {
    int16_t i_sample = ptr[2 * i];      // I 分量
    int16_t q_sample = ptr[2 * i + 1];  // Q 分量
}

/* 6. 释放资源 */
iio_buffer_destroy(buf);
iio_context_destroy(ctx);
```

### 4.5 示例程序

| 示例 | 路径 | 说明 |
|------|------|------|
| 基础参数配置 | [`examples/02-linux-iio-basic/`](../examples/02-linux-iio-basic/) | 设置频率/采样率/增益并打印参数 |
| IQ 数据接收存文件 | [`examples/03-linux-iio-rx-file/`](../examples/03-linux-iio-rx-file/) | 接收 N 个采样并保存为二进制文件 |
| 频谱扫描 | [`examples/04-linux-iio-spectrum-scan/`](../examples/04-linux-iio-spectrum-scan/) | 扫描指定频段，打印信号强度 |

---

## 5. 路径三：Vitis HLS FPGA 加速

当某个算法对实时性要求极高（如实时解调、相关运算、FFT），可以用 Vitis HLS 将 C/C++ 函数综合为 FPGA IP 核，在 PL 端并行运行，速度可比 ARM 快 10–100 倍。

### 5.1 典型应用场景

- 实时 FFT（1024 点，重复率 > 1 MHz）
- FIR/CIC 数字滤波器
- CORDIC 频率校正
- 相关峰检测（用于 GPS/北斗捕获）
- OFDM 符号同步

### 5.2 HLS 开发流程

```
C/C++ 算法代码
       │
       │ Vitis HLS 综合
       ▼
   FPGA IP 核 (.xo 文件)
       │
       │ 在 Vivado 中集成到 Block Design
       ▼
   新的 system_top.bit
       │
       │ 替换 E310 固件中的位流
       ▼
   ARM 通过 AXI 寄存器控制 IP
```

### 5.3 HLS C++ 代码示例（FIR 滤波器）

```cpp
// fir_filter.cpp
#include "ap_int.h"
#include "hls_stream.h"

// 告诉 HLS 工具这是顶层函数
void fir_filter(
    hls::stream<ap_int<32>> &data_in,
    hls::stream<ap_int<32>> &data_out,
    ap_int<16> coeffs[64]
) {
#pragma HLS INTERFACE axis port=data_in
#pragma HLS INTERFACE axis port=data_out
#pragma HLS INTERFACE s_axilite port=coeffs
#pragma HLS INTERFACE s_axilite port=return

    static ap_int<32> shift_reg[64] = {0};

    ap_int<32> x = data_in.read();
#pragma HLS PIPELINE II=1

    ap_int<64> acc = 0;
    for (int i = 63; i > 0; i--) {
        shift_reg[i] = shift_reg[i-1];
        acc += shift_reg[i] * coeffs[i];
    }
    shift_reg[0] = x;
    acc += x * coeffs[0];

    data_out.write(acc >> 15);
}
```

### 5.4 从 HLS 到 E310 的步骤摘要

1. **Vitis HLS** 中综合并导出 IP（生成 `.xo` 文件）
2. **Vivado** 中打开 `plutosdr-fw/hdl/projects/ant/` 工程
3. 在 Block Design 里添加新 IP，连接 AXI Stream 到 AD9361 DMA 路径
4. 重新综合实现，导出新的 XSA + 位流
5. 重新编译固件（`sudo -E make`）或只替换 `system_top.bit`

---

## 6. PS 与 PL 之间的数据交互

### 6.1 常用接口

| 接口 | 带宽 | 用途 |
|------|------|------|
| AXI-Lite | 低 | 寄存器读写（控制、状态） |
| AXI-Full | 高 | DMA 大块数据传输 |
| AXI-Stream | 最高 | 流式 IQ 数据（无地址） |
| BRAM | 中 | 小量共享内存（LUT 表等） |

### 6.2 在 Linux 中访问 PL 的 AXI-Lite 寄存器

```c
#include <sys/mman.h>
#include <fcntl.h>

// 打开 /dev/mem 访问物理地址（需要 root）
int fd = open("/dev/mem", O_RDWR | O_SYNC);

// 映射 AXI-Lite 基地址（从 xparameters.h 查地址）
#define MY_IP_BASEADDR  0x43C00000
#define MAP_SIZE        4096

void *mapped = mmap(NULL, MAP_SIZE,
                    PROT_READ | PROT_WRITE,
                    MAP_SHARED, fd, MY_IP_BASEADDR);

// 写寄存器
*((volatile uint32_t *)(mapped + 0x00)) = 0x01;  // 启动

// 读状态
uint32_t status = *((volatile uint32_t *)(mapped + 0x04));

munmap(mapped, MAP_SIZE);
close(fd);
```

### 6.3 IQ 数据流路径

```
AD9361 ADC
    │  LVDS 接口（300 MHz）
    ▼
PL: ADI JESD/CMOS 接口 IP
    │  AXI-Stream（IQ 数据，12-bit → 16-bit）
    ▼
PL: [可选：HLS 数字信号处理 IP]
    │  AXI-Stream
    ▼
PL: AXI DMA（scatter-gather）
    │  AXI-Full（写 DDR）
    ▼
PS: DDR3（物理地址）
    │  /dev/iio:device0 或 DMA 直接访问
    ▼
ARM 用户空间程序（你的算法）
```
