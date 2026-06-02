/*
 * ANTSDR E310 — IQ 数据接收并保存到文件
 *
 * 功能：
 *   1. 配置 AD9361 RX 参数（频率、采样率、增益）
 *   2. 持续接收 IQ 数据并写入二进制文件
 *   3. 数据格式：交织的 int16_t，[I0, Q0, I1, Q1, ...]
 *
 * 编译：
 *   make           (使用 Makefile 交叉编译)
 *
 * 运行（E310 上）：
 *   ./iio_rx_file -f 433e6 -s 5e6 -n 1000000 -o rx_data.bin
 *   ./iio_rx_file --help
 *
 * 输出文件用 Python 分析：
 *   import numpy as np
 *   iq = np.fromfile('rx_data.bin', dtype=np.int16)
 *   samples = iq[0::2] + 1j * iq[1::2]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <math.h>
#include <getopt.h>
#include <iio.h>

/* ── 默认参数 ──────────────────────────────────────── */
#define DEFAULT_RX_FREQ_HZ    433000000LL
#define DEFAULT_SAMPLE_RATE   5000000LL
#define DEFAULT_BANDWIDTH     4000000LL
#define DEFAULT_GAIN_DB       40
#define DEFAULT_NUM_SAMPLES   1000000
#define DEFAULT_BUF_SIZE      1024       /* 每次 DMA 传输的采样数 */
#define DEFAULT_OUTPUT        "rx_data.bin"
/* ──────────────────────────────────────────────────── */

static volatile sig_atomic_t g_stop = 0;

static void sighandler(int sig)
{
    (void)sig;
    g_stop = 1;
}

static void usage(const char *prog)
{
    printf("用法: %s [选项]\n\n", prog);
    printf("选项:\n");
    printf("  -u <uri>       IIO 上下文 URI（默认：本地）\n");
    printf("                 示例：-u ip:192.168.1.10\n");
    printf("  -f <freq>      RX 中心频率 Hz（默认：%lld）\n", DEFAULT_RX_FREQ_HZ);
    printf("                 支持科学计数法：433e6\n");
    printf("  -s <rate>      采样率 Hz（默认：%lld）\n", DEFAULT_SAMPLE_RATE);
    printf("  -b <bw>        RF 带宽 Hz（默认：%lld）\n", DEFAULT_BANDWIDTH);
    printf("  -g <gain>      RX 增益 dB（默认：%d，-1 表示 AGC）\n", DEFAULT_GAIN_DB);
    printf("  -n <samples>   总采样数（默认：%d，0 表示持续）\n", DEFAULT_NUM_SAMPLES);
    printf("  -o <file>      输出文件（默认：%s）\n", DEFAULT_OUTPUT);
    printf("  -h             显示帮助\n");
}

/* 解析科学计数法频率字符串，如 "433e6" → 433000000 */
static long long parse_freq(const char *str)
{
    double val = strtod(str, NULL);
    return (long long)val;
}

