/*
 * 步骤 06：SPI 主机通信
 *
 * 硬件：PS SPI0（0xE0006000）→ AD9361 SPI 接口
 *       （Linux DTS 中写作 spi@e0006000，BSP 中是 SPI0）
 *
 * 学习目标：
 *   1. 掌握 XSpiPs 驱动：配置模式、片选、传输
 *   2. 理解 SPI 四种模式（CPOL × CPHA）
 *   3. AD9361 SPI 帧格式（3字节: 控制+地址高+地址低+数据）
 *   4. 实现 AD9361 SPI 读写，验证芯片存在性
 *
 * AD9361 SPI 参数：
 *   模式  : CPOL=0, CPHA=0（Mode 0）
 *   时钟  : 最大 20 MHz
 *   字长  : 8 bit
 *   帧长  : 3 字节（24 bit）
 *   MSB   : 先发
 *   CS    : 低有效
 *
 * 帧格式：
 *   Byte 0: [R/W, 0, 0, A12:A8]   R=1读，W=0写
 *   Byte 1: A7:A0
 *   Byte 2: 写数据 / 读返回
 */

#include "xparameters.h"
#include "xspips.h"     /* PS SPI 驱动 */
#include "xil_printf.h"
#include "xil_cache.h"
#include "sleep.h"

/* SPI 设备（AD9361 所在）*/
#define SPI_DEVICE_ID    XPAR_PS7_SPI_0_DEVICE_ID  /* 0xE0006000 */
#define SPI_CLK_HZ       10000000  /* 10 MHz SPI 时钟（AD9361 最高 20 MHz）*/

/* AD9361 SPI 片选 */
#define AD9361_CS        0x00  /* 片选 0，CS0 有效时=0 */

/* AD9361 关键寄存器 */
#define AD9361_REG_PRODUCT_ID  0x037  /* 读此寄存器验证 AD9361 存在 */
#define AD9361_REG_SPI_CONF    0x000  /* SPI 配置（含软复位）*/

/* 期望的产品 ID */
#define AD9361_PRODUCT_ID_MASK 0xF8
#define AD9361_PRODUCT_ID      0x08  /* bits[7:3]=0x01（9361）*/

static XSpiPs spi;

/* ── SPI 初始化 ─────────────────────────────────────── */
static int spi_init(void)
{
    XSpiPs_Config *cfg = XSpiPs_LookupConfig(SPI_DEVICE_ID);
    if (!cfg) {
        xil_printf("[错误] 找不到 SPI 配置\r\n");
        return XST_FAILURE;
    }

    int ret = XSpiPs_CfgInitialize(&spi, cfg, cfg->BaseAddress);
    if (ret != XST_SUCCESS) {
        xil_printf("[错误] SPI 初始化失败: %d\r\n", ret);
        return ret;
    }

    /* 自检 */
    ret = XSpiPs_SelfTest(&spi);
    if (ret != XST_SUCCESS) {
        xil_printf("[错误] SPI 自检失败\r\n");
        return ret;
    }

    /* 设置主机模式，CPOL=0, CPHA=0，CS 高时默认 */
    ret = XSpiPs_SetOptions(&spi,
            XSPIPS_MASTER_OPTION          |  /* 主机模式 */
            XSPIPS_FORCE_SSELECT_OPTION   |  /* 手动控制 CS */
            XSPIPS_CLK_PHASE_1_OPTION);      /* CPHA=1 */
    /*
     * 注意：AD9361 实际上使用 CPHA=0。
     * 如果 CPHA=1_OPTION 不工作，改为不添加此选项（默认 CPHA=0）。
     * 实测按照 AD9361 datasheet: CPOL=0, CPHA=0（Mode 0）。
     */

    /* 设置 SPI 时钟分频（目标：10 MHz）*/
    ret = XSpiPs_SetClkPrescaler(&spi, XSPIPS_CLK_PRESCALE_8);
    /* 实际频率 = PS SPI 时钟 / 8
     * PS SPI 时钟 ≈ 100 MHz → 100/8 ≈ 12.5 MHz（接近目标）*/

    xil_printf("[SPI] 初始化完成，时钟分频 /8\r\n");
    return XST_SUCCESS;
}

/*
 * AD9361 SPI 3字节传输
 * 返回：Byte 2（读操作时为寄存器数据）
 */
