/*
 * ANTSDR E310 — 简单频谱扫描器
 *
 * 功能：
 *   在指定频段内步进扫描，每个频点采集少量 IQ 数据，
 *   计算功率（dBm 估算），在终端打印 ASCII 频谱图。
 *
 * 编译：make
 *
 * 运行示例（扫描 400–500 MHz，步进 1 MHz）：
 *   ./iio_spectrum_scan -u ip:192.168.1.10 \
 *                       --start 400e6 --stop 500e6 --step 1e6
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <math.h>
#include <getopt.h>
#include <unistd.h>
#include <iio.h>

/* ── 默认扫描参数 ─────────────────────────────────── */
#define DEFAULT_START_HZ   400000000LL   /* 起始频率 400 MHz */
#define DEFAULT_STOP_HZ    500000000LL   /* 终止频率 500 MHz */
#define DEFAULT_STEP_HZ      1000000LL   /* 步进 1 MHz */
#define DEFAULT_SAMPLE_RATE  5000000LL   /* 采样率 5 MHz */
#define DEFAULT_BUF_SAMPLES     4096     /* 每个频点采集的采样数 */
#define SPECTRUM_WIDTH            60     /* ASCII 图宽度（字符数）*/
/* ──────────────────────────────────────────────────── */

static volatile sig_atomic_t g_stop = 0;
static void sighandler(int s) { (void)s; g_stop = 1; }

static long long parse_freq(const char *s) { return (long long)strtod(s, NULL); }

/* 计算 IQ 缓冲区的平均功率（线性，归一化到 full-scale） */
static double compute_power_db(const int16_t *iq, int n_samples)
{
    double power = 0.0;
    for (int i = 0; i < n_samples; i++) {
        double I = iq[2 * i]     / 32768.0;
        double Q = iq[2 * i + 1] / 32768.0;
        power += I * I + Q * Q;
    }
    power /= n_samples;
    /* 转换为 dBFS（dB relative to full scale） */
    return (power > 0) ? 10.0 * log10(power) : -120.0;
}

/* 打印一行 ASCII 频谱 */
static void print_bar(double freq_mhz, double power_db,
                      double min_db, double max_db)
{
    int bar_len = (int)((power_db - min_db) / (max_db - min_db) * SPECTRUM_WIDTH);
    if (bar_len < 0) bar_len = 0;
    if (bar_len > SPECTRUM_WIDTH) bar_len = SPECTRUM_WIDTH;

    printf("%7.1f MHz |", freq_mhz);
    for (int i = 0; i < bar_len; i++)
        putchar(i == bar_len - 1 ? '>' : '=');
    for (int i = bar_len; i < SPECTRUM_WIDTH; i++)
        putchar(' ');
    printf("| %5.1f dBFS\n", power_db);
}

static void usage(const char *prog)
{
    printf("用法: %s [选项]\n\n", prog);
    printf("  -u <uri>         IIO URI（默认：local）\n");
    printf("  --start <freq>   起始频率 Hz（默认：400e6）\n");
    printf("  --stop  <freq>   终止频率 Hz（默认：500e6）\n");
    printf("  --step  <freq>   步进频率 Hz（默认：1e6）\n");
    printf("  -s <rate>        采样率 Hz（默认：5e6）\n");
    printf("  -g <gain>        增益 dB（默认：50）\n");
    printf("  -r <repeats>     扫描轮数（默认：1，0 表示持续）\n");
    printf("  -h               帮助\n");
}

