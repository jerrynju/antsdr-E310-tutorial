/*
 * 步骤 08：ADI AXI DMAC — 流式数据传输
 *
 * 硬件：
 *   RX DMAC @ 0x7C400000（S2MM：从 AD9361 ADC 数据流 → DDR）
 *   TX DMAC @ 0x7C420000（M2S：DDR → AD9361 DAC 数据流）
 *   DDR3 缓冲区 @ 0x01000000（1 MB）
 *
 * 学习目标：
 *   1. 理解 ADI AXI DMAC 寄存器操作流程
 *   2. 掌握 S2MM（接收）DMA 传输的完整序列
 *   3. 理解为什么 DMA 前后必须 Flush/Invalidate Cache
 *   4. 轮询方式检测传输完成（简单可靠）
 *   5. 理解双缓冲（Ping-Pong）提高吞吐量
 *
 * DMA 传输序列（S2MM）：
 *   1. 使能 DMAC（CTRL bit0=1）
 *   2. 设置目标地址（DEST_ADDRESS）
 *   3. 设置传输长度（X_LENGTH = 字节数-1）
 *   4. Flush D-Cache（清除目标内存的旧缓存）
 *   5. 提交传输（TRANSFER_SUBMIT 写1）
 *   6. 等待完成（轮询 TRANSFER_DONE）
 *   7. Invalidate D-Cache（让 CPU 读到新数据）
 *   8. 处理数据
 */

#include "xparameters.h"
#include "xil_io.h"
#include "xil_cache.h"
#include "xil_printf.h"
#include "sleep.h"

/* ── 硬件地址 ─────────────────────────────────────── */
#define AXI_DMAC_RX_BASE   0x7C400000
#define AXI_DMAC_TX_BASE   0x7C420000
#define AXI_ADC_BASE       0x79020000

/* DMA 缓冲区（位于 DDR）*/
#define DMA_RX_BUF_ADDR    0x01000000
#define DMA_TX_BUF_ADDR    0x01800000
#define DMA_BUF_BYTES      (64 * 1024)  /* 每次传输 64 KB */
/* 每个 IQ 采样 = 4 字节（I:int16 + Q:int16）*/
#define IQ_SAMPLE_BYTES    4
#define NUM_SAMPLES        (DMA_BUF_BYTES / IQ_SAMPLE_BYTES)  /* 16384 采样 */

/* ── ADI AXI DMAC 寄存器偏移 ────────────────────── */
#define DMAC_REG_IRQ_MASK  0x0080
#define DMAC_REG_IRQ_PEND  0x0084
#define DMAC_REG_IRQ_SRC   0x0088
#define DMAC_REG_CTRL      0x0400
#define DMAC_REG_XFER_ID   0x0404
#define DMAC_REG_XFER_SUB  0x0408
#define DMAC_REG_FLAGS     0x040C
#define DMAC_REG_DEST_ADDR 0x0410
#define DMAC_REG_SRC_ADDR  0x0414
#define DMAC_REG_X_LEN     0x0418
#define DMAC_REG_Y_LEN     0x041C
#define DMAC_REG_XFER_DONE 0x0428

/* ── ADI AXI ADC 寄存器 ─────────────────────────── */
#define ADC_REG_RSTN       0x0040
#define ADC_REG_CNTRL      0x0044
#define ADC_REG_CH0_CTRL   0x0400
#define ADC_REG_CH1_CTRL   0x0440

#define AXI_R(base, off)   Xil_In32((base) + (off))
#define AXI_W(base, off, v) Xil_Out32((base) + (off), (v))

/* ── 初始化 AXI ADC 和通道 ────────────────────────── */
static void adc_setup(void)
{
    AXI_W(AXI_ADC_BASE, ADC_REG_RSTN, 0x0);   /* 复位 */
    usleep(1000);
    AXI_W(AXI_ADC_BASE, ADC_REG_RSTN, 0x3);   /* 释放复位 */
    usleep(1000);
    /* 使能 CH0(I) 和 CH1(Q) */
    AXI_W(AXI_ADC_BASE, ADC_REG_CH0_CTRL, 0x1);
    AXI_W(AXI_ADC_BASE, ADC_REG_CH1_CTRL, 0x1);
    xil_printf("[ADC] 核心复位并使能通道 I/Q\r\n");
}

/* ── 初始化 RX DMAC ───────────────────────────────── */
static void rx_dma_init(void)
{
    /* 屏蔽所有中断（轮询模式不需要中断）*/
    AXI_W(AXI_DMAC_RX_BASE, DMAC_REG_IRQ_MASK, 0xFFFFFFFF);

    /* 使能 DMAC */
    AXI_W(AXI_DMAC_RX_BASE, DMAC_REG_CTRL, 0x1);

    xil_printf("[DMAC RX] 初始化完成，已使能\r\n");
}

/*
 * 执行一次 S2MM DMA 传输（非循环，单次）
 * @buf_addr: 目标 DDR 物理地址（必须是 8 字节对齐）
 * @bytes:    传输字节数
 * 返回：0=成功，-1=超时
 */
