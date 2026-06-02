/*
 * ad9361_spi.c — AD9361 SPI 底层驱动实现
 */
#include "ad9361_spi.h"
#include "xparameters.h"
#include "xspips.h"
#include "xgpiops.h"
#include "xil_printf.h"
#include "sleep.h"

/* Zynq PS SPI0（0xE0006000）连接 AD9361 */
#define SPI_DEVICE_ID   XPAR_PS7_SPI_0_DEVICE_ID
#define GPIO_DEVICE_ID  XPAR_PS7_GPIO_0_DEVICE_ID

static XSpiPs  g_spi;
static XGpioPs g_gpio;
static int     g_init_done = 0;

int ad9361_spi_init(void)
{
    /* ── GPIO 初始化（RESET 引脚）── */
    XGpioPs_Config *gcfg = XGpioPs_LookupConfig(GPIO_DEVICE_ID);
    XGpioPs_CfgInitialize(&g_gpio, gcfg, gcfg->BaseAddr);

    /* RESETB 输出，初始高（不复位）*/
    XGpioPs_SetDirectionPin(&g_gpio, E310_GPIO_AD9361_RESET, 1);
    XGpioPs_SetOutputEnablePin(&g_gpio, E310_GPIO_AD9361_RESET, 1);
    XGpioPs_WritePin(&g_gpio, E310_GPIO_AD9361_RESET, 1); /* RESETB=1 正常工作 */

    /* EN_AGC 输出，初始低 */
    XGpioPs_SetDirectionPin(&g_gpio, E310_GPIO_AD9361_EN_AGC, 1);
    XGpioPs_SetOutputEnablePin(&g_gpio, E310_GPIO_AD9361_EN_AGC, 1);
    XGpioPs_WritePin(&g_gpio, E310_GPIO_AD9361_EN_AGC, 0);

    /* ── SPI 初始化 ── */
    XSpiPs_Config *cfg = XSpiPs_LookupConfig(SPI_DEVICE_ID);
    if (!cfg) {
        xil_printf("[ad9361_spi] 找不到 SPI0 配置\r\n");
        return -1;
    }
    int ret = XSpiPs_CfgInitialize(&g_spi, cfg, cfg->BaseAddress);
    if (ret != XST_SUCCESS) return -1;

    /* 主机模式，CPOL=0 CPHA=0（AD9361 SPI Mode 0），手动 CS */
    XSpiPs_SetOptions(&g_spi,
        XSPIPS_MASTER_OPTION | XSPIPS_FORCE_SSELECT_OPTION);

    /* 时钟分频：PS SPI 时钟约 100 MHz，/16 ≈ 6.25 MHz（保守值）*/
    XSpiPs_SetClkPrescaler(&g_spi, XSPIPS_CLK_PRESCALE_16);

    /* 取消所有片选（FORCE_SSELECT 模式需要手动设置）*/
    XSpiPs_SetSlaveSelect(&g_spi, 0xF);

    g_init_done = 1;
    return 0;
}

/*
 * AD9361 SPI 3字节帧传输
 * Byte0: [R/W, 0, 0, A12:A8]
 * Byte1: A7:A0
 * Byte2: 写数据 / 读返回
 */
static u8 spi_transfer_3byte(u8 b0, u8 b1, u8 b2)
{
    u8 tx[3] = {b0, b1, b2};
    u8 rx[3] = {0, 0, 0};

    XSpiPs_SetSlaveSelect(&g_spi, 0x00);  /* CS0 有效（低）*/
    XSpiPs_Transfer(&g_spi, tx, rx, 3);
    XSpiPs_SetSlaveSelect(&g_spi, 0x0F);  /* CS 全部无效 */

    return rx[2];
}

void ad9361_reg_write(u16 reg, u8 val)
{
    u8 b0 = (u8)((reg >> 8) & 0x1F);  /* R/W=0 */
    u8 b1 = (u8)(reg & 0xFF);
    spi_transfer_3byte(b0, b1, val);
}

u8 ad9361_reg_read(u16 reg)
{
    u8 b0 = (u8)((reg >> 8) & 0x1F) | 0x80;  /* R/W=1 */
    u8 b1 = (u8)(reg & 0xFF);
    return spi_transfer_3byte(b0, b1, 0x00);
}

void ad9361_reg_write_n(u16 reg, const u8 *buf, u32 len)
{
    for (u32 i = 0; i < len; i++)
        ad9361_reg_write(reg + i, buf[i]);
}

void ad9361_hw_reset(void)
{
    xil_printf("[ad9361] 硬件复位...\r\n");
    XGpioPs_WritePin(&g_gpio, E310_GPIO_AD9361_RESET, 0);  /* RESETB=0 复位 */
    usleep(200);
    XGpioPs_WritePin(&g_gpio, E310_GPIO_AD9361_RESET, 1);  /* RESETB=1 释放 */
    usleep(10000);  /* 等待 AD9361 上电完成（至少 1 ms，保守用 10 ms）*/
    xil_printf("[ad9361] 硬件复位完成\r\n");
}

int ad9361_check_id(void)
{
    u8 pid = ad9361_reg_read(AD9361_REG_PRODUCT_ID);
    xil_printf("[ad9361] 产品 ID = 0x%02X\r\n", pid);

    if ((pid & AD9361_PRODUCT_ID_MASK) == AD9361_PRODUCT_ID_VAL) {
        xil_printf("[ad9361] AD9361 识别成功 ✓\r\n");
        return 0;
    }
    xil_printf("[ad9361] 识别失败（期望 0x%02X，实际 0x%02X）\r\n",
               AD9361_PRODUCT_ID_VAL, pid & AD9361_PRODUCT_ID_MASK);
    return -1;
}
