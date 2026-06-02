TARGET   := iio_rx_file
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
	@echo "已部署。运行: ssh $(DEVICE_USER)@$(DEVICE_IP) '$(DEVICE_PATH) -f 433e6 -s 5e6 -n 100000'"

clean:
	rm -f $(TARGET) $(TARGET)_native
