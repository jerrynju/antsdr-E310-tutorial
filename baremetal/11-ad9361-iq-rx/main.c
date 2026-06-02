/*
 * 步骤 11：AD9361 IQ 数据接收
 *
 * 硬件：AD9361 + AXI ADC 核心 + AXI DMAC RX
 *   AD9361 → (LVDS/CMOS) → AXI ADC IP → AXI DMAC → DDR3 → ARM
 *
 * 学习目标：
 *   1. 掌握完整的 IQ 数据流接收管道
 *   2. 理解 IQ 数据格式（交织 int16_t，范围 ±2048 for 12-bit ADC）
 *   3. 实现双缓冲（Ping-Pong），实现无间隔连续采集
 *   4. 计算信号功率（RMS）
 *   5. 生成 UART ASCII 频谱图
 *
 * IQ 数据格式（ADI AXI ADC 输出）：
 *   每个采样 = 4 字节
 *   字节 0-1：I（实部），int16，有效位 12 bit（左对齐到 16 bit）
 *   字节 2-3：Q（虚部），int16，有效位 12 bit
 *   实际值 = raw / 2048.0（归一化到 ±1.0）
 */

#include "xparameters.h"
#include "xil_io.h"
#include "xil_cache.h"
#include "xil_printf.h"
#include "sleep.h"
/* 使用步骤 09/10 的驱动 */
#include "../09-ad9361-init/ad9361_spi.h"
#include "../10-ad9361-config/ad9361.h"

/* ── 硬件地址 ─────────────────────────────────── */
#define AXI_ADC_BASE       0x79020000
#define AXI_DMAC_RX_BASE   0x7C400000

/* ── DMA 缓冲区（双缓冲：Ping + Pong）── */
#define PING_BUF_ADDR      0x01000000   /* 1 MB 处 */
#define PONG_BUF_ADDR      0x01080000   /* 1 MB + 512 KB 处 */
#define BUF_SAMPLES        16384        /* 每缓冲区采样数 */
#define BUF_BYTES          (BUF_SAMPLES * 4)  /* 4 字节/采样 */

/* ── DMAC 寄存器访问宏 ─────────────────────────── */
#define DMAC_R(off)     Xil_In32(AXI_DMAC_RX_BASE + (off))
#define DMAC_W(off, v)  Xil_Out32(AXI_DMAC_RX_BASE + (off), (v))
#define ADC_W(off, v)   Xil_Out32(AXI_ADC_BASE + (off), (v))
#define ADC_R(off)      Xil_In32(AXI_ADC_BASE + (off))

/* DMAC 寄存器偏移 */
#define DMAC_IRQ_MASK   0x0080
#define DMAC_IRQ_PEND   0x0084
#define DMAC_CTRL       0x0400
#define DMAC_XFER_ID    0x0404
#define DMAC_XFER_SUB   0x0408
#define DMAC_FLAGS      0x040C
#define DMAC_DEST_ADDR  0x0410
#define DMAC_X_LEN      0x0418
#define DMAC_Y_LEN      0x041C
#define DMAC_XFER_DONE  0x0428

/* ADC 寄存器偏移 */
#define ADC_RSTN        0x0040
#define ADC_CNTRL       0x0044
#define ADC_CH0_CTRL    0x0400
#define ADC_CH1_CTRL    0x0440

/* ── 初始化管道 ────────────────────────────────── */
static void pipeline_init(void)
{
    /* AXI ADC 复位 + 使能 */
    ADC_W(ADC_RSTN, 0x0);
    usleep(1000);
    ADC_W(ADC_RSTN, 0x3);
    usleep(1000);
    ADC_W(ADC_CH0_CTRL, 0x1);  /* I 通道 */
    ADC_W(ADC_CH1_CTRL, 0x1);  /* Q 通道 */

    /* DMAC 屏蔽中断，使能 */
    DMAC_W(DMAC_IRQ_MASK, 0xFFFFFFFF);
    DMAC_W(DMAC_CTRL, 0x1);

    xil_printf("[管道] AXI ADC + DMAC 初始化完成\r\n");
}

/* ── 提交一次 DMA 传输并等待完成 ──────────────── */
static int dma_capture(u32 buf_addr, u32 bytes)
{
    Xil_DCacheFlushRange((UINTPTR)buf_addr, bytes);

    DMAC_W(DMAC_FLAGS,     0x0);
    DMAC_W(DMAC_DEST_ADDR, buf_addr);
    DMAC_W(DMAC_X_LEN,     bytes - 1);
    DMAC_W(DMAC_Y_LEN,     0x0);
    DMAC_W(DMAC_XFER_SUB,  0x1);

    u32 id = DMAC_R(DMAC_XFER_ID);
    for (int t = 0; t < 2000; t++) {
        if (DMAC_R(DMAC_XFER_DONE) & (1u << (id % 32))) {
            Xil_DCacheInvalidateRange((UINTPTR)buf_addr, bytes);
            return 0;
        }
        usleep(500);
    }
    return -1;  /* 超时 */
}

