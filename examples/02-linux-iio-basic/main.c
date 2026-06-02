/*
 * ANTSDR E310 — libiio 基础控制示例
 *
 * 功能：连接 AD9361，配置 RX/TX 频率、采样率、带宽、增益，
 *       打印当前所有 RF 参数。
 *
 * 编译（在主机端交叉编译）：
 *   arm-linux-gnueabihf-gcc -o iio_basic main.c -liio -lm
 *
 * 运行（在 E310 Linux 上）：
 *   ./iio_basic                          # 本地运行
 *   ./iio_basic 192.168.1.10             # 通过网络控制
 *
 * 依赖：libiio（设备端已内置，主机端 apt install libiio-dev）
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <iio.h>

/* ── RF 配置参数（按需修改） ─────────────────────── */
#define RX_LO_FREQ_HZ     433000000LL   /* RX 中心频率：433 MHz */
#define TX_LO_FREQ_HZ     433000000LL   /* TX 中心频率：433 MHz */
#define SAMPLE_RATE_HZ      5000000LL   /* 采样率：5 MHz */
#define RF_BANDWIDTH_HZ     4000000LL   /* RF 带宽：4 MHz */
#define RX_GAIN_DB               30     /* RX 增益：30 dB（manual 模式）*/
/* ──────────────────────────────────────────────── */

/* 错误检查宏：属性写失败时打印警告继续 */
#define CHK_ATTR(ret, name) \
    do { if ((ret) < 0) \
        fprintf(stderr, "  [WARN] 写 '%s' 失败: %s\n", \
                (name), strerror(-(ret))); \
    } while (0)

static struct iio_context *create_context(const char *uri)
{
    struct iio_context *ctx;
    if (uri == NULL || strcmp(uri, "local") == 0)
        ctx = iio_create_local_context();
    else
        ctx = iio_create_network_context(uri);

    if (!ctx) {
        fprintf(stderr, "无法创建 IIO 上下文 (uri=%s): %s\n",
                uri ? uri : "local", strerror(errno));
        return NULL;
    }
    return ctx;
}

/* 读取并打印一个通道属性（long long 类型） */
static void print_attr_ll(struct iio_channel *ch, const char *attr,
                           const char *unit)
{
    long long val;
    int ret = iio_channel_attr_read_longlong(ch, attr, &val);
    if (ret == 0)
        printf("  %-30s = %lld %s\n", attr, val, unit ? unit : "");
    else
        printf("  %-30s = (读取失败: %s)\n", attr, strerror(-ret));
}

/* 读取并打印一个通道属性（字符串类型） */
static void print_attr_str(struct iio_channel *ch, const char *attr)
{
    char buf[128];
    int ret = iio_channel_attr_read(ch, attr, buf, sizeof(buf));
    if (ret > 0)
        printf("  %-30s = %s\n", attr, buf);
    else
        printf("  %-30s = (读取失败)\n", attr);
}

