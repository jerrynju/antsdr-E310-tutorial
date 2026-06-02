# 交叉编译 Makefile — 用于在主机上编译出运行在 E310 ARM 上的程序
#
# 用法：
#   make              # 交叉编译（生成 ARM 可执行文件）
#   make native       # 本机编译（在 E310 上直接 make 时使用）
#   make deploy       # 编译并通过 scp 推送到设备
#   make clean        # 清理

TARGET   := iio_basic
SRCS     := main.c

# 交叉编译器（确保已 export PATH 包含 Linaro 工具链）
CROSS_CC := arm-linux-gnueabihf-gcc
# 本机编译器
NATIVE_CC := gcc

CFLAGS   := -Wall -O2 -std=c11
LDFLAGS  := -liio -lm

# 设备 IP（make deploy 时使用）
DEVICE_IP := 192.168.1.10
DEVICE_USER := root
DEVICE_PATH := /tmp/$(TARGET)

# ── 目标 ─────────────────────────────────────────────

.PHONY: all native deploy clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CROSS_CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "编译完成：$@ (ARM)"
	@file $@

native: $(SRCS)
	$(NATIVE_CC) $(CFLAGS) -o $(TARGET)_native $^ $(LDFLAGS)
	@echo "本机编译完成：$(TARGET)_native"

deploy: all
	scp $(TARGET) $(DEVICE_USER)@$(DEVICE_IP):$(DEVICE_PATH)
	@echo "已推送到 $(DEVICE_USER)@$(DEVICE_IP):$(DEVICE_PATH)"
	@echo "运行命令：ssh $(DEVICE_USER)@$(DEVICE_IP) $(DEVICE_PATH)"

clean:
	rm -f $(TARGET) $(TARGET)_native
