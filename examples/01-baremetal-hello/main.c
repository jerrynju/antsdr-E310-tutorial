/*
 * ANTSDR E310 裸机 Hello World
 *
 * 运行环境：Vitis Bare-metal，无操作系统
 * 功能：通过串口打印信息，读取 CPU 频率，控制 GPIO LED
 *
 * 编译方式：在 Vitis IDE 中创建 Platform（基于 system_top.xsa），
 *           然后创建 Application Project，将此文件放入 src/ 目录。
 */

#include <stdio.h>
#include "xparameters.h"   /* 硬件地址表（Vitis 从 XSA 自动生成） */
#include "xil_printf.h"    /* 轻量级 printf，输出到 UART */
#include "xil_io.h"        /* Xil_In32 / Xil_Out32 寄存器读写 */
#include "xil_cache.h"     /* Cache 控制 */
#include "sleep.h"         /* usleep / sleep */

/* 如果板上有 AXI GPIO，取消注释并包含此头文件 */
/* #include "xgpio.h" */

/*
 * 从 ARM CP15 协处理器读取当前 CPU 频率
 * 实际频率由 Vivado 硬件设计决定，E310 通常为 666 MHz 或 800 MHz
 */
static void print_cpu_info(void)
{
    u32 midr;
    /* 读取 Main ID Register */
    asm volatile ("mrc p15, 0, %0, c0, c0, 0" : "=r"(midr));

    xil_printf("CPU MIDR = 0x%08X\r\n", midr);
    xil_printf("  Implementer : 0x%02X (%s)\r\n",
               (midr >> 24) & 0xFF,
               ((midr >> 24) & 0xFF) == 0x41 ? "ARM Ltd." : "Unknown");
    xil_printf("  Part Number : 0x%03X (%s)\r\n",
               (midr >> 4) & 0xFFF,
               ((midr >> 4) & 0xFFF) == 0xC09 ? "Cortex-A9" : "Unknown");
}

/*
 * 读写一个 AXI-Lite 寄存器（演示 PS→PL 控制）
 * 实际地址从 xparameters.h 中 XPAR_*_BASEADDR 宏获取
 */
static void demo_axi_register_access(void)
{
#ifdef XPAR_AXI_GPIO_0_BASEADDR
    xil_printf("\r\n[AXI GPIO Demo]\r\n");
    /* 将方向寄存器设置为全部输出（偏移 0x04） */
    Xil_Out32(XPAR_AXI_GPIO_0_BASEADDR + 0x04, 0x00000000);

    /* 闪烁 LED 5 次 */
    for (int i = 0; i < 5; i++) {
        Xil_Out32(XPAR_AXI_GPIO_0_BASEADDR, 0xFFFFFFFF); /* 全亮 */
        usleep(500000);
        Xil_Out32(XPAR_AXI_GPIO_0_BASEADDR, 0x00000000); /* 全灭 */
        usleep(500000);
        xil_printf("Blink %d\r\n", i + 1);
    }
#else
    xil_printf("[AXI GPIO] 硬件未包含 AXI GPIO，跳过演示\r\n");
#endif
}

/*
 * 演示 DDR3 内存读写测试
 * 写入一段数据，再读回验证，检测 DDR 完整性
 */
static int demo_memory_test(void)
{
    /* 使用 DDR 中安全的测试区域（避开代码段） */
    volatile u32 *test_base = (volatile u32 *)0x10000000;
    const int test_count = 1024;
    int errors = 0;

    xil_printf("\r\n[Memory Test] 写入 %d 个字到 0x%08X...\r\n",
               test_count, (u32)test_base);

    /* 禁用 D-Cache 确保真实写入 DDR */
    Xil_DCacheDisable();

    for (int i = 0; i < test_count; i++)
        test_base[i] = (u32)(0xDEAD0000 + i);

    for (int i = 0; i < test_count; i++) {
        if (test_base[i] != (u32)(0xDEAD0000 + i)) {
            xil_printf("  错误：地址 0x%08X 期望 0x%08X 实际 0x%08X\r\n",
                       (u32)&test_base[i],
                       0xDEAD0000 + i,
                       test_base[i]);
            errors++;
        }
    }

    Xil_DCacheEnable();

    if (errors == 0)
        xil_printf("[Memory Test] 通过！无错误\r\n");
    else
        xil_printf("[Memory Test] 失败！%d 个错误\r\n", errors);

    return errors;
}

int main(void)
{
    /* 初始化 Cache */
    Xil_ICacheEnable();
    Xil_DCacheEnable();

    xil_printf("\r\n");
    xil_printf("================================================\r\n");
    xil_printf("  ANTSDR E310 Bare-metal Hello World\r\n");
    xil_printf("  Zynq-7020 ARM Cortex-A9\r\n");
    xil_printf("================================================\r\n\r\n");

    /* 打印 CPU 信息 */
    print_cpu_info();

    /* 内存测试 */
    demo_memory_test();

    /* GPIO / LED 演示 */
    demo_axi_register_access();

    xil_printf("\r\n[Done] 程序执行完毕，进入空循环\r\n");

    /* 裸机程序不能返回，进入空循环 */
    while (1) {
        /* 这里可以放主循环逻辑 */
    }

    return 0; /* 不会到达 */
}
