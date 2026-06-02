/*
 * ad9361.h — AD9361 RF 参数配置驱动
 *
 * 封装 PLL 计算、LO 频率设置、增益控制。
 * 依赖 ad9361_spi.h 的底层 SPI 读写。
 */
#ifndef AD9361_H
#define AD9361_H

#include "xil_types.h"
#include "../09-ad9361-init/ad9361_spi.h"

/* ── RF 参数结构体 ───────────────────────────── */
typedef struct {
    u64 rx_lo_hz;          /* RX LO 频率（Hz），70e6 ~ 6000e6 */
    u64 tx_lo_hz;          /* TX LO 频率（Hz）*/
    u32 sample_rate_hz;    /* 采样率（Hz），最大 61.44 MHz */
    u32 rx_bw_hz;          /* RX 射频带宽（Hz）*/
    u32 tx_bw_hz;          /* TX 射频带宽（Hz）*/
    int rx1_gain_db;       /* RX1 增益（dB），-3 ~ 71 */
    int rx2_gain_db;       /* RX2 增益（dB）*/
    int tx1_atten_mdb;     /* TX1 衰减（毫dB），0 ~ 89750 */
    u8  gain_mode;         /* 0=手动, 1=慢速AGC, 2=快速AGC */
    u8  two_rx_two_tx;     /* 0=1R1T, 1=2R2T */
} Ad9361Config;

/* ── 默认配置（433 MHz ISM，5 MHz 采样率）── */
#define AD9361_DEFAULT_CONFIG { \
    .rx_lo_hz        = 433000000ULL, \
    .tx_lo_hz        = 433000000ULL, \
    .sample_rate_hz  = 5000000,      \
    .rx_bw_hz        = 4000000,      \
    .tx_bw_hz        = 4000000,      \
    .rx1_gain_db     = 40,           \
    .rx2_gain_db     = 40,           \
    .tx1_atten_mdb   = 10000,        \
    .gain_mode       = 0,            \
    .two_rx_two_tx   = 0,            \
}

/* ── API ─────────────────────────────────────── */

/* 完整 AD9361 初始化（包括 PLL 上电，滤波器配置）*/
int ad9361_init(const Ad9361Config *cfg);

/* 仅设置 RX LO 频率 */
int ad9361_set_rx_lo(u64 freq_hz);

/* 仅设置 TX LO 频率 */
int ad9361_set_tx_lo(u64 freq_hz);

/* 设置 RX 增益（手动模式）*/
int ad9361_set_rx_gain(int ch, int gain_db);

/* 设置增益控制模式 */
int ad9361_set_gain_mode(u8 mode);

/* 读取 RSSI（接收信号强度，dBFS）*/
int ad9361_get_rssi_dbfs(int ch);

/* 打印当前 RF 参数 */
void ad9361_print_config(void);

/* E310 参考时钟（40 MHz TCXO）*/
#define AD9361_REF_CLK_HZ    40000000UL

/* AD9361 增益模式 */
#define AD9361_GAIN_MANUAL      0
#define AD9361_GAIN_SLOW_AGC    1
#define AD9361_GAIN_FAST_AGC    2

/* AD9361 频率限制 */
#define AD9361_FREQ_MIN_HZ   70000000ULL    /* 70 MHz */
#define AD9361_FREQ_MAX_HZ   6000000000ULL  /* 6 GHz */

#endif /* AD9361_H */
