
LIBUSB = 1
CFLAGS = -O2 -Wall -Wextra -std=c99 -pedantic -Wno-unused
CFLAGS += -DUSE_LIBUSB=$(LIBUSB)

ifeq ($(LIBUSB), 1)
LIBS = -lusb-1.0
endif

.PHONY: all clean
all: mtk_dump

clean:
	$(RM) mtk_dump

mtk_dump: mtk_dump.c mtk_cmd.h custom_cmd.h
	$(CC) -s $(CFLAGS) -o $@ $< $(LIBS)
