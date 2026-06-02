TARGET   := iio_spectrum_scan
SRCS     := main.c
CROSS_CC := arm-linux-gnueabihf-gcc
NATIVE_CC := gcc
CFLAGS   := -Wall -O2 -std=c11
LDFLAGS  := -liio -lm

DEVICE_IP   := 192.168.1.10
DEVICE_USER := root
DEVICE_PATH := /tmp/$(TARGET)

.PHONY: all native deploy clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CROSS_CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "编译完成：$@ (ARM)"

native: $(SRCS)
	$(NATIVE_CC) $(CFLAGS) -o $(TARGET)_native $^ $(LDFLAGS)

deploy: all
	scp $(TARGET) $(DEVICE_USER)@$(DEVICE_IP):$(DEVICE_PATH)
	@echo "已部署。运行示例:"
	@echo "  ssh $(DEVICE_USER)@$(DEVICE_IP) '$(DEVICE_PATH) --start 400e6 --stop 500e6 --step 1e6'"

clean:
	rm -f $(TARGET) $(TARGET)_native
