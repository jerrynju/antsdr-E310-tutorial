/*
 * ad9361.c — AD9361 RF 参数配置实现
 *
 * 核心功能：PLL 频率计算和寄存器编程。
 *
 * AD9361 RX/TX PLL 结构：
 *   参考时钟（40 MHz）
 *       ↓ 整数/分数 N 分频器（INT.FRAC）
 *   VCO（2.4 GHz ~ 6 GHz）
 *       ↓ 输出分频器 D（2/4/8/16/32/64）
 *   LO 输出频率 = VCO / D
 *
 * 频率计算：
 *   f_lo = f_ref × (INT + FRAC/2^23) / D
 *   → f_vco = f_lo × D
 *   → INT  = floor(f_vco / f_ref)
 *   → FRAC = (f_vco / f_ref - INT) × 2^23
 */

#include "ad9361.h"
#include "xil_printf.h"
#include "sleep.h"
#include <string.h>

#define FRAC_BITS     23
#define FRAC_MODULUS  (1 << FRAC_BITS)  /* 2^23 = 8388608 */

/* VCO 频率范围（Hz）*/
#define VCO_MIN_HZ   2400000000ULL
#define VCO_MAX_HZ   6000000000ULL

/* PLL 输出分频器可选值（从小到大）*/
static const u8 vco_dividers[] = {2, 4, 8, 16, 32, 64};

/*
 * 计算 PLL 寄存器值
 * @f_lo:  目标 LO 频率（Hz）
 * @f_ref: 参考时钟（Hz，E310 = 40 MHz）
 * @out:   输出：{int_val, frac_val, divider_reg}
 */
typedef struct {
    u16 int_val;   /* 整数部分，10 bit */
    u32 frac_val;  /* 分数部分，23 bit */
    u8  div_reg;   /* 分频寄存器值（0~5 对应 /2~/64）*/
    u8  div_val;   /* 实际分频数 */
} PllParams;

static int calc_pll_params(u64 f_lo, u32 f_ref, PllParams *p)
{
    if (f_lo < AD9361_FREQ_MIN_HZ || f_lo > AD9361_FREQ_MAX_HZ) {
        xil_printf("[PLL] 频率 %llu Hz 超出范围 [70M, 6G]\r\n", f_lo);
        return -1;
    }

    /* 寻找合适的 VCO 分频器，使 VCO 在 [2.4G, 6G] 范围内 */
    for (u8 i = 0; i < sizeof(vco_dividers); i++) {
        u8  div = vco_dividers[i];
        u64 vco = f_lo * div;
        if (vco >= VCO_MIN_HZ && vco <= VCO_MAX_HZ) {
            p->div_reg = i;
            p->div_val = div;

            /* INT = floor(vco / fref) */
            p->int_val = (u16)(vco / f_ref);

            /* FRAC = (vco - INT * fref) * 2^23 / fref */
            u64 remainder = vco - (u64)p->int_val * f_ref;
            p->frac_val = (u32)((remainder * FRAC_MODULUS) / f_ref);

            return 0;
        }
    }
    xil_printf("[PLL] 找不到合适的 VCO 分频器（f_lo=%llu Hz）\r\n", f_lo);
    return -1;
}

/* 写 RX PLL 寄存器 */
static int write_rx_pll(u64 f_lo)
{
    PllParams p;
    if (calc_pll_params(f_lo, AD9361_REF_CLK_HZ, &p) != 0) return -1;

    xil_printf("[RX PLL] f_lo=%.3f MHz, VCO/D=%d, INT=%d, FRAC=%lu\r\n",
               (double)f_lo/1e6, p.div_val, p.int_val, (u32)p.frac_val);

    /* 写分数分频值（23 bit，分3字节）*/
    ad9361_reg_write(0x27A, (u8)((p.frac_val >> 16) & 0x7F));  /* [22:16] */
    ad9361_reg_write(0x27B, (u8)((p.frac_val >> 8) & 0xFF));   /* [15:8] */
    ad9361_reg_write(0x27C, (u8)(p.frac_val & 0xFF));           /* [7:0] */

    /* 写整数分频值（10 bit，分2字节）*/
    ad9361_reg_write(0x27D, (u8)((p.int_val >> 8) & 0x03));    /* [9:8] */
    ad9361_reg_write(0x27E, (u8)(p.int_val & 0xFF));             /* [7:0] */

    /* 写输出分频器 */
    ad9361_reg_write(0x27F, p.div_reg & 0x07);

    /* 校准 PLL：写 VCO calibration 触发位 */
    u8 tmp = ad9361_reg_read(0x261);
    ad9361_reg_write(0x261, tmp | 0x04);  /* VCO Cal Trigger */
    usleep(1000);

    return 0;
}

