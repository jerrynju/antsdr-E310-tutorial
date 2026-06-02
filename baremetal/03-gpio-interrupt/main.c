/*
 * 步骤 03：GPIO 外部中断 + GIC 中断控制器
 *
 * 硬件：
 *   PS GPIO[14] → 用户按钮（按下=低电平，下降沿触发中断）
 *   PS GPIO[15] → LED（每次按钮中断触发后翻转）
 *   GIC @ 0xF8F01000
 *
 * 学习目标：
 *   1. 理解 Zynq GIC（Generic Interrupt Controller）层次结构
 *   2. 掌握 XScuGic 驱动：注册 ISR、设置优先级、使能中断
 *   3. 理解 GPIO 中断：edge/level 触发，中断状态清除
 *   4. 掌握 ISR 编写规范（快速、不阻塞、清中断标志）
 *
 * GIC 中断处理流程：
 *   硬件中断 → GIC 分发器 → GIC CPU接口 → ARM IRQ 异常 →
 *   Xilinx BSP 向量表 → XScuGic_InterruptHandler → 用户 ISR
 *
 * 重要：在 Vitis BSP 设置中开启 "Enable Interrupts"
 *       或在 lscript.ld 中确保 vector 表已包含
 */

#include "xparameters.h"
#include "xgpiops.h"
#include "xscugic.h"    /* ARM Cortex-A9 通用中断控制器 */
#include "xil_printf.h"
#include "xil_exception.h"  /* 使能 ARM 异常 */
#include "sleep.h"

/* 硬件常量 */
#define GPIO_DEVICE_ID   XPAR_PS7_GPIO_0_DEVICE_ID
#define GIC_DEVICE_ID    XPAR_PS7_SCUGIC_0_DEVICE_ID
#define GPIO_LED_PIN     15
#define GPIO_BTN_PIN     14
#define GPIO_IRQ_ID      52   /* PS GPIO 中断号（固定值）*/

/* 按钮按下计数（在 ISR 中递增）*/
static volatile u32 btn_press_count = 0;
/* LED 当前状态 */
static volatile u32 led_state = 0;

static XGpioPs  gpio;
static XScuGic  gic;

/* ── ISR（中断服务例程）── 必须快速执行 ─────────────── */
static void gpio_isr(void *arg)
{
    XGpioPs *gpio_ptr = (XGpioPs *)arg;

    /* 读取 Bank0 的中断状态（哪个引脚触发了中断）*/
    u32 pending = XGpioPs_IntrGetStatusPin(gpio_ptr, GPIO_BTN_PIN);

    if (pending) {
        /* 计数 */
        btn_press_count++;

        /* 翻转 LED */
        led_state ^= 1;
        XGpioPs_WritePin(gpio_ptr, GPIO_LED_PIN, led_state);

        /*
         * 必须清除中断标志，否则会持续触发！
         * 向中断状态寄存器写1来清除对应位。
         */
        XGpioPs_IntrClearPin(gpio_ptr, GPIO_BTN_PIN);
    }
}

static int gic_init(void)
{
    XScuGic_Config *cfg = XScuGic_LookupConfig(GIC_DEVICE_ID);
    if (!cfg) return XST_FAILURE;

    int ret = XScuGic_CfgInitialize(&gic, cfg, cfg->CpuBaseAddress);
    if (ret != XST_SUCCESS) return ret;

    /*
     * 注册 GPIO 中断处理函数
     * 参数：GIC实例, 中断ID, 处理函数, 处理函数参数
     */
    ret = XScuGic_Connect(&gic, GPIO_IRQ_ID,
                          (Xil_InterruptHandler)gpio_isr,
                          (void *)&gpio);
    if (ret != XST_SUCCESS) return ret;

    /* 设置中断优先级（0xA0）和触发类型（0x01=边沿，0x03=电平）*/
    XScuGic_SetPriorityTriggerType(&gic, GPIO_IRQ_ID, 0xA0, 0x03);

    /* 使能 GIC 中的这路中断 */
    XScuGic_Enable(&gic, GPIO_IRQ_ID);

    xil_printf("[GIC] 初始化完成，GPIO 中断 ID=%d 已注册\r\n", GPIO_IRQ_ID);
    return XST_SUCCESS;
}

static int gpio_init_with_intr(void)
{
    XGpioPs_Config *cfg = XGpioPs_LookupConfig(GPIO_DEVICE_ID);
    int ret = XGpioPs_CfgInitialize(&gpio, cfg, cfg->BaseAddr);
    if (ret != XST_SUCCESS) return ret;

    /* LED：输出 */
    XGpioPs_SetDirectionPin(&gpio, GPIO_LED_PIN, 1);
    XGpioPs_SetOutputEnablePin(&gpio, GPIO_LED_PIN, 1);
    XGpioPs_WritePin(&gpio, GPIO_LED_PIN, 0);

    /* 按钮：输入，配置中断 */
    XGpioPs_SetDirectionPin(&gpio, GPIO_BTN_PIN, 0);

    /*
     * 设置中断类型：边沿触发（1=边沿, 0=电平）
     * 极性：下降沿（0=低/下降, 1=高/上升）
     * 任意边沿：0=单边沿
     */
    XGpioPs_SetIntrTypePin(&gpio, GPIO_BTN_PIN,
                            XGPIOPS_IRQ_TYPE_EDGE_FALLING);

    /* 使能该引脚的中断 */
    XGpioPs_IntrEnablePin(&gpio, GPIO_BTN_PIN);

    xil_printf("[GPIO] 按钮中断配置完成（Pin%d, 下降沿）\r\n", GPIO_BTN_PIN);
    return XST_SUCCESS;
}

int main(void)
{
    Xil_ICacheEnable();
    Xil_DCacheEnable();

    xil_printf("\r\n================================================\r\n");
    xil_printf("  ANTSDR E310 裸机 - 步骤 03：GPIO 中断\r\n");
    xil_printf("================================================\r\n\r\n");

    /* 初始化 GPIO */
    if (gpio_init_with_intr() != XST_SUCCESS) {
        xil_printf("[错误] GPIO 初始化失败\r\n");
        while (1) {}
    }

    /* 初始化 GIC */
    if (gic_init() != XST_SUCCESS) {
        xil_printf("[错误] GIC 初始化失败\r\n");
        while (1) {}
    }

    /*
     * 连接 Vitis BSP 异常向量到 GIC handler
     * 这两行是使能 ARM 中断的关键！缺少会导致中断永远不响应。
     */
    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_IRQ_INT,
                                  (Xil_ExceptionHandler)XScuGic_InterruptHandler,
                                  &gic);
    Xil_ExceptionEnable();  /* 清除 ARM CPSR 的 I 位（使能 IRQ）*/

    xil_printf("[就绪] 请按下按钮，LED 会翻转，串口会打印计数\r\n\r\n");

    u32 last_count = 0;
    while (1) {
        /*
         * 主循环什么都不做（中断驱动模式）。
         * 只做一件事：打印变化的按钮计数。
         *
         * 注意：读取 volatile 变量时无需关中断，因为 u32 读写是原子的。
         */
        if (btn_press_count != last_count) {
            xil_printf("  按钮已按下 %lu 次，LED 状态 = %lu\r\n",
                       (u32)btn_press_count, (u32)led_state);
            last_count = btn_press_count;
        }
        usleep(10000);  /* 10 ms 轮询，降低 CPU 空转功耗 */
    }

    return 0;
}
