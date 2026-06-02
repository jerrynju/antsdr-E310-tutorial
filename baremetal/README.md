# ANTSDR E310 裸机开发渐进式学习路径

> 按硬件模块划分，从最简单的串口输出到完整 SDR 系统，逐步构建。
> 每个步骤都能独立编译运行，是下一步骤的基础。

---

## 硬件架构总览

```
┌───────────────────────────────────────────────────────────────┐
│                      Zynq-7020 SoC                            │
│                                                               │
│  ┌──────────────────────────┐  ┌──────────────────────────┐  │
│  │    PS (ARM 处理器系统)   │  │    PL (可编程逻辑/FPGA)  │  │
│  │                          │  │                          │  │
│  │  CPU0/CPU1 (Cortex-A9)  │  │  AXI ADC Core (RX IQ)   │  │
│  │  UART1  0xE0001000       │  │  0x79020000              │  │
│  │  GPIO   0xE000A000       │  │                          │  │
│  │  SPI1   0xE0006000 ──────┼──┼──→ AD9361 SPI            │  │
│  │  TTC0   0xF8001000       │  │  AXI DAC Core (TX IQ)   │  │
│  │  GIC    0xF8F01000       │  │  0x79024000              │  │
│  │                          │  │                          │  │
│  │  DDR3   0x00000000       │  │  RX DMA  0x7C400000      │  │
│  │         512 MB           │◄─┼──  TX DMA  0x7C420000    │  │
│  └──────────────────────────┘  └──────────────────────────┘  │
└───────────────────────────────────────────────────────────────┘
            │                              │
         PS MIO                        EMIO/PL
    GPIO[0:53]                     GPIO[54:117]
         │
    ┌────┴────────────────────────────────┐
    │ GPIO[14] → 用户按钮（中断）          │
    │ GPIO[15] → LED（绿灯）              │
    │ GPIO[46] → AD9361 RESETB           │
    │ GPIO[66] → AD9361 EN_AGC           │
    │ GPIO[86-93] → RF 频段选择开关       │
    └──────────────────────────────────────┘
```

---

## 学习路径总览

| 步骤 | 目录 | 硬件 | 新学内容 |
|------|------|------|---------|
| 00 | `00-hardware-map/` | — | E310 完整硬件地址表 |
| 01 | `01-uart-hello/` | UART1 | Vitis项目结构、xil_printf、串口调试 |
| 02 | `02-gpio-led/` | PS GPIO[15] | XGpioPs驱动、输出控制 |
| 03 | `03-gpio-interrupt/` | PS GPIO[14] | XScuGic中断控制器、ISR编写 |
| 04 | `04-timer/` | TTC0 | XTtcPs定时器、精确延时、周期中断 |
| 05 | `05-ddr-memtest/` | DDR3 | Cache管理、内存屏障、DMA准备 |
| 06 | `06-spi-master/` | PS SPI1 | XSpiPs驱动、SPI时序、片选控制 |
| 07 | `07-axi-lite/` | PL AXI-Lite | PS→PL寄存器读写、地址映射 |
| 08 | `08-axi-dma/` | ADI AXI DMAC | DMA描述符、流式传输、双缓冲 |
| 09 | `09-ad9361-init/` | AD9361 SPI | AD9361 SPI协议、芯片ID验证 |
| 10 | `10-ad9361-config/` | AD9361 完整 | PLL配置、LO频率、增益、带宽 |
| 11 | `11-ad9361-iq-rx/` | AD9361+DMA | IQ数据流接收、缓冲区管理 |
| 12 | `12-full-sdr/` | 全部硬件 | 完整SDR系统：初始化→接收→处理→输出 |

---

## 前置条件

1. **Vivado 2023.2** 已安装，从 `plutosdr-fw` 编译已生成 `system_top.xsa`
2. **Vitis 2023.2** 已安装（与 Vivado 同版本）
3. **JTAG 调试器**：Digilent JTAG-HS3 或 Xilinx Platform Cable USB
4. **串口线**：E310 UART 连接到 PC（波特率 115200）

### 创建 Vitis Platform（所有步骤共用）

```sh
source /opt/Xilinx/Vitis/2023.2/settings64.sh
vitis &
```

1. **File → New → Platform Project**
   - Name: `e310_platform`
   - Hardware: 选择 `plutosdr-fw/build/system_top.xsa`
   - OS: `standalone`
   - CPU: `ps7_cortexa9_0`
   - Language: C
2. **Build Platform**（右键 → Build）
3. 每个步骤新建 **Application Project**，选择 `e310_platform`

---

## 构建与调试流程

```
新建 Application Project
       │
       ↓
复制对应步骤的 .c/.h 到 src/
       │
       ↓
右键 Project → Build
       │
       ↓
Run → Debug Configurations
       │
       ↓
选 "Single Application Debug (GDB)"
       │
       ↓
连接 JTAG → 运行 → 串口查看输出
```

---

## 依赖关系图

```
01-uart-hello
      │
      ↓
02-gpio-led
      │
      ↓
03-gpio-interrupt ─── 04-timer
      │                    │
      └─────────┬──────────┘
                ↓
         05-ddr-memtest
                │
                ↓
          06-spi-master
                │
                ↓
          07-axi-lite ── 08-axi-dma
                │              │
                └──────┬───────┘
                       ↓
                09-ad9361-init
                       │
                       ↓
               10-ad9361-config
                       │
                       ↓
               11-ad9361-iq-rx
                       │
                       ↓
                12-full-sdr
```