/* 写 TX PLL 寄存器（TX PLL 寄存器地址 = RX PLL + 0x40）*/
static int write_tx_pll(u64 f_lo)
{
    PllParams p;
    if (calc_pll_params(f_lo, AD9361_REF_CLK_HZ, &p) != 0) return -1;

    xil_printf("[TX PLL] f_lo=%.3f MHz, VCO/D=%d, INT=%d, FRAC=%lu\r\n",
               (double)f_lo/1e6, p.div_val, p.int_val, (u32)p.frac_val);

    ad9361_reg_write(0x2BA, (u8)((p.frac_val >> 16) & 0x7F));
    ad9361_reg_write(0x2BB, (u8)((p.frac_val >> 8) & 0xFF));
    ad9361_reg_write(0x2BC, (u8)(p.frac_val & 0xFF));
    ad9361_reg_write(0x2BD, (u8)((p.int_val >> 8) & 0x03));
    ad9361_reg_write(0x2BE, (u8)(p.int_val & 0xFF));
    ad9361_reg_write(0x2BF, p.div_reg & 0x07);

    u8 tmp = ad9361_reg_read(0x2A1);
    ad9361_reg_write(0x2A1, tmp | 0x04);
    usleep(1000);

    return 0;
}

int ad9361_set_rx_lo(u64 freq_hz)
{
    return write_rx_pll(freq_hz);
}

int ad9361_set_tx_lo(u64 freq_hz)
{
    return write_tx_pll(freq_hz);
}

int ad9361_set_rx_gain(int ch, int gain_db)
{
    /* AD9361 增益范围：-3 ~ 71 dB（取决于频段）*/
    if (gain_db < -3) gain_db = -3;
    if (gain_db > 71) gain_db = 71;

    /* 裁剪到 7 位，写寄存器 */
    u8 gain_code = (u8)(gain_db + 3) & 0x7F;

    if (ch == 1) {
        ad9361_reg_write(0x109, gain_code);  /* RX1 手动增益 */
    } else if (ch == 2) {
        ad9361_reg_write(0x10C, gain_code);  /* RX2 手动增益 */
    }

    xil_printf("[RX%d 增益] %d dB → 寄存器 0x%02X\r\n", ch, gain_db, gain_code);
    return 0;
}

int ad9361_set_gain_mode(u8 mode)
{
    /* 0x0FA: 增益控制模式
     * bit[1:0]: 00=MGC（手动）, 01=FASTAGC, 10=SLOWAGC */
    u8 val = mode & 0x03;
    ad9361_reg_write(0x0FA, val);
    const char *modes[] = {"手动", "快速AGC", "慢速AGC"};
    xil_printf("[增益模式] %s\r\n", mode < 3 ? modes[mode] : "未知");
    return 0;
}

int ad9361_get_rssi_dbfs(int ch)
{
    /* RSSI 寄存器：2字节，单位 0.25 dB */
    u8 hi, lo;
    if (ch == 1) {
        hi = ad9361_reg_read(0x1A7);
        lo = ad9361_reg_read(0x1A8);
    } else {
        hi = ad9361_reg_read(0x1A9);
        lo = ad9361_reg_read(0x1AA);
    }
    int rssi_quarter_db = (int)((hi << 2) | (lo & 0x03));
    return -(rssi_quarter_db / 4);  /* 转换为 dBFS（负值）*/
}

/*
 * AD9361 完整初始化序列
 * 基于 ADI no-OS 库的精简版，适用于 E310 硬件
 */