static int dma_rx_oneshot(u32 buf_addr, u32 bytes)
{
    /*
     * 步骤1：Flush 目标缓冲区的 D-Cache
     * 目的：清除 CPU 可能写入的旧缓存，让 DMA 写操作能覆盖正确的地址
     */
    Xil_DCacheFlushRange((UINTPTR)buf_addr, bytes);

    /* 步骤2：配置传输参数 */
    AXI_W(AXI_DMAC_RX_BASE, DMAC_REG_FLAGS,     0x0);         /* 非循环 */
    AXI_W(AXI_DMAC_RX_BASE, DMAC_REG_DEST_ADDR, buf_addr);    /* 目标地址 */
    AXI_W(AXI_DMAC_RX_BASE, DMAC_REG_SRC_ADDR,  0x0);         /* S2MM 不用 */
    AXI_W(AXI_DMAC_RX_BASE, DMAC_REG_X_LEN,     bytes - 1);   /* 字节数-1 */
    AXI_W(AXI_DMAC_RX_BASE, DMAC_REG_Y_LEN,     0x0);         /* 1D 传输 */

    /* 步骤3：提交传输（写1开始）*/
    AXI_W(AXI_DMAC_RX_BASE, DMAC_REG_XFER_SUB, 0x1);

    u32 xfer_id = AXI_R(AXI_DMAC_RX_BASE, DMAC_REG_XFER_ID);

    /* 步骤4：等待完成（超时 = 1000 ms）*/
    for (int timeout = 0; timeout < 1000; timeout++) {
        u32 done = AXI_R(AXI_DMAC_RX_BASE, DMAC_REG_XFER_DONE);
        if (done & (1 << (xfer_id % 32))) {
            /*
             * 步骤5：Invalidate 目标缓冲区的 D-Cache
             * 目的：让 CPU 读时从 DDR 取新数据，而不是 Cache 里的旧数据
             */
            Xil_DCacheInvalidateRange((UINTPTR)buf_addr, bytes);
            return 0;  /* 成功 */
        }
        usleep(1000);
    }
    return -1;  /* 超时 */
}

/* 分析接收到的 IQ 数据 */
static void analyze_iq_data(u32 buf_addr, u32 n_samples)
{
    volatile int16_t *iq = (volatile int16_t *)buf_addr;
    int32_t sum_i = 0, sum_q = 0;
    int16_t max_i = 0, max_q = 0;
    int32_t power = 0;

    for (u32 i = 0; i < n_samples; i++) {
        int16_t I = iq[2 * i];
        int16_t Q = iq[2 * i + 1];
        sum_i += I;
        sum_q += Q;
        if (I < 0 ? -I : I > max_i) max_i = I < 0 ? -I : I;
        if (Q < 0 ? -Q : Q > max_q) max_q = Q < 0 ? -Q : Q;
        power += (int32_t)I * I + (int32_t)Q * Q;  /* 粗略功率 */
    }

    xil_printf("  样本数: %lu\r\n", n_samples);
    xil_printf("  I 均值: %ld，峰值: %d\r\n", (long)(sum_i / n_samples), max_i);
    xil_printf("  Q 均值: %ld，峰值: %d\r\n", (long)(sum_q / n_samples), max_q);
    xil_printf("  功率（归一化）: %ld\r\n", (long)(power / n_samples));

    /* 打印前 8 个采样 */
    xil_printf("  前 8 个采样 [I, Q]:\r\n");
    for (int i = 0; i < 8; i++) {
        xil_printf("    [%d] I=%-6d  Q=%-6d\r\n", i, iq[2*i], iq[2*i+1]);
    }
}

int main(void)
{
    Xil_ICacheEnable();
    Xil_DCacheEnable();

    xil_printf("\r\n================================================\r\n");
    xil_printf("  ANTSDR E310 裸机 - 步骤 08：AXI DMA\r\n");
    xil_printf("================================================\r\n\r\n");

    xil_printf("[配置] 缓冲地址: 0x%08X，大小: %d KB，采样数: %d\r\n",
               DMA_RX_BUF_ADDR, DMA_BUF_BYTES / 1024, NUM_SAMPLES);

    /* 初始化 ADC 核心和 DMA */
    adc_setup();
    rx_dma_init();

    xil_printf("\r\n[注意] 此步骤假设 AD9361 已初始化并输出数据流。\r\n");
    xil_printf("       如未初始化，DMA 传输会超时或全部采样为 0。\r\n\r\n");

    /* ── 单次 DMA 捕获 ── */
    xil_printf("[DMA] 开始捕获 %d 个 IQ 采样...\r\n", NUM_SAMPLES);
    int ret = dma_rx_oneshot(DMA_RX_BUF_ADDR, DMA_BUF_BYTES);
    if (ret == 0) {
        xil_printf("[DMA] 传输完成！分析数据：\r\n");
        analyze_iq_data(DMA_RX_BUF_ADDR, NUM_SAMPLES);
    } else {
        xil_printf("[DMA] 传输超时（AD9361 数据流未准备好）\r\n");
    }

    xil_printf("\r\n[完成] 步骤 08 执行完毕。\r\n");
    xil_printf("下一步：09-ad9361-init（AD9361 完整初始化）\r\n\r\n");

    while (1) {}
    return 0;
}
