/*
 * 步骤 05：DDR3 内存测试与 Cache 管理
 *
 * 硬件：DDR3 SDRAM（512 MB，0x00000000 - 0x1FFFFFFF）
 *
 * 学习目标：
 *   1. 理解 Zynq 内存映射（OCM vs DDR vs Device）
 *   2. 掌握 Cache 管理 API（Flush, Invalidate）
 *   3. 理解为什么 DMA 之前必须 Flush D-Cache
 *   4. 内存屏障（DSB, DMB, ISB）的作用
 *   5. 为后续 DMA 操作打好基础
 *
 * Cache 与 DMA 的关系（关键知识点）：
 *
 *   CPU 写数据 → 先写 D-Cache（Write-Back 模式）
 *                 不立即写到 DDR
 *                 ↓ 必须调用 Xil_DCacheFlushRange()
 *   DMA 读 DDR → 才能读到最新数据
 *
 *   DMA 写 DDR → 写入 DDR
 *                 CPU 的 D-Cache 还存着旧数据
 *                 ↓ 必须调用 Xil_DCacheInvalidateRange()
 *   CPU 读数据 → 才能读到 DMA 写的新数据
 */

#include "xparameters.h"
#include "xil_printf.h"
#include "xil_cache.h"
#include "xil_io.h"
#include "sleep.h"

/* 测试缓冲区（位于 DDR，避开代码段）*/
#define TEST_BUF_ADDR    0x01000000   /* 16 MB 处开始 */
#define TEST_BUF_SIZE    (1 * 1024 * 1024)  /* 1 MB */
#define TEST_PATTERN_A   0xA5A5A5A5
#define TEST_PATTERN_B   0x5A5A5A5A
#define TEST_PATTERN_C   0xDEADBEEF

/* 测试结果统计 */
typedef struct {
    u32 total_words;
    u32 error_count;
    u32 first_error_addr;
    u32 expected;
    u32 actual;
} TestResult;

/* ── 模式填充测试 ──────────────────────────────────── */
static TestResult test_pattern(u32 addr, u32 size_bytes, u32 pattern)
{
    TestResult r = {0};
    volatile u32 *buf = (volatile u32 *)addr;
    u32 n_words = size_bytes / 4;
    r.total_words = n_words;

    /* 写入 */
    for (u32 i = 0; i < n_words; i++)
        buf[i] = pattern;

    /*
     * 确保所有写操作完成（内存屏障）
     * DSB（Data Synchronization Barrier）：确保之前的内存访问完成
     * 这在写入后、读回验证前是必要的
     */
    dsb();

    /* 读回验证 */
    for (u32 i = 0; i < n_words; i++) {
        if (buf[i] != pattern) {
            if (r.error_count == 0) {
                r.first_error_addr = addr + i * 4;
                r.expected = pattern;
                r.actual = buf[i];
            }
            r.error_count++;
        }
    }
    return r;
}

/* ── 地址递增测试 ──────────────────────────────────── */
static TestResult test_address_walk(u32 addr, u32 size_bytes)
{
    TestResult r = {0};
    volatile u32 *buf = (volatile u32 *)addr;
    u32 n_words = size_bytes / 4;
    r.total_words = n_words;

    /* 写入：每个位置写入自己的地址偏移 */
    for (u32 i = 0; i < n_words; i++)
        buf[i] = i;

    dsb();

    /* 验证 */
    for (u32 i = 0; i < n_words; i++) {
        if (buf[i] != i) {
            if (r.error_count == 0) {
                r.first_error_addr = addr + i * 4;
                r.expected = i;
                r.actual = buf[i];
            }
            r.error_count++;
        }
    }
    return r;
}

