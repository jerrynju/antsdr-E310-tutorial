/*
 * 步骤 10：AD9361 RF 参数配置
 *
 * 硬件：AD9361 完整（SPI + GPIO + RF 前端）
 *
 * 学习目标：
 *   1. 理解 AD9361 PLL 频率合成原理
 *   2. 掌握完整初始化流程
 *   3. 学会扫频（逐步改变 LO 频率）
 *   4. 理解频率→寄存器值的计算过程
 *
 * 本步骤演示 3 种配置场景：
 *   A. 433 MHz ISM 频段（无线遥控、LoRa）
 *   B. 915 MHz ISM 频段（LoRa, Sigfox）
 *   C. 2.4 GHz WiFi 频段
 */

#include "ad9361.h"
#include "xil_printf.h"
#include "xil_cache.h"
#include "sleep.h"

/* 从 ad9361_spi.h 中间接包含，需要链接 09 步骤的 .c 文件
 * 或将 ad9361_spi.c 复制到本步骤目录 */
#include "../09-ad9361-init/ad9361_spi.h"

/* ── 配置场景 ─────────────────────────────────── */
static const Ad9361Config config_433mhz = {
    .rx_lo_hz       = 433920000ULL,  /* 433.92 MHz ISM */
    .tx_lo_hz       = 433920000ULL,
    .sample_rate_hz = 5000000,       /* 5 MHz 采样率 */
    .rx_bw_hz       = 4000000,       /* 4 MHz 带宽 */
    .tx_bw_hz       = 4000000,
    .rx1_gain_db    = 40,
    .rx2_gain_db    = 40,
    .tx1_atten_mdb  = 10000,         /* 10 dB 衰减 */
    .gain_mode      = AD9361_GAIN_MANUAL,
    .two_rx_two_tx  = 0,             /* 1R1T */
};

static const Ad9361Config config_915mhz = {
    .rx_lo_hz       = 915000000ULL,
    .tx_lo_hz       = 915000000ULL,
    .sample_rate_hz = 10000000,
    .rx_bw_hz       = 8000000,
    .tx_bw_hz       = 8000000,
    .rx1_gain_db    = 35,
    .rx2_gain_db    = 35,
    .tx1_atten_mdb  = 10000,
    .gain_mode      = AD9361_GAIN_MANUAL,
    .two_rx_two_tx  = 0,
};

static const Ad9361Config config_2400mhz = {
    .rx_lo_hz       = 2412000000ULL, /* WiFi ch1 */
    .tx_lo_hz       = 2412000000ULL,
    .sample_rate_hz = 20000000,
    .rx_bw_hz       = 16000000,
    .tx_bw_hz       = 16000000,
    .rx1_gain_db    = 30,
    .rx2_gain_db    = 30,
    .tx1_atten_mdb  = 10000,
    .gain_mode      = AD9361_GAIN_MANUAL,
    .two_rx_two_tx  = 0,
};

/* ── 频率扫描演示（不重新初始化，只改 LO）── */
static void sweep_demo(u64 start_hz, u64 stop_hz, u64 step_hz)
{
    xil_printf("\r\n[频率扫描] %.0f MHz → %.0f MHz，步进 %.3f MHz\r\n",
               (double)start_hz/1e6,
               (double)stop_hz/1e6,
               (double)step_hz/1e6);

    for (u64 f = start_hz; f <= stop_hz; f += step_hz) {
        ad9361_set_rx_lo(f);
        sleep(0);  /* 小延时，让 PLL 锁定 */
        usleep(5000);
        int rssi = ad9361_get_rssi_dbfs(1);
        xil_printf("  f=%.3f MHz  RSSI=%d dBFS\r\n",
                   (double)f/1e6, rssi);
    }
}

/* ── 增益扫描演示 ── */
static void gain_sweep_demo(void)
{
    xil_printf("\r\n[增益扫描] 0 → 71 dB，步进 10 dB\r\n");
    for (int g = 0; g <= 71; g += 10) {
        ad9361_set_rx_gain(1, g);
        usleep(5000);
        int rssi = ad9361_get_rssi_dbfs(1);
        xil_printf("  增益=%3d dB  RSSI=%d dBFS\r\n", g, rssi);
    }
}

int main(void)
{
    Xil_ICacheEnable();
    Xil_DCacheEnable();

    xil_printf("\r\n================================================\r\n");
    xil_printf("  ANTSDR E310 裸机 - 步骤 10：RF 参数配置\r\n");
    xil_printf("================================================\r\n\r\n");

    /* ── 场景 A：433 MHz ── */
    xil_printf("===== 场景 A：433 MHz ISM =====\r\n");
    if (ad9361_init(&config_433mhz) != 0) {
        xil_printf("[错误] 初始化失败\r\n");
        while (1) {}
    }
    ad9361_print_config();
    sleep(1);

    /* ── 场景 B：915 MHz ── */
    xil_printf("\r\n===== 场景 B：915 MHz =====\r\n");
    /* 只改 LO 频率，不重新完整初始化 */
    ad9361_set_rx_lo(config_915mhz.rx_lo_hz);
    ad9361_set_tx_lo(config_915mhz.tx_lo_hz);
    ad9361_set_rx_gain(1, config_915mhz.rx1_gain_db);
    usleep(100000);
    ad9361_print_config();
    sleep(1);

    /* ── 场景 C：2.4 GHz ── */
    xil_printf("\r\n===== 场景 C：2.4 GHz =====\r\n");
    ad9361_set_rx_lo(config_2400mhz.rx_lo_hz);
    ad9361_set_tx_lo(config_2400mhz.tx_lo_hz);
    ad9361_set_rx_gain(1, config_2400mhz.rx1_gain_db);
    usleep(100000);
    ad9361_print_config();
    sleep(1);

    /* ── 频率扫描（ISM 433 MHz 附近）── */
    ad9361_set_rx_lo(433000000ULL);
    ad9361_set_rx_gain(1, 60);
    sweep_demo(430000000ULL, 436000000ULL, 1000000ULL);

    /* ── 增益扫描 ── */
    ad9361_set_rx_lo(433920000ULL);
    gain_sweep_demo();

    xil_printf("\r\n[完成] 步骤 10 执行完毕。\r\n");
    xil_printf("下一步：11-ad9361-iq-rx（IQ 数据接收）\r\n\r\n");

    while (1) {}
    return 0;
}
