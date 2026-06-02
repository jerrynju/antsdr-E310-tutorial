/*
 * 步骤 01：UART 串口输出 — Hello World
 *
 * 硬件：UART1 (0xE0001000)，115200-8-N-1
 * 连接：E310 UART → USB-UART → PC 串口工具
 *
 * 学习目标：
 *   1. 理解 Vitis 裸机项目结构（BSP, linker script, startup）
 *   2. 掌握 xil_printf —— 裸机调试的基础工具
 *   3. 读取 CPU 型号寄存器，验证 ARM Cortex-A9
 *   4. 理解裸机程序"不能返回"的特性
 *
 * 新手说明：
 *   裸机程序没有操作系统，main() 返回后没有地方去。
 *   程序最后必须有 while(1){} 无限循环，否则 CPU 会跑飞。
 */

#include "xparameters.h"  /* 自动生成的硬件地址表 */
#include "xil_printf.h"   /* 轻量 printf，输出到 UART，约 2 KB */
#include "xil_io.h"       /* Xil_In32 / Xil_Out32 寄存器读写 */
#include "xil_cache.h"    /* CPU Cache 初始化 */
#include "sleep.h"        /* usleep(microseconds), sleep(seconds) */

/* ── E310 专用常量（从 00-hardware-map 查阅）─── */
#define UART1_BASEADDR   0xE0001000
#define CPU_ID_REG       0xF8007080  /* Zynq PS: CPU0 ID 寄存器 */

/*
 * 从 ARM CP15 协处理器读取 Main ID Register (MIDR)
 * 包含：制造商代码、架构版本、部件号、修订号
 */
static u32 read_midr(void)
{
    u32 midr;
    asm volatile("mrc p15, 0, %0, c0, c0, 0" : "=r"(midr));
    return midr;
}

/* 读取 CPU 当前工作模式（来自 CPSR 寄存器）*/
static u32 read_cpsr(void)
{
    u32 cpsr;
    asm volatile("mrs %0, cpsr" : "=r"(cpsr));
    return cpsr;
}

static void print_cpu_info(void)
{
    u32 midr = read_midr();
    u32 cpsr = read_cpsr();

    xil_printf("\r\n[CPU 信息]\r\n");
    xil_printf("  MIDR = 0x%08X\r\n", midr);
    xil_printf("  制造商  : 0x%02X (%s)\r\n",
               (midr >> 24) & 0xFF,
               ((midr >> 24) & 0xFF) == 0x41 ? "ARM Ltd." : "Unknown");
    xil_printf("  架构    : ARMv%d\r\n",
               ((midr >> 16) & 0xF) == 0xF ? 7 : ((midr >> 16) & 0xF));
    xil_printf("  部件号  : 0x%03X (%s)\r\n",
               (midr >> 4) & 0xFFF,
               ((midr >> 4) & 0xFFF) == 0xC09 ? "Cortex-A9" : "Unknown");
    xil_printf("  修订号  : %d\r\n", midr & 0xF);

    /* CPSR mode bits [4:0] */
    const char *modes[] = {"USR","FIQ","IRQ","SVC","---","---","---","ABT",
                            "---","---","---","UND","---","---","---","SYS"};
    u32 mode = cpsr & 0x1F;
    xil_printf("  当前模式: 0x%02X (%s)\r\n", mode,
               mode < 16 ? modes[mode] : "SVC");
}

static void print_memory_map(void)
{
    xil_printf("\r\n[内存布局]\r\n");
    xil_printf("  OCM  (片上内存) : 0x00000000 - 0x0007FFFF (512 KB)\r\n");
    xil_printf("  DDR3 (外部内存) : 0x00100000 - 0x1FFFFFFF (511 MB)\r\n");
    xil_printf("  PS 外设         : 0xE0000000 - 0xEFFFFFFF\r\n");
    xil_printf("  PL 外设 (AXI)   : 0x40000000 - 0x7FFFFFFF\r\n");
    xil_printf("  SCU / GIC       : 0xF8F00000\r\n");
}

int main(void)
{
    /* 使能 I-Cache 和 D-Cache，提升代码执行速度 */
    Xil_ICacheEnable();
    Xil_DCacheEnable();

    /* ── 欢迎信息 ── */
    xil_printf("\r\n");
    xil_printf("================================================\r\n");
    xil_printf("  ANTSDR E310 裸机入门 - 步骤 01\r\n");
    xil_printf("  UART Hello World\r\n");
    xil_printf("================================================\r\n");

    print_cpu_info();
    print_memory_map();

    /* ── 演示：格式化打印 ── */
    xil_printf("\r\n[格式化输出演示]\r\n");
    xil_printf("  整数    : %d\r\n", 42);
    xil_printf("  十六进制: 0x%08X\r\n", 0xDEADBEEF);
    xil_printf("  字符串  : %s\r\n", "Hello E310!");

    /* ── 演示：延时 ── */
    xil_printf("\r\n[延时演示] 倒计时 5 秒...\r\n");
    for (int i = 5; i > 0; i--) {
        xil_printf("  %d...\r\n", i);
        sleep(1);   /* sleep() 基于 TTC 定时器实现 */
    }

    xil_printf("\r\n[完成] 步骤 01 执行完毕，进入空循环。\r\n");
    xil_printf("下一步：02-gpio-led\r\n\r\n");

    /*
     * 裸机程序必须以无限循环结束。
     * 没有 OS 可以接管 CPU，一旦 main 返回，行为未定义。
     */
    while (1) {
        /* 实际应用中可在这里轮询任务 */
    }

    return 0; /* 不可达 */
}