/* ── 演示 Cache 与 DMA 的交互 ──────────────────────── */
static void demo_cache_flush(void)
{
    volatile u32 *buf = (volatile u32 *)TEST_BUF_ADDR;
    u32 test_val = 0x12345678;

    xil_printf("\r\n[Cache 演示]\r\n");

    /* 1. CPU 写数据（写入 D-Cache，可能还没到 DDR）*/
    buf[0] = test_val;
    xil_printf("  1. CPU 写入 buf[0] = 0x%08X（可能在 Cache 里）\r\n", test_val);

    /* 2. Flush Cache → 强制写回 DDR */
    Xil_DCacheFlushRange((UINTPTR)buf, 64);  /* Flush 64 字节（1 Cache line）*/
    xil_printf("  2. Flush D-Cache 后，数据已写入 DDR\r\n");

    /* 3. 演示 Invalidate：模拟 DMA 修改了 DDR */
    /* 绕过 Cache 直接写 DDR */
    Xil_Out32((UINTPTR)&buf[1], 0xBEEFCAFE);
    xil_printf("  3. 直接写 DDR buf[1] = 0xBEEFCAFE\r\n");

    /* 如果直接读，可能读到旧的 Cache 值 */
    xil_printf("  4. 未 Invalidate 前读 buf[1]（可能是旧值）: 0x%08X\r\n", buf[1]);

    /* Invalidate Cache → 下次读必须从 DDR 取 */
    Xil_DCacheInvalidateRange((UINTPTR)buf, 64);
    xil_printf("  5. Invalidate D-Cache 后读 buf[1]（新值）: 0x%08X\r\n", buf[1]);
}

static void print_result(const char *name, TestResult *r)
{
    xil_printf("  %-20s: %s（%lu 字，%lu 错误）",
               name,
               r->error_count == 0 ? "通过" : "失败",
               r->total_words,
               r->error_count);
    if (r->error_count > 0) {
        xil_printf("\r\n    首个错误地址: 0x%08X，期望: 0x%08X，实际: 0x%08X",
                   r->first_error_addr, r->expected, r->actual);
    }
    xil_printf("\r\n");
}

int main(void)
{
    Xil_ICacheEnable();
    Xil_DCacheEnable();

    xil_printf("\r\n================================================\r\n");
    xil_printf("  ANTSDR E310 裸机 - 步骤 05：DDR3 内存测试\r\n");
    xil_printf("================================================\r\n\r\n");

    xil_printf("[内存测试] 测试范围: 0x%08X，大小: %d KB\r\n\r\n",
               TEST_BUF_ADDR, TEST_BUF_SIZE / 1024);

    /* ── 执行各项测试 ── */
    TestResult r;

    xil_printf("[测试进行中...]\r\n");

    r = test_pattern(TEST_BUF_ADDR, TEST_BUF_SIZE, TEST_PATTERN_A);
    print_result("模式 0xA5A5A5A5", &r);

    r = test_pattern(TEST_BUF_ADDR, TEST_BUF_SIZE, TEST_PATTERN_B);
    print_result("模式 0x5A5A5A5A", &r);

    r = test_pattern(TEST_BUF_ADDR, TEST_BUF_SIZE, 0x00000000);
    print_result("全零", &r);

    r = test_pattern(TEST_BUF_ADDR, TEST_BUF_SIZE, 0xFFFFFFFF);
    print_result("全一", &r);

    r = test_address_walk(TEST_BUF_ADDR, TEST_BUF_SIZE);
    print_result("地址递增", &r);

    /* ── Cache 演示 ── */
    demo_cache_flush();

    xil_printf("\r\n[总结] DMA 操作的 Cache 规则：\r\n");
    xil_printf("  发送 DMA 前：Xil_DCacheFlushRange(buf, len)\r\n");
    xil_printf("  接收 DMA 后：Xil_DCacheInvalidateRange(buf, len)\r\n\r\n");
    xil_printf("[完成] 步骤 05 执行完毕。\r\n");
    xil_printf("下一步：06-spi-master\r\n\r\n");

    while (1) {}
    return 0;
}
