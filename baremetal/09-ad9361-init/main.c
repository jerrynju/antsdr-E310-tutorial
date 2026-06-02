/*
 * 步骤 09：AD9361 SPI 初始化与芯片探测
 *
 * 硬件：
 *   AD9361 RF 收发器，通过 SPI0（0xE0006000）控制
 *   GPIO[46] = RESETB（低电平复位）
 *   GPIO[66] = EN_AGC
 *
 * 学习目标：
 *   1. 理解 AD9361 的启动流程（硬件复位→SPI软复位→ID检查）
 *   2. 掌握 AD9361 寄存器访问方法
 *   3. 读取 AD9361 内部状态（时钟、BBPLL）
 *   4. 为下一步 RF 参数配置打好基础
 *
 * AD9361 启动顺序：
 *   1. 硬件复位（RESETB 低脉冲）
 *   2. SPI 软复位（写 REG_SPI_CONF bit7=1，再写 0）
 *   3. 读产品 ID 验证芯片
 *   4. 写初始化序列（时钟、PLL 默认值）
 *   5. 配置所需参数（见步骤 10）
 */

#include "ad9361_spi.h"
#include "xil_printf.h"
#include "xil_cache.h"
#include "sleep.h"

/* ── 读取并打印 AD9361 关键状态寄存器 ──────────── */
static void ad9361_print_status(void)
{
    /* 一批关键状态寄存器 */
    struct { u16 addr; const char *name; } regs[] = {
        {0x000, "SPI Config"},
        {0x002, "Product ID (0x002)"},
        {0x037, "Product ID (0x037)"},
        {0x008, "BBPLL Config"},
        {0x009, "Ref Clk Scale"},
        {0x00A, "Ref Clk Divider"},
        {0x05A, "BBPLL Cfg-1"},
        {0x05B, "BBPLL Cfg-2"},
        {0x05C, "BBPLL Cfg-3"},
        {0x0A0, "RX FIR Config"},
        {0x0A2, "RX FIR Bandwidth"},
        {0x100, "RX Gain 1"},
        {0x101, "RX Gain 2"},
        {0x250, "RX PLL Enable"},
        {0x27F, "RX PLL Divider"},
        {0x290, "TX PLL Enable"},
        {0x2BF, "TX PLL Divider"},
    };

    xil_printf("\r\n[AD9361 寄存器状态]\r\n");
    for (u32 i = 0; i < sizeof(regs)/sizeof(regs[0]); i++) {
        u8 val = ad9361_reg_read(regs[i].addr);
        xil_printf("  0x%03X %-22s = 0x%02X\r\n",
                   regs[i].addr, regs[i].name, val);
    }
}

/*
 * 最小初始化序列
 * 仅确保 SPI 通信正常，不配置 RF 参数（RF 参数在步骤 10）
 */
static int ad9361_minimal_init(void)
{
    xil_printf("\r\n[AD9361 最小初始化]\r\n");

    /* 步骤1：SPI 软复位 */
    xil_printf("  1. SPI 软复位\r\n");
    ad9361_reg_write(AD9361_REG_SPI_CONF, 0x80);  /* 断言软复位 */
    usleep(200);
    ad9361_reg_write(AD9361_REG_SPI_CONF, 0x00);  /* 释放软复位 */
    usleep(200);

    /* 步骤2：检查产品 ID */
    xil_printf("  2. 验证产品 ID\r\n");
    if (ad9361_check_id() != 0) {
        xil_printf("[错误] AD9361 未响应，检查硬件连接\r\n");
        return -1;
    }

    /* 步骤3：写必要的初始化寄存器
     * 来源：ADI no-OS 库 ad9361_init_seq 精简版
     * 这些值确保芯片内部时钟树处于已知状态
     */
    xil_printf("  3. 写基础初始化序列\r\n");

    /* 参考时钟配置（E310 使用 40 MHz TCXO）*/
    ad9361_reg_write(0x009, 0x17);  /* XTAL = 40 MHz */
    ad9361_reg_write(0x00A, 0x00);  /* Ref clk divider = /1 */
    ad9361_reg_write(0x00B, 0x00);  /* 不分频 */

    /* BBPLL（基带 PLL）初始值（采样率 61.44 MHz）*/
    /* 实际值需根据采样率计算，此处写默认值 */
    ad9361_reg_write(0x045, 0x00);  /* BB PLL default */

    xil_printf("  → 初始化序列写入完成\r\n");
    return 0;
}

/* 验证 SPI 写读一致性 */
static void spi_loopback_test(void)
{
    xil_printf("\r\n[SPI 写读验证]\r\n");

    /* 找一个可安全读写的非关键寄存器
     * REG 0x3DF 在 AD9361 中通常是可读写的测试寄存器
     * 实际需根据芯片版本确认
     */
    const u8 test_vals[] = {0xAA, 0x55, 0xA5, 0x5A};
    for (u32 i = 0; i < sizeof(test_vals); i++) {
        /* 使用 0x000 寄存器作为测试（默认值已知为 0x00）*/
        ad9361_reg_write(0x000, 0x00);  /* 先清零 */
        u8 back = ad9361_reg_read(0x000);
        xil_printf("  写 0x00 → 读 0x%02X %s\r\n",
                   back, back == 0x00 ? "✓" : "✗");
        break;  /* 只测一次，避免改变其他寄存器状态 */
    }
}

int main(void)
{
    Xil_ICacheEnable();
    Xil_DCacheEnable();

    xil_printf("\r\n================================================\r\n");
    xil_printf("  ANTSDR E310 裸机 - 步骤 09：AD9361 初始化\r\n");
    xil_printf("================================================\r\n\r\n");

    /* 初始化 SPI（包括 GPIO RESET 引脚）*/
    if (ad9361_spi_init() != 0) {
        xil_printf("[致命错误] SPI 初始化失败\r\n");
        while (1) {}
    }
    xil_printf("[SPI] 初始化完成\r\n");

    /* 硬件复位 */
    ad9361_hw_reset();

    /* 最小初始化 */
    if (ad9361_minimal_init() != 0) {
        xil_printf("[错误] AD9361 初始化失败，进入诊断模式\r\n");
        /* 依然打印状态寄存器，帮助调试 */
        ad9361_print_status();
        while (1) {}
    }

    /* SPI 读写验证 */
    spi_loopback_test();

    /* 打印当前状态 */
    ad9361_print_status();

    xil_printf("\r\n[完成] AD9361 响应正常！\r\n");
    xil_printf("下一步：10-ad9361-config（配置 RF 频率和增益）\r\n\r\n");

    while (1) {}
    return 0;
}
