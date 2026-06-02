/*
 * 步骤 07：AXI-Lite PS→PL 寄存器读写
 *
 * 硬件：PL 中的 ADI AXI ADC/DAC 核心
 *   AXI ADC @ 0x79020000（读取版本、时钟状态）
 *   AXI DAC @ 0x79024000
 *
 * 学习目标：
 *   1. 理解 AXI-Lite 协议（地址+数据，单次传输）
 *   2. 掌握 Xil_In32/Xil_Out32 直接访问 PL 寄存器
 *   3. 读取 FPGA IP 核的版本和状态寄存器
 *   4. 初始化 ADI AXI ADC 核心（复位→使能→检查时钟）
 *
 * 注意：运行此步骤前，FPGA 必须已加载正确的位流。
 *       如果从 SD 卡或 QSPI 启动，FSBL 会自动加载位流。
 *       如果通过 JTAG 调试，需要先在 Vivado 中下载位流。
 */

#include "xparameters.h"
#include "xil_io.h"       /* Xil_In32, Xil_Out32 */
#include "xil_printf.h"
#include "xil_cache.h"
#include "sleep.h"

/* ── PL 外设基地址 ────────────────────────────────── */
#define AXI_ADC_BASE    0x79020000
#define AXI_DAC_BASE    0x79024000
#define AXI_DMAC_RX     0x7C400000
#define AXI_DMAC_TX     0x7C420000

/* ── ADI AXI ADC 寄存器偏移 ─────────────────────── */
#define ADC_REG_VERSION  0x0000  /* IP 核版本 */
#define ADC_REG_ID       0x0004
#define ADC_REG_SCRATCH  0x0008  /* 可读写的 scratch 寄存器（用于测试）*/
#define ADC_REG_RSTN     0x0040  /* bit[0]=复位, bit[1]=MMCM复位 */
#define ADC_REG_CNTRL    0x0044  /* 控制寄存器 */
#define ADC_REG_CLK_FREQ 0x0054  /* 采样时钟频率（单位: Hz/1000）*/
#define ADC_REG_CLK_RATIO 0x0058 /* 时钟比率 */
#define ADC_REG_STATUS   0x005C  /* bit[0]=时钟锁定 */

/* ── ADI AXI DMAC 寄存器偏移 ────────────────────── */
#define DMAC_REG_VERSION 0x0000
#define DMAC_REG_INTF    0x0018  /* 接口描述（读只）*/

/* 简便宏：以偏移访问 AXI 外设 */
#define AXI_READ(base, off)      Xil_In32((base) + (off))
#define AXI_WRITE(base, off, v)  Xil_Out32((base) + (off), (v))

/* 解码 ADI 版本号 */
static void print_adi_version(const char *name, u32 base)
{
    u32 ver = AXI_READ(base, ADC_REG_VERSION);
    xil_printf("  %-20s: v%lu.%lu.%c (0x%08X)\r\n",
               name,
               (ver >> 16) & 0xFF,
               (ver >> 8)  & 0xFF,
               (char)((ver & 0xFF) + 'a'),
               ver);
}

/* ── 初始化 AXI ADC 核心 ─────────────────────────── */
static int axi_adc_init(void)
{
    /* 1. 写 scratch 寄存器验证 AXI 总线可访问 */
    AXI_WRITE(AXI_ADC_BASE, ADC_REG_SCRATCH, 0x5A5A5A5A);
    u32 scratch = AXI_READ(AXI_ADC_BASE, ADC_REG_SCRATCH);
    if (scratch != 0x5A5A5A5A) {
        xil_printf("[AXI ADC] Scratch 寄存器测试失败！(读到 0x%08X)\r\n", scratch);
        xil_printf("          → 请检查 FPGA 位流是否已加载\r\n");
        return -1;
    }
    xil_printf("[AXI ADC] AXI 总线访问正常\r\n");

    /* 2. 复位 ADC 核心 */
    AXI_WRITE(AXI_ADC_BASE, ADC_REG_RSTN, 0x0);   /* 断言复位 */
    usleep(100);
    AXI_WRITE(AXI_ADC_BASE, ADC_REG_RSTN, 0x3);   /* 释放复位（bit0=ADC, bit1=MMCM）*/
    usleep(1000);

    /* 3. 检查时钟状态 */
    u32 status = AXI_READ(AXI_ADC_BASE, ADC_REG_STATUS);
    xil_printf("[AXI ADC] 状态寄存器 = 0x%08X\r\n", status);
    if (status & 0x1) {
        xil_printf("[AXI ADC] 采样时钟锁定 ✓\r\n");
    } else {
        xil_printf("[AXI ADC] 采样时钟未锁定 ✗（AD9361 可能未初始化）\r\n");
    }

    /* 4. 读取时钟频率（由 IP 核内部计数得出）*/
    u32 clk_freq  = AXI_READ(AXI_ADC_BASE, ADC_REG_CLK_FREQ);
    u32 clk_ratio = AXI_READ(AXI_ADC_BASE, ADC_REG_CLK_RATIO);
    /* ADI 的时钟频率寄存器格式：freq_hz = clk_freq * 100 / clk_ratio */
    if (clk_ratio > 0)
        xil_printf("[AXI ADC] 采样率 ≈ %lu Hz\r\n",
                   (u32)((u64)clk_freq * 100 / clk_ratio));

    return 0;
}

