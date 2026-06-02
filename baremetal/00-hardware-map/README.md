# E310 裸机硬件地址参考手册

> 裸机开发必读。所有寄存器地址、GPIO 编号、中断号均来自 E310 硬件设计。

---

## PS 外设地址映射

```c
/* ===== 串口 ===== */
#define UART1_BASEADDR          0xE0001000  /* 调试串口，115200-8-N-1 */
#define UART0_BASEADDR          0xE0000000  /* 备用 */

/* ===== PS GPIO ===== */
#define PS_GPIO_BASEADDR        0xE000A000  /* 全部 118 个 GPIO 引脚 */

/* ===== SPI ===== */
#define SPI0_BASEADDR           0xE0006000  /* 注意：SPI1 在 Linux DTS 中叫 spi@e0006000 */
#define SPI1_BASEADDR           0xE0007000

/*
 * 关于 SPI 编号：Linux DTS 中 spi@e0006000 对应 Zynq 的 SPI0 控制器。
 * Xilinx BSP 里 XPAR_PS7_SPI_0_DEVICE_ID=0, BASEADDR=0xE0006000。
 * AD9361 连接在此 SPI（DEVICE_ID=0）的 CS0 上。
 */

/* ===== 定时器 ===== */
#define TTC0_BASEADDR           0xF8001000  /* Triple Timer Counter 0 */
#define TTC1_BASEADDR           0xF8002000

/* ===== 中断控制器 ===== */
#define SCUGIC_DIST_BASEADDR    0xF8F01000  /* GIC 分发器 */
#define SCUGIC_CPU_BASEADDR     0xF8F00100  /* GIC CPU 接口 */

/* ===== 看门狗 ===== */
#define WDT_BASEADDR            0xF8005000

/* ===== DMA (PS 内部) ===== */
#define DMAC0_BASEADDR          0xF8003000

/* ===== USB ===== */
#define USB0_BASEADDR           0xE0002000

/* ===== 以太网 ===== */
#define GEM0_BASEADDR           0xE000B000  /* Gigabit ETH MAC */
```

---

## DDR3 内存布局

```
地址范围                    大小      用途
0x00000000 - 0x0007FFFF   512 KB   OCM (On-Chip Memory，FSBL使用)
0x00100000 - 0x003FFFFF   3 MB     程序代码 + 数据（BSS/Stack/Heap）
0x00400000 - 0x00FFFFFF   12 MB    用户可用
0x01000000 - 0x1FFFFFFF   ~495 MB  DMA 缓冲区、大数据处理

常用 DMA 缓冲区地址（代码中定义）：
#define DMA_RX_BUF_ADDR    0x01000000   /* RX IQ 数据缓冲 */
#define DMA_TX_BUF_ADDR    0x01800000   /* TX IQ 数据缓冲 */
```

---

## PS GPIO 引脚表

```c
/* ─── Bank 0：MIO[0:31] ─── */
#define GPIO_LED            15   /* 板载绿色 LED（高电平亮）*/
#define GPIO_BUTTON         14   /* 用户按钮（低电平按下，下降沿中断）*/

/* ─── Bank 1：MIO[32:53] ─── */
#define GPIO_AD9361_RESET   46   /* AD9361 RESETB（低电平复位）*/
#define GPIO_ETH_PHY_RESET  47   /* 以太网 PHY 复位 */

/* ─── Bank 2：EMIO[0:31]，GPIO偏移 54+0 ~ 54+31 ─── */
#define GPIO_AD9361_EN_AGC  66   /* AD9361 Enable AGC（EMIO[12]）*/
#define GPIO_AD9361_SYNC    67   /* AD9361 SYNC_IN（EMIO[13]）*/

/* ─── Bank 3：EMIO[32:63]，GPIO偏移 86+0 ~ 86+7 ─── */
#define GPIO_BAND_RX1_HI    86   /* RX1 3~6 GHz 选通 */
#define GPIO_BAND_RX1_LO    87   /* RX1 4.5M~3 GHz 选通 */
#define GPIO_BAND_TX1_HI    88   /* TX1 3~6 GHz 选通 */
#define GPIO_BAND_TX1_LO    89   /* TX1 4.5M~3 GHz 选通 */
#define GPIO_BAND_RX2_HI    90   /* RX2 3~6 GHz 选通 */
#define GPIO_BAND_RX2_LO    91   /* RX2 4.5M~3 GHz 选通 */
#define GPIO_BAND_TX2_HI    92   /* TX2 3~6 GHz 选通 */
#define GPIO_BAND_TX2_LO    93   /* TX2 4.5M~3 GHz 选通 */
```

