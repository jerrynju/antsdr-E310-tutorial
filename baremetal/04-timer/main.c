/*
 * 步骤 04：定时器（TTC — Triple Timer Counter）
 *
 * 硬件：TTC0（0xF8001000），内含 3 个独立 16 位计数器
 *
 * 学习目标：
 *   1. 掌握 XTtcPs 驱动
 *   2. 理解定时器模式：间隔模式（周期中断）vs 事件计数模式
 *   3. 基于定时器实现精确延时（比 usleep 更精准）
 *   4. 用定时器中断实现周期任务（调度基础）
 *
 * E310 时钟源：
 *   TTC 时钟 = CPU_1x 时钟 / N
 *   通常：CPU_1x = 111 MHz（333 MHz / 3）
 */

#include "xparameters.h"
#include "xttcps.h"     /* Triple Timer Counter 驱动 */
#include "xscugic.h"
#include "xgpiops.h"
#include "xil_printf.h"
#include "xil_exception.h"

/* 使用 TTC0 的 Timer 0 */
#define TTC_DEVICE_ID    XPAR_PS7_TTC_0_0_DEVICE_ID
#define TTC_INTR_ID      42   /* TTC0_0 中断 ID */
#define GIC_DEVICE_ID    XPAR_PS7_SCUGIC_0_DEVICE_ID

/* LED */
#define GPIO_DEVICE_ID   XPAR_PS7_GPIO_0_DEVICE_ID
#define GPIO_LED_PIN     15

/* 定时器目标：每 500ms 触发一次中断 */
#define TIMER_HZ         2   /* 2 Hz = 500 ms 周期 */

static XTtcPs  ttc;
static XScuGic gic;
static XGpioPs gpio;

static volatile u32 tick_count = 0;

/* ── 定时器 ISR ────────────────────────────────────── */
static void ttc_isr(void *arg)
{
    XTtcPs *ttc_ptr = (XTtcPs *)arg;

    /* 读取并清除中断状态（必须，否则持续触发）*/
    u32 status = XTtcPs_GetInterruptStatus(ttc_ptr);
    XTtcPs_ClearInterruptStatus(ttc_ptr, status);

    if (status & XTTCPS_IXR_INTERVAL_MASK) {
        tick_count++;

        /* 翻转 LED */
        u32 led = XGpioPs_ReadPin(&gpio, GPIO_LED_PIN);
        XGpioPs_WritePin(&gpio, GPIO_LED_PIN, led ^ 1);
    }
}

/* ── 初始化定时器为间隔模式 ─────────────────────────── */
static int timer_init(u32 hz)
{
    XTtcPs_Config *cfg = XTtcPs_LookupConfig(TTC_DEVICE_ID);
    if (!cfg) return XST_FAILURE;

    int ret = XTtcPs_CfgInitialize(&ttc, cfg, cfg->BaseAddress);
    if (ret != XST_SUCCESS) return ret;

    /*
     * 设置间隔时间：
     * XTtcPs_CalcIntervalFromFreq 计算合适的预分频和计数值
     * 使定时器按指定频率触发中断
     */
    XInterval interval;
    u8 prescaler;
    XTtcPs_CalcIntervalFromFreq(&ttc, hz, &interval, &prescaler);

    xil_printf("[TTC] 频率=%d Hz，Interval=%u，Prescaler=%u\r\n",
               hz, (u32)interval, (u32)prescaler);

    /* 配置为间隔模式（周期触发），计数到 interval 后复位 */
    XTtcPs_SetOptions(&ttc, XTTCPS_OPTION_INTERVAL_MODE |
                             XTTCPS_OPTION_WAVE_DISABLE);
    XTtcPs_SetInterval(&ttc, interval);
    XTtcPs_SetPrescaler(&ttc, prescaler);

    /* 使能间隔中断 */
    XTtcPs_EnableInterrupts(&ttc, XTTCPS_IXR_INTERVAL_MASK);

    return XST_SUCCESS;
}

static int gic_init(void)
{
    XScuGic_Config *cfg = XScuGic_LookupConfig(GIC_DEVICE_ID);
    int ret = XScuGic_CfgInitialize(&gic, cfg, cfg->CpuBaseAddress);
    if (ret != XST_SUCCESS) return ret;

    ret = XScuGic_Connect(&gic, TTC_INTR_ID,
                          (Xil_InterruptHandler)ttc_isr, &ttc);
    if (ret != XST_SUCCESS) return ret;

    XScuGic_SetPriorityTriggerType(&gic, TTC_INTR_ID, 0xA0, 0x03);
    XScuGic_Enable(&gic, TTC_INTR_ID);

    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_IRQ_INT,
                                  (Xil_ExceptionHandler)XScuGic_InterruptHandler,
                                  &gic);
    Xil_ExceptionEnable();
    return XST_SUCCESS;
}

/* ── 基于 TTC 的高精度延时（微秒级）────────────────── */
static void timer_delay_us(u32 us)
{
    /*
     * 使用 TTC 的自由运行模式测量经过时间。
     * 简单实现：保存起始计数值，循环直到经过足够周期。
     * 注意：只适用于延时值 < 一个计数周期时。
     */
    u32 start = XTtcPs_GetCounterValue(&ttc);
    /* 将 us 转换为计数器步数（需要知道计数器频率）*/
    /* 简化：此处用 CPU 周期估算，生产代码应校准 */
    volatile u32 cycles = us * 111; /* 粗略估算 111 cycles/us @111 MHz */
    while (cycles--) {
        asm volatile("nop");
    }
    (void)start;
}

int main(void)
{
    Xil_ICacheEnable();
    Xil_DCacheEnable();

    xil_printf("\r\n================================================\r\n");
    xil_printf("  ANTSDR E310 裸机 - 步骤 04：定时器\r\n");
    xil_printf("================================================\r\n\r\n");

    /* 初始化 GPIO（LED）*/
    XGpioPs_Config *gpio_cfg = XGpioPs_LookupConfig(GPIO_DEVICE_ID);
    XGpioPs_CfgInitialize(&gpio, gpio_cfg, gpio_cfg->BaseAddr);
    XGpioPs_SetDirectionPin(&gpio, GPIO_LED_PIN, 1);
    XGpioPs_SetOutputEnablePin(&gpio, GPIO_LED_PIN, 1);
    XGpioPs_WritePin(&gpio, GPIO_LED_PIN, 0);

    /* 初始化定时器 */
    if (timer_init(TIMER_HZ) != XST_SUCCESS) {
        xil_printf("[错误] 定时器初始化失败\r\n");
        while (1) {}
    }

    /* 初始化 GIC */
    if (gic_init() != XST_SUCCESS) {
        xil_printf("[错误] GIC 初始化失败\r\n");
        while (1) {}
    }

    /* 启动定时器 */
    XTtcPs_Start(&ttc);
    xil_printf("[TTC] 定时器启动，每 %d ms 中断一次\r\n",
               1000 / TIMER_HZ);
    xil_printf("  → LED 以 %d Hz 闪烁\r\n", TIMER_HZ);
    xil_printf("  → 串口每 2 秒打印一次 tick 计数\r\n\r\n");

    u32 last_print = 0;
    while (1) {
        u32 now = tick_count;
        /* 每 2 * TIMER_HZ 个 tick 打印一次 */
        if (now - last_print >= (u32)(2 * TIMER_HZ)) {
            xil_printf("  Tick count = %lu，运行时间 ≈ %lu 秒\r\n",
                       now, now / TIMER_HZ);
            last_print = now;
        }
    }

    return 0;
}