int main(int argc, char *argv[])
{
    const char *uri      = NULL;
    long long start_hz   = DEFAULT_START_HZ;
    long long stop_hz    = DEFAULT_STOP_HZ;
    long long step_hz    = DEFAULT_STEP_HZ;
    long long samp_rate  = DEFAULT_SAMPLE_RATE;
    int       gain_db    = 50;
    int       repeats    = 1;

    static struct option long_opts[] = {
        {"start", required_argument, 0, 'S'},
        {"stop",  required_argument, 0, 'E'},
        {"step",  required_argument, 0, 'T'},
        {0, 0, 0, 0}
    };

    int opt, idx;
    while ((opt = getopt_long(argc, argv, "u:s:g:r:h", long_opts, &idx)) != -1) {
        switch (opt) {
        case 'u': uri       = optarg;             break;
        case 'S': start_hz  = parse_freq(optarg); break;
        case 'E': stop_hz   = parse_freq(optarg); break;
        case 'T': step_hz   = parse_freq(optarg); break;
        case 's': samp_rate = parse_freq(optarg); break;
        case 'g': gain_db   = atoi(optarg);       break;
        case 'r': repeats   = atoi(optarg);       break;
        case 'h': usage(argv[0]); return EXIT_SUCCESS;
        default:  usage(argv[0]); return EXIT_FAILURE;
        }
    }

    signal(SIGINT, sighandler);

    printf("ANTSDR E310 频谱扫描器\n");
    printf("  扫描范围: %.1f – %.1f MHz，步进 %.3f MHz\n",
           start_hz / 1e6, stop_hz / 1e6, step_hz / 1e6);
    printf("  采样率:   %.1f MHz\n\n", samp_rate / 1e6);

    /* 建立 IIO 上下文 */
    struct iio_context *ctx = uri ?
        iio_create_network_context(uri) : iio_create_local_context();
    if (!ctx) {
        fprintf(stderr, "IIO 上下文创建失败: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    struct iio_device *phy = iio_context_find_device(ctx, "ad9361-phy");
    struct iio_device *rx  = iio_context_find_device(ctx, "cf-ad9361-lpc");
    if (!phy || !rx) {
        fprintf(stderr, "未找到 AD9361\n");
        iio_context_destroy(ctx);
        return EXIT_FAILURE;
    }

    struct iio_channel *rx_lo = iio_device_find_channel(phy, "altvoltage0", true);
    struct iio_channel *rx_ch = iio_device_find_channel(phy, "voltage0",    false);
    struct iio_channel *rx_i  = iio_device_find_channel(rx,  "voltage0",    false);
    struct iio_channel *rx_q  = iio_device_find_channel(rx,  "voltage1",    false);

    /* 配置固定参数 */
    iio_channel_attr_write_longlong(rx_ch, "sampling_frequency", samp_rate);
    iio_channel_attr_write_longlong(rx_ch, "rf_bandwidth", samp_rate * 8 / 10);
    iio_channel_attr_write(rx_ch, "gain_control_mode", "manual");
    iio_channel_attr_write_longlong(rx_ch, "hardwaregain", gain_db);

    iio_channel_enable(rx_i);
    iio_channel_enable(rx_q);

    struct iio_buffer *buf = iio_device_create_buffer(rx, DEFAULT_BUF_SAMPLES, false);
    if (!buf) {
        fprintf(stderr, "缓冲区创建失败\n");
        iio_context_destroy(ctx);
        return EXIT_FAILURE;
    }

    int16_t *iq_buf = malloc(DEFAULT_BUF_SAMPLES * 2 * sizeof(int16_t));
    if (!iq_buf) {
        fprintf(stderr, "内存分配失败\n");
        iio_buffer_destroy(buf);
        iio_context_destroy(ctx);
        return EXIT_FAILURE;
    }

    int scan_count = 0;
    while (!g_stop && (repeats == 0 || scan_count < repeats)) {
        int num_points = (int)((stop_hz - start_hz) / step_hz) + 1;
        double *powers = malloc(num_points * sizeof(double));

        /* ── 扫描一轮 ── */
        for (int p = 0; p < num_points && !g_stop; p++) {
            long long freq = start_hz + (long long)p * step_hz;
            iio_channel_attr_write_longlong(rx_lo, "frequency", freq);

            /* 跳过 1 个缓冲区（让 AGC 和 LO 稳定） */
            iio_buffer_refill(buf);

            /* 正式采集 */
            ssize_t nb = iio_buffer_refill(buf);
            if (nb < 0) { powers[p] = -120.0; continue; }

            memcpy(iq_buf, iio_buffer_start(buf),
                   DEFAULT_BUF_SAMPLES * 2 * sizeof(int16_t));
            powers[p] = compute_power_db(iq_buf, DEFAULT_BUF_SAMPLES);
        }

        /* ── 打印频谱 ── */
        printf("\n第 %d 次扫描  [按 Ctrl+C 退出]\n", scan_count + 1);
        printf("        频率   |%-*s| 功率\n", SPECTRUM_WIDTH, " 频谱 ");
        printf("---------------+");
        for (int i = 0; i < SPECTRUM_WIDTH; i++) putchar('-');
        printf("+-------\n");

        double min_db = -80.0, max_db = 0.0;
        for (int p = 0; p < num_points; p++) {
            double freq_mhz = (start_hz + (long long)p * step_hz) / 1e6;
            print_bar(freq_mhz, powers[p], min_db, max_db);
        }

        free(powers);
        scan_count++;

        if (!g_stop && (repeats == 0 || scan_count < repeats))
            sleep(1);  /* 连续扫描间隔 */
    }

    free(iq_buf);
    iio_buffer_destroy(buf);
    iio_context_destroy(ctx);
    printf("\n[退出]\n");
    return EXIT_SUCCESS;
}