/* ── 演示：配置 ADC 通道 ────────────────────────── */
static void axi_adc_config_channels(void)
{
    /* ADI AXI ADC 的通道寄存器布局：
     * 每个通道占 0x40 字节，从偏移 0x0400 开始
     * CH0（I）: 0x0400
     * CH1（Q）: 0x0440
     */
    const u32 CH0_CTRL = 0x0400;
    const u32 CH1_CTRL = 0x0440;

    /* 使能 CH0 和 CH1，关闭测试模式 */
    AXI_WRITE(AXI_ADC_BASE, CH0_CTRL, 0x01);  /* bit[0]=enable */
    AXI_WRITE(AXI_ADC_BASE, CH1_CTRL, 0x01);
    xil_printf("[AXI ADC] 通道 0(I) 和 1(Q) 已使能\r\n");

    /* 读回确认 */
    xil_printf("  CH0 控制寄存器: 0x%08X\r\n",
               AXI_READ(AXI_ADC_BASE, CH0_CTRL));
    xil_printf("  CH1 控制寄存器: 0x%08X\r\n",
               AXI_READ(AXI_ADC_BASE, CH1_CTRL));
}

int main(void)
{
    Xil_ICacheEnable();
    Xil_DCacheEnable();

    xil_printf("\r\n================================================\r\n");
    xil_printf("  ANTSDR E310 裸机 - 步骤 07：AXI-Lite 访问\r\n");
    xil_printf("================================================\r\n\r\n");

    /* ── 读取版本号 ── */
    xil_printf("[PL IP 核版本]\r\n");
    print_adi_version("AXI ADC (0x79020000)", AXI_ADC_BASE);
    print_adi_version("AXI DAC (0x79024000)", AXI_DAC_BASE);
    print_adi_version("RX DMAC (0x7C400000)", AXI_DMAC_RX);
    print_adi_version("TX DMAC (0x7C420000)", AXI_DMAC_TX);

    /* ── 初始化 AXI ADC ── */
    xil_printf("\r\n[初始化 AXI ADC]\r\n");
    axi_adc_init();

    /* ── 配置通道 ── */
    xil_printf("\r\n[配置 ADC 通道]\r\n");
    axi_adc_config_channels();

    /* ── 演示：直接读写 DMAC 寄存器 ── */
    xil_printf("\r\n[DMAC 接口描述]\r\n");
    u32 dmac_intf = AXI_READ(AXI_DMAC_RX, 0x0018);
    xil_printf("  接口描述符 = 0x%08X\r\n", dmac_intf);
    xil_printf("  DMA 方向  : %s\r\n",
               (dmac_intf >> 1) & 1 ? "Memory to Stream (M2S)" : "Stream to Memory (S2MM)");
    xil_printf("  数据宽度  : %lu 位\r\n", (((dmac_intf >> 8) & 0xFF) + 1) * 8);

    xil_printf("\r\n[完成] 步骤 07 执行完毕。\r\n");
    xil_printf("下一步：08-axi-dma（DMA 数据传输）\r\n\r\n");

    while (1) {}
    return 0;
}