---

## 中断号（XScuGic IRQ ID）

```c
/* Zynq GIC 中断编号 = SPI编号 + 32 */

/* PS 外设中断 */
#define IRQ_PS_GPIO         52   /* PS GPIO 控制器 */
#define IRQ_PS_TTC0_0       42   /* TTC0 Timer 0 */
#define IRQ_PS_TTC0_1       43   /* TTC0 Timer 1 */
#define IRQ_PS_TTC0_2       44   /* TTC0 Timer 2 */
#define IRQ_PS_UART1        82   /* UART1 */
#define IRQ_PS_SPI0         81   /* SPI0（AD9361 SPI）*/
#define IRQ_PS_DMA0_ABORT   45   /* PS DMA0 abort */

/* PL 中断（来自 DTS：interrupts = <0 56 ...> → 56+32=88）*/
#define IRQ_PL_RX_DMA       88   /* ADI AXI DMAC RX（SPI 56）*/
#define IRQ_PL_TX_DMA       89   /* ADI AXI DMAC TX（SPI 57）*/
```

---

## PL（FPGA）外设地址

```c
/* ─── ADI ADC/DAC 核心 ─── */
#define AXI_ADC_BASEADDR    0x79020000  /* cf-ad9361-lpc（RX IQ）*/
#define AXI_DAC_BASEADDR    0x79024000  /* cf-ad9361-dds-core-lpc（TX IQ）*/

/* ─── ADI AXI DMAC ─── */
#define AXI_DMAC_RX_BASE    0x7C400000  /* S2MM：AD9361 ADC → DDR */
#define AXI_DMAC_TX_BASE    0x7C420000  /* M2S：DDR → AD9361 DAC */

/* ─── AXI ADC 核心关键寄存器偏移 ─── */
#define ADC_REG_RSTN        0x0040  /* bit[0]=adc_rst, bit[1]=mmcm_rst */
#define ADC_REG_CNTRL       0x0044  /* bit[3]=r1_mode（1R1T/2R2T切换）*/
#define ADC_REG_CLK_FREQ    0x0054
#define ADC_REG_CLK_RATIO   0x0058
#define ADC_REG_STATUS      0x005C  /* bit[0]=pn_err */
#define ADC_CH_REG(ch, r)   (0x0400 + (ch)*0x40 + (r))

/* ─── ADI AXI DMAC 关键寄存器偏移 ─── */
#define DMAC_REG_IRQ_MASK   0x0080  /* 中断屏蔽（0=使能）*/
#define DMAC_REG_IRQ_PEND   0x0084  /* 中断挂起（写1清除）*/
#define DMAC_REG_IRQ_SRC    0x0088
#define DMAC_REG_CTRL       0x0400  /* bit[0]=enable, bit[1]=pause */
#define DMAC_REG_XFER_ID    0x0404  /* 当前传输 ID（只读）*/
#define DMAC_REG_XFER_SUB   0x0408  /* 写1提交传输 */
#define DMAC_REG_FLAGS      0x040C  /* bit[0]=cyclic */
#define DMAC_REG_DEST_ADDR  0x0410  /* 目标地址（S2MM：写入DDR的地址）*/
#define DMAC_REG_SRC_ADDR   0x0414  /* 源地址（M2S：从DDR读取）*/
#define DMAC_REG_X_LEN      0x0418  /* 传输字节数-1 */
#define DMAC_REG_Y_LEN      0x041C  /* 2D传输（单次=0）*/
#define DMAC_REG_XFER_DONE  0x0428  /* 完成标志位图（只读）*/
```

---

## AD9361 关键寄存器