int ad9361_init(const Ad9361Config *cfg)
{
    xil_printf("\r\n[AD9361 完整初始化]\r\n");

    /* 1. SPI 和 GPIO */
    if (ad9361_spi_init() != 0) return -1;

    /* 2. 硬件复位 */
    ad9361_hw_reset();

    /* 3. SPI 软复位 */
    ad9361_reg_write(AD9361_REG_SPI_CONF, 0x80);
    usleep(200);
    ad9361_reg_write(AD9361_REG_SPI_CONF, 0x00);
    usleep(200);

    /* 4. 验证 ID */
    if (ad9361_check_id() != 0) return -1;

    /* 5. 写基础寄存器（时钟、参考频率）*/
    xil_printf("  [5] 配置参考时钟 %lu Hz\r\n", (u32)AD9361_REF_CLK_HZ);
    ad9361_reg_write(0x009, 0x17);   /* REF CLK 40 MHz scaler */
    ad9361_reg_write(0x00A, 0x00);
    ad9361_reg_write(0x00B, 0x00);

    /* 6. 使能 RX/TX PLL 上电 */
    xil_printf("  [6] 使能 PLL 上电\r\n");
    ad9361_reg_write(0x250, 0x0D);   /* RX Synth Power On */
    ad9361_reg_write(0x290, 0x0D);   /* TX Synth Power On */
    usleep(1000);

    /* 7. 设置 RX LO 频率 */
    xil_printf("  [7] RX LO = %.3f MHz\r\n", (double)cfg->rx_lo_hz / 1e6);
    if (write_rx_pll(cfg->rx_lo_hz) != 0) return -1;

    /* 8. 设置 TX LO 频率 */
    xil_printf("  [8] TX LO = %.3f MHz\r\n", (double)cfg->tx_lo_hz / 1e6);
    if (write_tx_pll(cfg->tx_lo_hz) != 0) return -1;

    /* 9. 设置增益模式和 RX 增益 */
    xil_printf("  [9] RX1 增益 = %d dB，模式 = %d\r\n",
               cfg->rx1_gain_db, cfg->gain_mode);
    ad9361_set_gain_mode(cfg->gain_mode);
    ad9361_set_rx_gain(1, cfg->rx1_gain_db);
    if (cfg->two_rx_two_tx)
        ad9361_set_rx_gain(2, cfg->rx2_gain_db);

    /* 10. 使能 RX/TX 路径 */
    xil_printf("  [10] 使能 RX 路径\r\n");
    ad9361_reg_write(0x014, cfg->two_rx_two_tx ? 0x03 : 0x01); /* RX 使能 */
    ad9361_reg_write(0x015, cfg->two_rx_two_tx ? 0x03 : 0x01); /* TX 使能 */

    xil_printf("[AD9361] 初始化完成 ✓\r\n");
    return 0;
}

void ad9361_print_config(void)
{
    xil_printf("\r\n[AD9361 当前配置]\r\n");

    /* 读 RX PLL 整数和分数部分 */
    u16 rx_int  = ((u16)(ad9361_reg_read(0x27D) & 0x03) << 8)
                  | ad9361_reg_read(0x27E);
    u32 rx_frac = ((u32)(ad9361_reg_read(0x27A) & 0x7F) << 16)
                  | ((u32)ad9361_reg_read(0x27B) << 8)
                  | ad9361_reg_read(0x27C);
    u8  rx_div  = vco_dividers[ad9361_reg_read(0x27F) & 0x07];

    /* 反推频率：f_lo = f_ref * (INT + FRAC/2^23) / D */
    u64 rx_lo = (u64)AD9361_REF_CLK_HZ *
                ((u64)rx_int * FRAC_MODULUS + rx_frac) /
                ((u64)rx_div * FRAC_MODULUS);

    xil_printf("  RX LO        : ~%llu Hz (%.3f MHz)\r\n",
               rx_lo, (double)rx_lo / 1e6);
    xil_printf("  RX PLL INT   : %d\r\n", rx_int);
    xil_printf("  RX PLL FRAC  : %lu\r\n", (u32)rx_frac);
    xil_printf("  RX VCO Div   : /%d\r\n", rx_div);

    int rssi = ad9361_get_rssi_dbfs(1);
    xil_printf("  RX1 RSSI     : %d dBFS\r\n", rssi);
}
