/*
 * 步骤 12：完整 SDR 裸机系统
 *
 * 综合前 11 个步骤，构建完整的软件无线电系统：
 *
 *   UART（调试+命令）
 *      ↓
 *   GPIO（LED 状态指示）
 *      ↓
 *   TTC（系统 Tick，任务调度基础）
 *      ↓
 *   SPI → AD9361（RF 前端）
 *      ↓
 *   AXI ADC + AXI DMAC（高速 IQ 采集）
 *      ↓
 *   DDR3（IQ 数据缓冲）
 *      ↓
 *   ARM 信号处理（功率计、频谱扫描）
 *      ↓
 *   UART 输出（ASCII 频谱显示）
 *
 * 系统功能：
 *   - 上电后初始化所有硬件
 *   - 循环执行"频谱扫描"：扫描 400~500 MHz，每 1 MHz 测量功率
 *   - 用 ASCII 图形显示频谱
 *   - LED 用于状态指示（初始化中=快闪，就绪=慢闪，扫描=常亮）
 *   - 定时器 Tick 驱动系统节拍
 */

#include "xparameters.h"
#include "xgpiops.h"
#include "xscugic.h"
#include "xttcps.h"
#include "xil_io.h"
#include "xil_cache.h"
#include "xil_printf.h"
#include "xil_exception.h"
#include "sleep.h"
#include "../09-ad9361-init/ad9361_spi.h"
#include "../10-ad9361-config/ad9361.h"

/* ════════════════════════════════════════════════
 *   硬件地址
 * ════════════════════════════════════════════════ */
#define GPIO_DEVICE_ID      XPAR_PS7_GPIO_0_DEVICE_ID
#define GIC_DEVICE_ID       XPAR_PS7_SCUGIC_0_DEVICE_ID
#define TTC_DEVICE_ID       XPAR_PS7_TTC_0_0_DEVICE_ID
#define TTC_IRQ_ID          42
#define GPIO_LED_PIN        15
#define GPIO_BTN_PIN        14

#define AXI_ADC_BASE        0x79020000
#define AXI_DMAC_RX_BASE    0x7C400000

/* DMA 缓冲区 */
#define RX_BUF_ADDR         0x01000000
#define RX_BUF_SAMPLES      8192
#define RX_BUF_BYTES        (RX_BUF_SAMPLES * 4)

/* 频谱扫描参数 */
#define SWEEP_START_HZ      400000000ULL
#define SWEEP_STOP_HZ       500000000ULL
#define SWEEP_STEP_HZ       1000000ULL
#define SWEEP_POINTS        ((SWEEP_STOP_HZ - SWEEP_START_HZ) / SWEEP_STEP_HZ + 1)

/* ════════════════════════════════════════════════
 *   全局对象
 * ════════════════════════════════════════════════ */
static XGpioPs g_gpio;
static XScuGic g_gic;
static XTtcPs  g_ttc;

static volatile u32 g_tick_ms   = 0;
static volatile u32 g_btn_count = 0;

/* ════════════════════════════════════════════════
 *   AXI 宏
 * ════════════════════════════════════════════════ */
#define ADC_W(o, v)  Xil_Out32(AXI_ADC_BASE + (o), (v))
#define DMAC_R(o)    Xil_In32(AXI_DMAC_RX_BASE + (o))
#define DMAC_W(o, v) Xil_Out32(AXI_DMAC_RX_BASE + (o), (v))

/* ════════════════════════════════════════════════
 *   系统 Tick ISR（1 ms 周期）
 * ════════════════════════════════════════════════ */
static void tick_isr(void *arg)
{
    XTtcPs *t = (XTtcPs *)arg;
    XTtcPs_ClearInterruptStatus(t, XTtcPs_GetInterruptStatus(t));
    g_tick_ms++;
}

/* ════════════════════════════════════════════════
 *   LED 控制
 * ════════════════════════════════════════════════ */
typedef enum { LED_OFF=0, LED_ON=1 } LedState;

static void led_set(LedState s)
{
    XGpioPs_WritePin(&g_gpio, GPIO_LED_PIN, (u32)s);
}

/* ════════════════════════════════════════════════
 *   硬件初始化
 * ════════════════════════════════════════════════ */
