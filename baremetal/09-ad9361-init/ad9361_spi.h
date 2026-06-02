/*
 * ad9361_spi.h — AD9361 SPI 底层驱动
 *
 * 封装 Zynq PS SPI0 的 3 字节帧传输。
 * 上层调用 ad9361_reg_write / ad9361_reg_read 操作寄存器。
 */
#ifndef AD9361_SPI_H
#define AD9361_SPI_H

#include "xil_types.h"

/* ── SPI 初始化 ─────────────────────────────── */
int  ad9361_spi_init(void);

/* ── 寄存器读写 ─────────────────────────────── */
void ad9361_reg_write(u16 reg, u8 val);
u8   ad9361_reg_read(u16 reg);

/* 多字节连续写（地址自动递增）*/
void ad9361_reg_write_n(u16 reg, const u8 *buf, u32 len);

/* ── GPIO 硬件复位 ──────────────────────────── */
void ad9361_hw_reset(void);   /* RESETB 低脉冲 */

/* ── 产品 ID 验证 ────────────────────────────── */
int  ad9361_check_id(void);   /* 返回 0=成功 */

/* AD9361 关键寄存器地址 */
#define AD9361_REG_SPI_CONF        0x000
#define AD9361_REG_CLK_CTRL_1      0x009
#define AD9361_REG_CLK_CTRL_2      0x00A
#define AD9361_REG_PRODUCT_ID      0x037
#define AD9361_PRODUCT_ID_MASK     0xF8
#define AD9361_PRODUCT_ID_VAL      0x08   /* AD9361 = 0x0A & 0xF8 */

/* E310 GPIO 连接 */
#define E310_GPIO_AD9361_RESET     46
#define E310_GPIO_AD9361_EN_AGC    66

#endif /* AD9361_SPI_H */
