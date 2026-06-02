/*
 * 步骤 02：GPIO 控制 LED
 *
 * 硬件：PS GPIO[15] → 板载绿色 LED（高电平亮）
 * 地址：0xE000A000
 *
 * 学习目标：
 *   1. 掌握 XGpioPs 驱动 API
 *   2. 理解 GPIO Bank 和 Pin 的关系
 *   3. 学会方向配置（Input/Output）
 *   4. 用软件延时实现 LED 闪烁
 *
 * GPIO 引脚编号说明（Zynq-7020）：
 *   Bank 0: MIO[0:31]   → Pin 0-31   （本板 LED 在 Pin 15）
 *   Bank 1: MIO[32:53]  → Pin 32-53
 *   Bank 2: EMIO[0:31]  → Pin 54-85  （连接到 PL 管脚）
 *   Bank 3: EMIO[32:63] → Pin 86-117 （连接到 PL 管脚）
 */

#include "xparameters.h"
#include "xgpiops.h"    /* PS GPIO 驱动 */
#include "xil_printf.h"
#include "sleep.h"

/* E310 硬件定义 */
#define GPIO_DEVICE_ID   XPAR_PS7_GPIO_0_DEVICE_ID
#define GPIO_LED_PIN     15   /* 绿色 LED */
#define GPIO_BTN_PIN     14   /* 用户按钮（此步骤只读，下一步才接中断）*/

#define LED_ON   1
#define LED_OFF  0

static XGpioPs gpio;

static int gpio_init(void)
{
    XGpioPs_Config *cfg = XGpioPs_LookupConfig(GPIO_DEVICE_ID);
    if (!cfg) {
        xil_printf("[错误] 找不到 GPIO 配置\r\n");
        return XST_FAILURE;
    }

    int ret = XGpioPs_CfgInitialize(&gpio, cfg, cfg->BaseAddr);
    if (ret != XST_SUCCESS) {
        xil_printf("[错误] GPIO 初始化失败: %d\r\n", ret);
        return ret;
    }

    /* 设置 LED 引脚为输出方向，初始值关闭 */
    XGpioPs_SetDirectionPin(&gpio, GPIO_LED_PIN, 1); /* 1=输出 */
    XGpioPs_SetOutputEnablePin(&gpio, GPIO_LED_PIN, 1);
    XGpioPs_WritePin(&gpio, GPIO_LED_PIN, LED_OFF);

    /* 设置按钮引脚为输入方向 */
    XGpioPs_SetDirectionPin(&gpio, GPIO_BTN_PIN, 0); /* 0=输入 */

    xil_printf("[GPIO] 初始化完成。LED=Pin%d, BTN=Pin%d\r\n",
               GPIO_LED_PIN, GPIO_BTN_PIN);
    return XST_SUCCESS;
}

/* LED 闪烁 N 次，on_ms/off_ms 单位毫秒 */
static void led_blink(int n, int on_ms, int off_ms)
{
    for (int i = 0; i < n; i++) {
        XGpioPs_WritePin(&gpio, GPIO_LED_PIN, LED_ON);
        usleep(on_ms * 1000);
        XGpioPs_WritePin(&gpio, GPIO_LED_PIN, LED_OFF);
        usleep(off_ms * 1000);
    }
}

/*
 * 读取按钮状态并用 LED 反映
 * 按钮按下（低电平）→ LED 亮
 * 按钮释放（高电平）→ LED 灭
 */
static void led_follow_button(int seconds)
{
    xil_printf("[按住按钮] LED 会跟随按钮亮灭，持续 %d 秒\r\n", seconds);
    for (int i = 0; i < seconds * 100; i++) {
        int btn = XGpioPs_ReadPin(&gpio, GPIO_BTN_PIN);
        /* 按钮是低电平有效，取反后驱动 LED */
        XGpioPs_WritePin(&gpio, GPIO_LED_PIN, btn ? LED_OFF : LED_ON);
        usleep(10000);  /* 10 ms 轮询间隔 */
    }
}

int main(void)
{
    Xil_ICacheEnable();
    Xil_DCacheEnable();

    xil_printf("\r\n================================================\r\n");
    xil_printf("  ANTSDR E310 裸机 - 步骤 02：GPIO LED\r\n");
    xil_printf("================================================\r\n\r\n");

    if (gpio_init() != XST_SUCCESS) {
        xil_printf("[致命错误] GPIO 初始化失败，停止执行\r\n");
        while (1) {}
    }

    /* ── 测试 1：快速闪烁 5 次，确认 LED 可控 ── */
    xil_printf("[测试1] LED 快速闪烁 5 次...\r\n");
    led_blink(5, 100, 100);
    xil_printf("  完成\r\n");

    /* ── 测试 2：慢速闪烁 3 次 ── */
    xil_printf("[测试2] LED 慢速闪烁 3 次...\r\n");
    led_blink(3, 500, 500);
    xil_printf("  完成\r\n");

    /* ── 测试 3：SOS 摩尔斯码 ── */
    xil_printf("[测试3] LED 发送 SOS 摩尔斯码...\r\n");
    /* S = ... (三短) */
    led_blink(3, 100, 100);
    usleep(300000);
    /* O = --- (三长) */
    led_blink(3, 300, 100);
    usleep(300000);
    /* S = ... (三短) */
    led_blink(3, 100, 100);
    xil_printf("  完成 (... --- ...)\r\n");

    /* ── 测试 4：按钮控制 LED ── */
    led_follow_button(5);

    xil_printf("\r\n[完成] 步骤 02 执行完毕。\r\n");
    xil_printf("下一步：03-gpio-interrupt（按钮触发中断）\r\n\r\n");

    /* 继续用 LED 慢闪，表示程序正常运行 */
    while (1) {
        led_blink(1, 1000, 1000);
    }

    return 0;
}