static int hw_init(void)
{
    int ret;

    /* ── GPIO ── */
    XGpioPs_Config *gcfg = XGpioPs_LookupConfig(GPIO_DEVICE_ID);
    ret = XGpioPs_CfgInitialize(&g_gpio, gcfg, gcfg->BaseAddr);
    if (ret != XST_SUCCESS) return -1;
    XGpioPs_SetDirectionPin(&g_gpio, GPIO_LED_PIN, 1);
    XGpioPs_SetOutputEnablePin(&g_gpio, GPIO_LED_PIN, 1);
    XGpioPs_SetDirectionPin(&g_gpio, GPIO_BTN_PIN, 0);
    led_set(LED_OFF);

    /* ── TTC（1 ms Tick）── */
    XTtcPs_Config *tcfg = XTtcPs_LookupConfig(TTC_DEVICE_ID);
    ret = XTtcPs_CfgInitialize(&g_ttc, tcfg, tcfg->BaseAddress);
    if (ret != XST_SUCCESS) return -1;
    {
        XInterval iv; u8 ps;
        XTtcPs_CalcIntervalFromFreq(&g_ttc, 1000, &iv, &ps);
        XTtcPs_SetOptions(&g_ttc, XTTCPS_OPTION_INTERVAL_MODE |
                                   XTTCPS_OPTION_WAVE_DISABLE);
        XTtcPs_SetInterval(&g_ttc, iv);
        XTtcPs_SetPrescaler(&g_ttc, ps);
        XTtcPs_EnableInterrupts(&g_ttc, XTTCPS_IXR_INTERVAL_MASK);
    }

    /* ── GIC ── */
    XScuGic_Config *icfg = XScuGic_LookupConfig(GIC_DEVICE_ID);
    ret = XScuGic_CfgInitialize(&g_gic, icfg, icfg->CpuBaseAddress);
    if (ret != XST_SUCCESS) return -1;
    XScuGic_Connect(&g_gic, TTC_IRQ_ID,
                    (Xil_InterruptHandler)tick_isr, &g_ttc);
    XScuGic_SetPriorityTriggerType(&g_gic, TTC_IRQ_ID, 0xA0, 0x03);
    XScuGic_Enable(&g_gic, TTC_IRQ_ID);
    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_IRQ_INT,
                                  (Xil_ExceptionHandler)XScuGic_InterruptHandler,
                                  &g_gic);
    Xil_ExceptionEnable();
    XTtcPs_Start(&g_ttc);

    xil_printf("[系统] GPIO + TTC + GIC 初始化完成，Tick=1 ms\r\n");
    return 0;
}

static int axi_pipeline_init(void)
{
    /* AXI ADC 复位 */
    ADC_W(0x0040, 0x0); usleep(1000); ADC_W(0x0040, 0x3); usleep(1000);
    ADC_W(0x0400, 0x1); ADC_W(0x0440, 0x1);  /* 使能 I/Q 通道 */

    /* DMAC 初始化 */
    DMAC_W(0x0080, 0xFFFFFFFF);  /* 屏蔽中断 */
    DMAC_W(0x0400, 0x1);         /* 使能 */

    xil_printf("[AXI] ADC 核心 + DMAC 初始化完成\r\n");
    return 0;
}

/* ════════════════════════════════════════════════
 *   DMA 捕获
 * ════════════════════════════════════════════════ */
static int dma_capture(void)
{
    Xil_DCacheFlushRange((UINTPTR)RX_BUF_ADDR, RX_BUF_BYTES);
    DMAC_W(0x040C, 0x0);
    DMAC_W(0x0410, RX_BUF_ADDR);
    DMAC_W(0x0418, RX_BUF_BYTES - 1);
    DMAC_W(0x041C, 0x0);
    DMAC_W(0x0408, 0x1);

    u32 id = DMAC_R(0x0404);
    for (int t = 0; t < 1000; t++) {
        if (DMAC_R(0x0428) & (1u << (id % 32))) {
            Xil_DCacheInvalidateRange((UINTPTR)RX_BUF_ADDR, RX_BUF_BYTES);
            return 0;
        }
        usleep(1000);
    }
    return -1;
}

/* ════════════════════════════════════════════════
 *   信号功率计算（dBFS，无 libm）
 * ════════════════════════════════════════════════ */
static int calc_power_dbfs(void)
{
    volatile int16_t *iq = (volatile int16_t *)RX_BUF_ADDR;
    u32 sum_sq = 0;
    for (u32 i = 0; i < RX_BUF_SAMPLES; i++) {
        int32_t I = iq[2*i];
        int32_t Q = iq[2*i+1];
        sum_sq += (u32)(I*I + Q*Q) >> 14;  /* 右移避免溢出 */
    }
    u32 avg = sum_sq / RX_BUF_SAMPLES;

    /* 整数 log2 近似 */
    if (avg == 0) return -80;
    int bits = 0;
    u32 tmp = avg;
    while (tmp > 1) { tmp >>= 1; bits++; }
    /* 10*log10(avg/2048^2) = 10*log2(avg/2048^2)/log2(10) */
    /* 简化：power_dbfs ≈ 3*(bits - 22) */
    return 3 * (bits - 22);
}