int main(int argc, char *argv[])
{
    const char *uri          = NULL;
    long long   rx_freq      = DEFAULT_RX_FREQ_HZ;
    long long   sample_rate  = DEFAULT_SAMPLE_RATE;
    long long   bandwidth    = DEFAULT_BANDWIDTH;
    int         gain_db      = DEFAULT_GAIN_DB;
    long long   num_samples  = DEFAULT_NUM_SAMPLES;
    const char *output_file  = DEFAULT_OUTPUT;

    /* 解析命令行参数 */
    int opt;
    while ((opt = getopt(argc, argv, "u:f:s:b:g:n:o:h")) != -1) {
        switch (opt) {
        case 'u': uri         = optarg;             break;
        case 'f': rx_freq     = parse_freq(optarg); break;
        case 's': sample_rate = parse_freq(optarg); break;
        case 'b': bandwidth   = parse_freq(optarg); break;
        case 'g': gain_db     = atoi(optarg);       break;
        case 'n': num_samples = atoll(optarg);      break;
        case 'o': output_file = optarg;             break;
        case 'h': usage(argv[0]); return EXIT_SUCCESS;
        default:  usage(argv[0]); return EXIT_FAILURE;
        }
    }

    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);

    printf("ANTSDR E310 IQ 接收器\n");
    printf("  URI          : %s\n", uri ? uri : "local");
    printf("  RX 频率      : %.3f MHz\n", rx_freq / 1e6);
    printf("  采样率       : %.3f MHz\n", sample_rate / 1e6);
    printf("  带宽         : %.3f MHz\n", bandwidth / 1e6);
    printf("  增益         : %s\n", gain_db < 0 ? "AGC（自动）" :
                                    (char[16]){}, gain_db < 0 ? "" :
                                    (snprintf((char[16]){}, 16, "%d dB", gain_db), (char[16]){}));
    if (gain_db >= 0)
        printf("  增益         : %d dB\n", gain_db);
    else
        printf("  增益         : AGC（自动）\n");
    printf("  采样总数     : %s\n", num_samples == 0 ? "持续" :
           (char[32]){(snprintf((char[32]){}, 32, "%lld", num_samples), 0)});
    printf("  输出文件     : %s\n\n", output_file);

    /* 建立 IIO 上下文 */
    struct iio_context *ctx = uri ?
        iio_create_network_context(uri) : iio_create_local_context();
    if (!ctx) {
        fprintf(stderr, "IIO 上下文创建失败: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    /* 获取 AD9361 控制设备 */
    struct iio_device *phy = iio_context_find_device(ctx, "ad9361-phy");
    struct iio_device *rx  = iio_context_find_device(ctx, "cf-ad9361-lpc");
    if (!phy || !rx) {
        fprintf(stderr, "未找到 AD9361 设备\n");
        iio_context_destroy(ctx);
        return EXIT_FAILURE;
    }

    /* 配置 RF 参数 */
    struct iio_channel *rx_lo = iio_device_find_channel(phy, "altvoltage0", true);
    struct iio_channel *rx_ch = iio_device_find_channel(phy, "voltage0", false);

    iio_channel_attr_write_longlong(rx_lo, "frequency",        rx_freq);
    iio_channel_attr_write_longlong(rx_ch, "sampling_frequency", sample_rate);
    iio_channel_attr_write_longlong(rx_ch, "rf_bandwidth",     bandwidth);

    if (gain_db < 0) {
        iio_channel_attr_write(rx_ch, "gain_control_mode", "slow_attack");
    } else {
        iio_channel_attr_write(rx_ch, "gain_control_mode", "manual");
        iio_channel_attr_write_longlong(rx_ch, "hardwaregain", gain_db);
    }

    /* 启用 RX IQ 通道（I=voltage0, Q=voltage1） */
    struct iio_channel *rx_i = iio_device_find_channel(rx, "voltage0", false);
    struct iio_channel *rx_q = iio_device_find_channel(rx, "voltage1", false);
    if (!rx_i || !rx_q) {
        fprintf(stderr, "RX IQ 通道获取失败\n");
        iio_context_destroy(ctx);
        return EXIT_FAILURE;
    }
    iio_channel_enable(rx_i);
    iio_channel_enable(rx_q);

    /* 创建 DMA 缓冲区 */
    struct iio_buffer *buf = iio_device_create_buffer(rx, DEFAULT_BUF_SIZE, false);
    if (!buf) {
        fprintf(stderr, "缓冲区创建失败: %s\n", strerror(errno));
        iio_context_destroy(ctx);
        return EXIT_FAILURE;
    }

    /* 打开输出文件 */
    FILE *fp = fopen(output_file, "wb");
    if (!fp) {
        fprintf(stderr, "无法打开输出文件 %s: %s\n", output_file, strerror(errno));
        iio_buffer_destroy(buf);
        iio_context_destroy(ctx);
        return EXIT_FAILURE;
    }

    printf("[开始接收] 按 Ctrl+C 停止\n");

    long long total = 0;
    long long target = (num_samples == 0) ? LLONG_MAX : num_samples;

    while (!g_stop && total < target) {
        ssize_t nbytes = iio_buffer_refill(buf);
        if (nbytes < 0) {
            fprintf(stderr, "缓冲区刷新失败: %s\n", strerror(-nbytes));
            break;
        }

        /* 获取数据指针，数据为交织的 int16_t [I, Q, I, Q, ...] */
        const int16_t *start = iio_buffer_start(buf);
        const int16_t *end   = iio_buffer_end(buf);
        size_t n_words = end - start;  /* I+Q 各 1 个 int16_t */

        fwrite(start, sizeof(int16_t), n_words, fp);
        total += n_words / 2;  /* 每对 I/Q 算一个采样 */

        if (total % (sample_rate) < DEFAULT_BUF_SIZE) {
            printf("\r  已接收: %lld 个采样 (%.2f MB)",
                   total, total * 4.0 / (1024 * 1024));
            fflush(stdout);
        }
    }

    printf("\n[完成] 共接收 %lld 个采样 (%.2f MB)\n",
           total, total * 4.0 / (1024 * 1024));
    printf("  数据已保存至: %s\n", output_file);
    printf("\nPython 加载方法:\n");
    printf("  import numpy as np\n");
    printf("  iq = np.fromfile('%s', dtype=np.int16)\n", output_file);
    printf("  samples = iq[0::2] + 1j * iq[1::2]  # 复数 IQ\n");

    fclose(fp);
    iio_buffer_destroy(buf);
    iio_context_destroy(ctx);
    return EXIT_SUCCESS;
}