```c
/* SPI 帧格式（3字节）：
 *  Byte 0: [R/W, 0, 0, A12:A8]   R=1读，W=0写
 *  Byte 1: A7:A0
 *  Byte 2: Data
 */

#define AD9361_REG_SPI_CONF         0x000  /* SPI配置，bit[7]=软复位 */
#define AD9361_REG_PRODUCT_ID       0x037  /* 产品ID，bits[7:3]=0x01，AD9361=0x0A */
#define AD9361_REG_CLK_CTRL_0       0x009  /* 时钟控制 */
#define AD9361_REG_BBPLL_CTRL       0x045  /* BB PLL控制 */

/* RX PLL */
#define AD9361_REG_RX_PLL_ENABLE    0x250
#define AD9361_REG_RX_VCO_OUTPUT    0x251
#define AD9361_REG_RX_CP_CURR       0x252
#define AD9361_REG_RX_CP_IQ         0x253
#define AD9361_REG_RX_CP_CAL        0x254
#define AD9361_REG_RX_LOOP_FILT_1   0x256
#define AD9361_REG_RX_LOOP_FILT_2   0x257
#define AD9361_REG_RX_LOOP_FILT_3   0x258
#define AD9361_REG_RX_VCO_BIAS_1    0x259
#define AD9361_REG_RX_VCO_VARACTOR  0x25C
#define AD9361_REG_RX_VCO_BIAS_2    0x268
#define AD9361_REG_RX_SYNTH_FRACT_1 0x27A  /* 分数分频[22:16] */
#define AD9361_REG_RX_SYNTH_FRACT_2 0x27B  /* 分数分频[15:8] */
#define AD9361_REG_RX_SYNTH_FRACT_3 0x27C  /* 分数分频[7:0] */
#define AD9361_REG_RX_SYNTH_INT_1   0x27D  /* 整数分频[10:8] */
#define AD9361_REG_RX_SYNTH_INT_2   0x27E  /* 整数分频[7:0] */
#define AD9361_REG_RX_DIVIDER       0x27F  /* VCO输出分频器 */

/* TX PLL (偏移 +0x40) */
#define AD9361_REG_TX_PLL_ENABLE    0x290
#define AD9361_REG_TX_SYNTH_FRACT_1 0x2BA
#define AD9361_REG_TX_SYNTH_FRACT_2 0x2BB
#define AD9361_REG_TX_SYNTH_FRACT_3 0x2BC
#define AD9361_REG_TX_SYNTH_INT_1   0x2BD
#define AD9361_REG_TX_SYNTH_INT_2   0x2BE
#define AD9361_REG_TX_DIVIDER       0x2BF

/* RX 增益控制 */
#define AD9361_REG_RX1_MANUAL_GAIN  0x109  /* RX1 手动增益 [0:6]=增益(dB) */
#define AD9361_REG_RX2_MANUAL_GAIN  0x10C
#define AD9361_REG_GAIN_MODE        0x0FA  /* bit[0:1]=0手动，1慢AGC，2快AGC */

/* 采样率 / 滤波器 */
#define AD9361_REG_RX_ENABLE_FILT   0x003  /* BBBPLL, RX/TX采样率控制 */
#define AD9361_REG_RX_FILT_BW       0x0A2  /* RX FIR带宽 */
```

---

## Vitis BSP 宏（xparameters.h 自动生成）

```c
/* 以下是典型值，实际值由 Vivado 硬件设计决定 */
#define XPAR_PS7_UART_1_DEVICE_ID       1
#define XPAR_PS7_UART_1_BASEADDR        0xE0001000

#define XPAR_PS7_GPIO_0_DEVICE_ID       0
#define XPAR_PS7_GPIO_0_BASEADDR        0xE000A000

#define XPAR_PS7_SPI_0_DEVICE_ID        0    /* AD9361 SPI */
#define XPAR_PS7_SPI_0_BASEADDR         0xE0006000

#define XPAR_PS7_TTC_0_0_DEVICE_ID      0
#define XPAR_PS7_TTC_0_0_BASEADDR       0xF8001000

#define XPAR_PS7_SCUGIC_0_DEVICE_ID     0
#define XPAR_PS7_SCUGIC_0_DIST_BASEADDR 0xF8F01000

/* PL DMA（如果硬件设计中包含）*/
#define XPAR_AXI_DMAC_RX_BASEADDR       0x7C400000
#define XPAR_AXI_DMAC_TX_BASEADDR       0x7C420000
```