/* ════════════════════════════════════════════════
 *   频谱扫描
 * ════════════════════════════════════════════════ */
static void spectrum_scan(void)
{
    int powers[SWEEP_POINTS];

    xil_printf("\r\n扫描 %.0f~%.0f MHz（步进 %.0f MHz）...\r\n",
               (double)SWEEP_START_HZ/1e6,
               (double)SWEEP_STOP_HZ/1e6,
               (double)SWEEP_STEP_HZ/1e6);

    for (u32 i = 0; i < SWEEP_POINTS; i++) {
        u64 freq = SWEEP_START_HZ + (u64)i * SWEEP_STEP_HZ;
        ad9361_set_rx_lo(freq);
        usleep(5000);  /* PLL 锁定时间 */

        led_set(LED_ON);
        if (dma_capture() == 0)
            powers[i] = calc_power_dbfs();
        else
            powers[i] = -80;
        led_set(LED_OFF);
    }

    /* ── ASCII 频谱图 ── */
    const int BAR_WIDTH = 40;
    int min_p = -80, max_p = -10;

    xil_printf("\r\n");
    xil_printf("  频率(MHz)  |%-*s| dBFS\r\n", BAR_WIDTH, "            功率谱            ");
    xil_printf("  ----------+");
    for (int i = 0; i < BAR_WIDTH; i++) xil_printf("-");
    xil_printf("+------\r\n");

    for (u32 i = 0; i < SWEEP_POINTS; i++) {
        u64 freq = SWEEP_START_HZ + (u64)i * SWEEP_STEP_HZ;
        int p = powers[i];
        int bars = (p - min_p) * BAR_WIDTH / (max_p - min_p);
        if (bars < 0) bars = 0;
        if (bars > BAR_WIDTH) bars = BAR_WIDTH;

        xil_printf("  %7.1f  |", (double)freq / 1e6);
        for (int b = 0; b < BAR_WIDTH; b++)
            xil_printf(b < bars ? (b == bars-1 ? ">" : "=") : " ");
        xil_printf("| %d\r\n", p);
    }
    xil_printf("\r\n");
}

/* ════════════════════════════════════════════════
 *   主函数
 * ════════════════════════════════════════════════ */
int main(void)
{
    Xil_ICacheEnable();
    Xil_DCacheEnable();

    xil_printf("\r\n");
    xil_printf("╔══════════════════════════════════════════════╗\r\n");
    xil_printf("║   ANTSDR E310 完整裸机 SDR 系统              ║\r\n");
    xil_printf("║   步骤 12 — 频谱扫描仪                       ║\r\n");
    xil_printf("╚══════════════════════════════════════════════╝\r\n\r\n");

    /* ── 初始化阶段（LED 快闪）── */
    xil_printf("[1/4] 初始化系统硬件...\r\n");
    for (int i = 0; i < 6; i++) {
        led_set(LED_ON);  usleep(100000);
        led_set(LED_OFF); usleep(100000);
    }
    if (hw_init() != 0) {
        xil_printf("[错误] 系统硬件初始化失败\r\n");
        while (1) {}
    }

    xil_printf("[2/4] 初始化 AD9361...\r\n");
    Ad9361Config rf_cfg = AD9361_DEFAULT_CONFIG;
    rf_cfg.rx_lo_hz    = SWEEP_START_HZ;
    rf_cfg.sample_rate_hz = 5000000;
    rf_cfg.rx1_gain_db = 60;
    if (ad9361_init(&rf_cfg) != 0) {
        xil_printf("[错误] AD9361 初始化失败\r\n");
        while (1) {}
    }

    xil_printf("[3/4] 初始化 AXI 数据管道...\r\n");
    axi_pipeline_init();

    xil_printf("[4/4] 系统就绪！\r\n\r\n");

    /* 系统就绪：LED 慢闪表示待机 */
    u32 last_tick = g_tick_ms;
    u32 scan_count = 0;

    while (1) {
        /* 每 2 秒扫描一次 */
        u32 now = g_tick_ms;
        if (now - last_tick >= 2000) {
            last_tick = now;
            scan_count++;
            xil_printf("══ 第 %lu 次扫描（运行时间 %lu 秒）══\r\n",
                       scan_count, now / 1000);
            spectrum_scan();
        }

        /* LED 慢闪（每 500 ms 切换）*/
        led_set((now / 500) & 1 ? LED_ON : LED_OFF);
    }

    return 0;
}