static int configure_ad9361(struct iio_context *ctx)
{
    /* 1. 获取控制设备（RF 参数配置） */
    struct iio_device *phy = iio_context_find_device(ctx, "ad9361-phy");
    if (!phy) {
        fprintf(stderr, "未找到 ad9361-phy 设备\n");
        return -ENODEV;
    }

    /* 2. 获取通道 */
    /* RX 物理通道（voltage0 = RX1）*/
    struct iio_channel *rx_ch = iio_device_find_channel(phy, "voltage0", false);
    /* TX 物理通道（voltage0 = TX1，方向为 output）*/
    struct iio_channel *tx_ch = iio_device_find_channel(phy, "voltage0", true);
    /* RX LO（本地振荡器，altvoltage0）*/
    struct iio_channel *rx_lo = iio_device_find_channel(phy, "altvoltage0", true);
    /* TX LO（altvoltage1）*/
    struct iio_channel *tx_lo = iio_device_find_channel(phy, "altvoltage1", true);

    if (!rx_ch || !tx_ch || !rx_lo || !tx_lo) {
        fprintf(stderr, "通道获取失败（确认 2R2T 模式？）\n");
        return -ENODEV;
    }

    int ret;

    printf("\n[配置 RX 参数]\n");
    ret = iio_channel_attr_write_longlong(rx_lo, "frequency", RX_LO_FREQ_HZ);
    CHK_ATTR(ret, "rx_lo/frequency");
    printf("  RX LO 频率     = %lld Hz (%.3f MHz)\n",
           RX_LO_FREQ_HZ, RX_LO_FREQ_HZ / 1e6);

    ret = iio_channel_attr_write_longlong(rx_ch, "sampling_frequency", SAMPLE_RATE_HZ);
    CHK_ATTR(ret, "rx/sampling_frequency");

    ret = iio_channel_attr_write_longlong(rx_ch, "rf_bandwidth", RF_BANDWIDTH_HZ);
    CHK_ATTR(ret, "rx/rf_bandwidth");

    /* 设置 AGC 模式为手动，再设置增益 */
    ret = iio_channel_attr_write(rx_ch, "gain_control_mode", "manual");
    CHK_ATTR(ret, "rx/gain_control_mode");

    ret = iio_channel_attr_write_longlong(rx_ch, "hardwaregain", RX_GAIN_DB);
    CHK_ATTR(ret, "rx/hardwaregain");

    printf("\n[配置 TX 参数]\n");
    ret = iio_channel_attr_write_longlong(tx_lo, "frequency", TX_LO_FREQ_HZ);
    CHK_ATTR(ret, "tx_lo/frequency");
    printf("  TX LO 频率     = %lld Hz (%.3f MHz)\n",
           TX_LO_FREQ_HZ, TX_LO_FREQ_HZ / 1e6);

    ret = iio_channel_attr_write_longlong(tx_ch, "sampling_frequency", SAMPLE_RATE_HZ);
    CHK_ATTR(ret, "tx/sampling_frequency");

    ret = iio_channel_attr_write_longlong(tx_ch, "rf_bandwidth", RF_BANDWIDTH_HZ);
    CHK_ATTR(ret, "tx/rf_bandwidth");

    /* TX 衰减：-10 dB（libiio 中 TX 增益为负值，单位 mdB） */
    ret = iio_channel_attr_write_longlong(tx_ch, "hardwaregain", -10000);
    CHK_ATTR(ret, "tx/hardwaregain");

    return 0;
}

static void print_current_params(struct iio_context *ctx)
{
    struct iio_device *phy = iio_context_find_device(ctx, "ad9361-phy");
    if (!phy) return;

    struct iio_channel *rx_ch = iio_device_find_channel(phy, "voltage0", false);
    struct iio_channel *rx_lo = iio_device_find_channel(phy, "altvoltage0", true);
    struct iio_channel *tx_lo = iio_device_find_channel(phy, "altvoltage1", true);

    printf("\n====== 当前 AD9361 配置 ======\n");

    printf("\n[RX LO]\n");
    print_attr_ll(rx_lo, "frequency", "Hz");

    printf("\n[RX 通道 (voltage0)]\n");
    print_attr_ll(rx_ch, "sampling_frequency", "Hz");
    print_attr_ll(rx_ch, "rf_bandwidth", "Hz");
    print_attr_str(rx_ch, "gain_control_mode");
    print_attr_ll(rx_ch, "hardwaregain", "dB");
    print_attr_ll(rx_ch, "rssi", "dB");  /* 接收信号强度 */

    printf("\n[TX LO]\n");
    print_attr_ll(tx_lo, "frequency", "Hz");

    printf("==============================\n");
}

int main(int argc, char *argv[])
{
    const char *uri = (argc > 1) ? argv[1] : NULL;

    printf("ANTSDR E310 libiio 基础控制示例\n");
    printf("连接目标: %s\n", uri ? uri : "本地 (local)");

    struct iio_context *ctx = create_context(uri);
    if (!ctx) return EXIT_FAILURE;

    /* 打印上下文信息 */
    printf("\n[IIO 上下文]\n");
    printf("  描述: %s\n", iio_context_get_description(ctx));
    printf("  设备数: %u\n", iio_context_get_devices_count(ctx));

    /* 配置 RF 参数 */
    int ret = configure_ad9361(ctx);
    if (ret < 0) {
        iio_context_destroy(ctx);
        return EXIT_FAILURE;
    }

    /* 打印当前配置 */
    print_current_params(ctx);

    iio_context_destroy(ctx);
    printf("\n[完成] 程序正常退出\n");
    return EXIT_SUCCESS;
}