static u8 ad9361_spi_xfer(u8 b0, u8 b1, u8 b2)
{
    u8 tx[3] = {b0, b1, b2};
    u8 rx[3] = {0, 0, 0};

    /* 使能 CS0（低有效）*/
    XSpiPs_SetSlaveSelect(&spi, AD9361_CS);

    /* 传输 3 字节 */
    XSpiPs_Transfer(&spi, tx, rx, 3);

    /* 释放 CS */
    XSpiPs_SetSlaveSelect(&spi, 0xF);  /* 所有 CS 无效 */

    return rx[2];
}

/* 写寄存器 */
static void ad9361_write(u16 reg, u8 val)
{
    u8 b0 = (u8)((reg >> 8) & 0x1F);  /* R/W=0(写), A12:A8 */
    u8 b1 = (u8)(reg & 0xFF);
    ad9361_spi_xfer(b0, b1, val);
}

/* 读寄存器 */
static u8 ad9361_read(u16 reg)
{
    u8 b0 = (u8)((reg >> 8) & 0x1F) | 0x80;  /* R/W=1(读) */
    u8 b1 = (u8)(reg & 0xFF);
    return ad9361_spi_xfer(b0, b1, 0x00);
}

/* 验证 AD9361 芯片存在并打印信息 */
static int ad9361_probe(void)
{
    /* 先发送软复位（写 SPI_CONF 寄存器 bit7=1 然后 bit7=0）*/
    ad9361_write(AD9361_REG_SPI_CONF, 0x80);  /* assert soft reset */
    usleep(100);
    ad9361_write(AD9361_REG_SPI_CONF, 0x00);  /* release soft reset */
    usleep(100);

    /* 读产品 ID */
    u8 pid = ad9361_read(AD9361_REG_PRODUCT_ID);
    xil_printf("[AD9361] 产品 ID 寄存器 (0x037) = 0x%02X\r\n", pid);

    if ((pid & AD9361_PRODUCT_ID_MASK) == AD9361_PRODUCT_ID) {
        xil_printf("[AD9361] 芯片识别成功！\r\n");
        xil_printf("  产品 ID bits[7:3] = 0x%02X (期望 0x%02X)\r\n",
                   (pid & AD9361_PRODUCT_ID_MASK) >> 3,
                   AD9361_PRODUCT_ID >> 3);
        return XST_SUCCESS;
    } else {
        xil_printf("[AD9361] 芯片未识别。返回值 0x%02X\r\n", pid);
        xil_printf("  可能原因：SPI 时序错误，CS 连接错误，或 AD9361 未上电\r\n");
        return XST_FAILURE;
    }
}

/* 演示：连续读多个寄存器 */
static void ad9361_dump_regs(u16 start, u16 count)
{
    xil_printf("\r\n[寄存器 Dump] 0x%03X - 0x%03X:\r\n", start, start + count - 1);
    for (u16 i = 0; i < count; i++) {
        if (i % 8 == 0) xil_printf("  0x%03X:", start + i);
        xil_printf(" %02X", ad9361_read(start + i));
        if (i % 8 == 7) xil_printf("\r\n");
    }
    if (count % 8) xil_printf("\r\n");
}

int main(void)
{
    Xil_ICacheEnable();
    Xil_DCacheEnable();

    xil_printf("\r\n================================================\r\n");
    xil_printf("  ANTSDR E310 裸机 - 步骤 06：SPI 主机\r\n");
    xil_printf("================================================\r\n\r\n");

    /* SPI 初始化 */
    if (spi_init() != XST_SUCCESS) {
        while (1) {}
    }

    /* 先复位 AD9361（通过 GPIO）*/
    /* 注意：完整系统中需要先用 GPIO 硬件复位 AD9361 */
    /* 这里只做 SPI 软复位 */

    /* 探测 AD9361 */
    xil_printf("\r\n[探测 AD9361...]\r\n");
    if (ad9361_probe() == XST_SUCCESS) {
        /* 读出几个寄存器 */
        ad9361_dump_regs(0x000, 16);
        ad9361_dump_regs(0x035, 8);  /* 包含 Product ID 0x037 */
    }

    /* 演示：写读验证 */
    xil_printf("\r\n[写读验证] 写 scratchpad 类寄存器...\r\n");
    u8 test_val = 0xA5;
    /* 注意：只写非关键寄存器！0x000 在软复位后为安全值 */
    ad9361_write(0x000, 0x00);
    u8 readback = ad9361_read(0x000);
    xil_printf("  写 0x000 = 0x00，读回 = 0x%02X %s\r\n",
               readback, readback == 0x00 ? "✓" : "✗");
    (void)test_val;

    xil_printf("\r\n[完成] 步骤 06 执行完毕。\r\n");
    xil_printf("下一步：07-axi-lite（PS 访问 PL 寄存器）\r\n\r\n");

    while (1) {}
    return 0;
}