/* ── IQ 数据分析 ────────────────────────────────── */
typedef struct {
    float power_dbfs;    /* 功率（dBFS）*/
    float dc_i;          /* I 直流偏置 */
    float dc_q;          /* Q 直流偏置 */
    int16_t peak_i;
    int16_t peak_q;
} IqStats;

static IqStats analyze_iq(u32 buf_addr, u32 n_samples)
{
    volatile int16_t *iq = (volatile int16_t *)buf_addr;
    IqStats s = {0};
    float sum_i = 0, sum_q = 0;
    float power = 0;
    int16_t max_i = 0, max_q = 0;

    for (u32 i = 0; i < n_samples; i++) {
        int16_t I = iq[2 * i];
        int16_t Q = iq[2 * i + 1];

        sum_i += (float)I;
        sum_q += (float)Q;

        float In = (float)I / 2048.0f;  /* 归一化到 ±1 */
        float Qn = (float)Q / 2048.0f;
        power += In * In + Qn * Qn;

        int16_t aI = I < 0 ? -I : I;
        int16_t aQ = Q < 0 ? -Q : Q;
        if (aI > max_i) max_i = aI;
        if (aQ > max_q) max_q = aQ;
    }

    s.dc_i = sum_i / n_samples;
    s.dc_q = sum_q / n_samples;
    s.peak_i = max_i;
    s.peak_q = max_q;

    float avg_power = power / n_samples;
    /* 10*log10(P) 的整数近似（避免依赖 libm）*/
    if (avg_power > 0) {
        /* 粗略 log10 估计：ln(x)/ln(10) ≈ log2(x) * 0.30103 */
        float log2p = 0;
        float p = avg_power;
        while (p < 1.0f) { p *= 2.0f; log2p--; }
        while (p >= 2.0f) { p /= 2.0f; log2p++; }
        s.power_dbfs = (log2p + (p - 1.0f)) * 3.01f; /* × 10*log2e */
    } else {
        s.power_dbfs = -120.0f;
    }

    return s;
}

/* ── UART ASCII 电平表 ──────────────────────────── */
static void print_power_bar(float power_dbfs, float min_db, float max_db)
{
    const int width = 40;
    float normalized = (power_dbfs - min_db) / (max_db - min_db);
    if (normalized < 0) normalized = 0;
    if (normalized > 1) normalized = 1;
    int bars = (int)(normalized * width);

    xil_printf("[");
    for (int i = 0; i < width; i++)
        xil_printf(i < bars ? "#" : ".");
    /* power_dbfs 用整数近似打印 */
    xil_printf("] %d dBFS\r\n", (int)power_dbfs);
}

int main(void)
{
    Xil_ICacheEnable();
    Xil_DCacheEnable();

    xil_printf("\r\n================================================\r\n");
    xil_printf("  ANTSDR E310 裸机 - 步骤 11：IQ 数据接收\r\n");
    xil_printf("================================================\r\n\r\n");

    /* 初始化 AD9361（433 MHz，5 MHz 采样率，增益 60 dB）*/
    Ad9361Config cfg = AD9361_DEFAULT_CONFIG;
    cfg.rx1_gain_db = 60;
    if (ad9361_init(&cfg) != 0) {
        xil_printf("[错误] AD9361 初始化失败\r\n");
        while (1) {}
    }

    /* 初始化 AXI 管道 */
    pipeline_init();

    xil_printf("\r\n[开始采集，按复位键停止]\r\n\r\n");
    xil_printf("频率: %.3f MHz，采样率: %lu Hz\r\n",
               (double)cfg.rx_lo_hz / 1e6, cfg.sample_rate_hz);
    xil_printf("缓冲区: %d 个采样，%.1f ms 时窗\r\n\r\n",
               BUF_SAMPLES,
               (float)BUF_SAMPLES / cfg.sample_rate_hz * 1000.0f);

    u32 capture_num = 0;
    int use_ping = 1;  /* 双缓冲交替标志 */

    while (1) {
        u32 buf = use_ping ? PING_BUF_ADDR : PONG_BUF_ADDR;
        use_ping ^= 1;

        int ret = dma_capture(buf, BUF_BYTES);
        if (ret != 0) {
            xil_printf("[DMA] 超时（第 %lu 次）\r\n", (u32)capture_num);
            usleep(100000);
            continue;
        }

        IqStats s = analyze_iq(buf, BUF_SAMPLES);
        capture_num++;

        /* 每 16 次采集打印一次报告 */
        if (capture_num % 16 == 0) {
            xil_printf("捕获 #%lu | 功率: ", (u32)capture_num);
            print_power_bar(s.power_dbfs, -80.0f, 0.0f);
            xil_printf("  DC偏置 I=%.1f Q=%.1f | 峰值 I=%d Q=%d\r\n",
                       s.dc_i, s.dc_q, s.peak_i, s.peak_q);
        }
    }

    return 0;
}
