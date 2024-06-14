/*
// MediaTek MT6260/MT6261 firmware dumper for Linux.
//
// sudo modprobe ftdi_sio
// echo 0e8d 0003 | sudo tee /sys/bus/usb-serial/drivers/generic/new_id
// make && sudo ./mtk_dump [options] commands...
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
*/

#define _GNU_SOURCE 1

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#ifndef LIBUSB_DETACH
/* detach the device from crappy kernel drivers */
#define LIBUSB_DETACH 1
#endif

#if USE_LIBUSB
#include <libusb-1.0/libusb.h>
#else
#include <termios.h>
#include <fcntl.h>
#include <poll.h>
#endif
#include <unistd.h>

#include "mtk_cmd.h"

static void print_mem(FILE *f, const uint8_t *buf, size_t len) {
	size_t i; int a, j, n;
	for (i = 0; i < len; i += 16) {
		n = len - i;
		if (n > 16) n = 16;
		for (j = 0; j < n; j++) fprintf(f, "%02x ", buf[i + j]);
		for (; j < 16; j++) fprintf(f, "   ");
		fprintf(f, " |");
		for (j = 0; j < n; j++) {
			a = buf[i + j];
			fprintf(f, "%c", a > 0x20 && a < 0x7f ? a : '.');
		}
		fprintf(f, "|\n");
	}
}

static void print_string(FILE *f, uint8_t *buf, size_t n) {
	size_t i; int a, b = 0;
	fprintf(f, "\"");
	for (i = 0; i < n; i++) {
		a = buf[i]; b = 0;
		switch (a) {
		case '"': case '\\': b = a; break;
		case 0: b = '0'; break;
		case '\b': b = 'b'; break;
		case '\t': b = 't'; break;
		case '\n': b = 'n'; break;
		case '\f': b = 'f'; break;
		case '\r': b = 'r'; break;
		}
		if (b) fprintf(f, "\\%c", b);
		else if (a >= 32 && a < 127) fprintf(f, "%c", a);
		else fprintf(f, "\\x%02x", a);
	}
	fprintf(f, "\"\n");
}

#define ERR_EXIT(...) \
	do { fprintf(stderr, __VA_ARGS__); exit(1); } while (0)

#define DBG_LOG(...) fprintf(stderr, __VA_ARGS__)

#define RECV_BUF_LEN 1024
#define TEMP_BUF_LEN 1024

typedef struct {
	uint8_t *recv_buf, *buf;
#if USE_LIBUSB
	libusb_device_handle *dev_handle;
	int endp_in, endp_out;
#else
	int serial;
#endif
	int flags, recv_len, recv_pos, nread;
	int verbose, timeout;
} usbio_t;

#if USE_LIBUSB
static void find_endpoints(libusb_device_handle *dev_handle, int result[2]) {
	int endp_in = -1, endp_out = -1;
	int i, k, err;
	//struct libusb_device_descriptor desc;
	struct libusb_config_descriptor *config;
	libusb_device *device = libusb_get_device(dev_handle);
	if (!device)
		ERR_EXIT("libusb_get_device failed\n");
	//if (libusb_get_device_descriptor(device, &desc) < 0)
	//	ERR_EXIT("libusb_get_device_descriptor failed");
	err = libusb_get_config_descriptor(device, 0, &config);
	if (err < 0)
		ERR_EXIT("libusb_get_config_descriptor failed : %s\n", libusb_error_name(err));

	for (k = 0; k < config->bNumInterfaces; k++) {
		const struct libusb_interface *interface;
		const struct libusb_interface_descriptor *interface_desc;
		int claim = 0;
		interface = config->interface + k;
		if (interface->num_altsetting < 1) continue;
		interface_desc = interface->altsetting + 0;
		for (i = 0; i < interface_desc->bNumEndpoints; i++) {
			const struct libusb_endpoint_descriptor *endpoint;
			endpoint = interface_desc->endpoint + i;
			if (endpoint->bmAttributes == 2) {
				int addr = endpoint->bEndpointAddress;
				err = 0;
				if (addr & 0x80) {
					if (endp_in >= 0) ERR_EXIT("more than one endp_in\n");
					endp_in = addr;
					claim = 1;
				} else {
					if (endp_out >= 0) ERR_EXIT("more than one endp_out\n");
					endp_out = addr;
					claim = 1;
				}
			}
		}
		if (claim) {
			i = interface_desc->bInterfaceNumber;
#if LIBUSB_DETACH
			err = libusb_kernel_driver_active(dev_handle, i);
			if (err > 0) {
				DBG_LOG("kernel driver is active, trying to detach\n");
				err = libusb_detach_kernel_driver(dev_handle, i);
				if (err < 0)
					ERR_EXIT("libusb_detach_kernel_driver failed : %s\n", libusb_error_name(err));
			}
#endif
			err = libusb_claim_interface(dev_handle, i);
			if (err < 0)
				ERR_EXIT("libusb_claim_interface failed : %s\n", libusb_error_name(err));
			break;
		}
	}
	if (endp_in < 0) ERR_EXIT("endp_in not found\n");
	if (endp_out < 0) ERR_EXIT("endp_out not found\n");
	libusb_free_config_descriptor(config);

	//DBG_LOG("USB endp_in=%02x, endp_out=%02x\n", endp_in, endp_out);

	result[0] = endp_in;
	result[1] = endp_out;
}
#else
static void init_serial(int serial) {
	struct termios tty = { 0 };

	// B921600
	cfsetispeed(&tty, B115200);
	cfsetospeed(&tty, B115200);

	tty.c_cflag = CS8 | CLOCAL | CREAD;
	tty.c_iflag = IGNPAR;
	tty.c_oflag = 0;
	tty.c_lflag = 0;

	tty.c_cc[VMIN] = 1;
	tty.c_cc[VTIME] = 0;

	tcflush(serial, TCIFLUSH);
	tcsetattr(serial, TCSANOW, &tty);
}
#endif

#if USE_LIBUSB
static usbio_t* usbio_init(libusb_device_handle *dev_handle, int flags) {
#else
static usbio_t* usbio_init(int serial, int flags) {
#endif
	uint8_t *p; usbio_t *io;

#if USE_LIBUSB
	int endpoints[2];
	find_endpoints(dev_handle, endpoints);
#else
	init_serial(serial);
	// fcntl(serial, F_SETFL, FNDELAY);
	tcflush(serial, TCIOFLUSH);
#endif

	p = (uint8_t*)malloc(sizeof(usbio_t) + RECV_BUF_LEN + TEMP_BUF_LEN);
	io = (usbio_t*)p; p += sizeof(usbio_t);
	if (!p) ERR_EXIT("malloc failed\n");
	io->flags = flags;
#if USE_LIBUSB
	io->dev_handle = dev_handle;
	io->endp_in = endpoints[0];
	io->endp_out = endpoints[1];
#else
	io->serial = serial;
#endif
	io->recv_len = 0;
	io->recv_pos = 0;
	io->recv_buf = p; p += RECV_BUF_LEN;
	io->buf = p;
	io->verbose = 0;
	io->timeout = 1000;
	return io;
}

static void usbio_free(usbio_t* io) {
	if (!io) return;
#if USE_LIBUSB
	libusb_close(io->dev_handle);
#else
	close(io->serial);
#endif
	free(io);
}

#define WRITE16_BE(p, a) do { \
	((uint8_t*)(p))[0] = (a) >> 8; \
	((uint8_t*)(p))[1] = (uint8_t)(a); \
} while (0)

#define WRITE32_BE(p, a) do { \
	((uint8_t*)(p))[0] = (a) >> 24; \
	((uint8_t*)(p))[1] = (a) >> 16; \
	((uint8_t*)(p))[2] = (a) >> 8; \
	((uint8_t*)(p))[3] = (uint8_t)(a); \
} while (0)

#define READ16_BE(p) ( \
	((uint8_t*)(p))[0] << 8 | \
	((uint8_t*)(p))[1])

#define READ32_BE(p) ( \
	((uint8_t*)(p))[0] << 24 | \
	((uint8_t*)(p))[1] << 16 | \
	((uint8_t*)(p))[2] << 8 | \
	((uint8_t*)(p))[3])

#define READ32_LE(p) ( \
	((uint8_t*)(p))[3] << 24 | \
	((uint8_t*)(p))[2] << 16 | \
	((uint8_t*)(p))[1] << 8 | \
	((uint8_t*)(p))[0])

static int usb_send(usbio_t *io, const void *data, int len) {
	const uint8_t *buf = (const uint8_t*)data;
	int ret;

	if (!buf) buf = io->buf;
	if (!len) ERR_EXIT("empty message\n");
	if (io->verbose >= 2) {
		DBG_LOG("send (%d):\n", len);
		print_mem(stderr, buf, len);
	}

#if USE_LIBUSB
	{
		int err = libusb_bulk_transfer(io->dev_handle,
				io->endp_out, (uint8_t*)buf, len, &ret, io->timeout);
		if (err < 0)
			ERR_EXIT("usb_send failed : %s\n", libusb_error_name(err));
	}
#else
	ret = write(io->serial, buf, len);
#endif
	if (ret != len)
		ERR_EXIT("usb_send failed (%d / %d)\n", ret, len);

#if !USE_LIBUSB
	tcdrain(io->serial);
	// usleep(1000);
#endif
	return ret;
}

static int usb_recv(usbio_t *io, int plen) {
	uint8_t *buf = io->buf;
	int a, pos, len, nread = 0;
	if (plen > TEMP_BUF_LEN)
		ERR_EXIT("target length too long\n");

	len = io->recv_len;
	pos = io->recv_pos;
	while (nread < plen) {
		if (pos >= len) {
#if USE_LIBUSB
			int err = libusb_bulk_transfer(io->dev_handle, io->endp_in, io->recv_buf, RECV_BUF_LEN, &len, io->timeout);
			if (err == LIBUSB_ERROR_NO_DEVICE)
				ERR_EXIT("connection closed\n");
			else if (err == LIBUSB_ERROR_TIMEOUT) break;
			else if (err < 0)
				ERR_EXIT("usb_recv failed : %s\n", libusb_error_name(err));
#else
			if (io->timeout >= 0) {
				struct pollfd fds = { 0 };
				fds.fd = io->serial;
				fds.events = POLLIN;
				a = poll(&fds, 1, io->timeout);
				if (a < 0) ERR_EXIT("poll failed, ret = %d\n", a);
				if (fds.revents & POLLHUP)
					ERR_EXIT("connection closed\n");
				if (!a) break;
			}
			len = read(io->serial, io->recv_buf, RECV_BUF_LEN);
#endif
			if (len < 0)
				ERR_EXIT("usb_recv failed, ret = %d\n", len);

			if (io->verbose >= 2) {
				DBG_LOG("recv (%d):\n", len);
				print_mem(stderr, io->recv_buf, len);
			}
			pos = 0;
			if (!len) break;
		}
		a = io->recv_buf[pos++];
		io->buf[nread++] = a;
	}
	io->recv_len = len;
	io->recv_pos = pos;
	io->nread = nread;
	return nread;
}

static void mtk_echo(usbio_t *io, const void *data, int len) {
	const uint8_t *ptr = (const uint8_t*)data;
	int ret;

	usb_send(io, ptr, len);
	ret = usb_recv(io, len);
	if (ret != len || memcmp(io->buf, ptr, len))
		ERR_EXIT("unexpected echo\n");
}

static void mtk_echo8(usbio_t *io, uint32_t value) {
	const uint8_t buf[1] = { value };
	mtk_echo(io, buf, sizeof(buf));
}

static void mtk_echo16(usbio_t *io, uint32_t value) {
	const uint8_t buf[2];
	WRITE16_BE(buf, value);
	mtk_echo(io, buf, sizeof(buf));
}

static void mtk_echo32(usbio_t *io, uint32_t value) {
	const uint8_t buf[4];
	WRITE32_BE(buf, value);
	mtk_echo(io, buf, sizeof(buf));
}

static void mtk_recv(usbio_t *io, uint32_t value) {
	const uint8_t buf[4];
	WRITE32_BE(buf, value);
	mtk_echo(io, buf, sizeof(buf));
}

static uint32_t mtk_status(usbio_t *io) {
	unsigned status;
	if (usb_recv(io, 2) != 2)
		ERR_EXIT("unexpected response\n");
	status = READ16_BE(io->buf);
	if (status >= 0x100)
		ERR_EXIT("unexpected status = %d (0x%04x)\n", status, status);
	else if (status && io->verbose >= 2)
		DBG_LOG("status = %d (0x%04x)\n", status, status);

	return status;
}

static uint32_t mtk_recv8(usbio_t *io) {
	if (usb_recv(io, 1) != 1)
		ERR_EXIT("unexpected response\n");
	return io->buf[0];
}

static uint32_t mtk_recv16(usbio_t *io) {
	if (usb_recv(io, 2) != 2)
		ERR_EXIT("unexpected response\n");
	return READ16_BE(io->buf);
}

static uint32_t mtk_recv32(usbio_t *io) {
	if (usb_recv(io, 4) != 4)
		ERR_EXIT("unexpected response\n");
	return READ32_BE(io->buf);
}

static int mtk_handshake(usbio_t *io) {
	static const uint8_t handshake[] = { 0xa0, 0x0a, 0x50, 0x05 };
	int i, ret;

	for (i = 0; i < 4; i++) {
		usb_send(io, handshake + i, 1);
		ret = mtk_recv8(io) ^ handshake[i];
		if (ret != 0xff) {
			if (!ret && !i) {
				DBG_LOG("handshake already done\n");
				return 0;
			}
			ERR_EXIT("unexpected response\n");
		}
	}
	return 1;
}

static unsigned dump_mem(usbio_t *io,
		uint32_t start, uint32_t len, const char *fn, int cmd) {
	uint32_t i, n, off, nread, step = 1024;
	int ret, legacy = cmd == CMD_LEGACY_READ;
	int align = cmd == CMD_READ32 ? 2 : 1;
	FILE *fo;

	if ((len | start) & ((1 << align) - 1))
		ERR_EXIT("unaligned read\n");

	fo = fopen(fn, "wb");
	if (!fo) ERR_EXIT("fopen(dump) failed\n");

	for (off = start; off < start + len; ) {
		n = start + len - off;
		if (n > step) n = step;

		mtk_echo8(io, cmd);
		mtk_echo32(io, off);
		mtk_echo32(io, n >> align);

		if (!legacy) {
			if (usb_recv(io, 2) != 2 || READ16_BE(io->buf)) {
				DBG_LOG("unexpected response\n");
				break;
			}
		}

		nread = usb_recv(io, n);
		if (nread != n) {
			DBG_LOG("unexpected response\n");
			break;
		}

		if (align == 1)
			for (i = 0; i < nread; i += 2) {
				uint32_t a = READ16_BE(io->buf + i);
				io->buf[i + 0] = a & 0xff;
				io->buf[i + 1] = a >> 8;
			}
		else if (align == 2)
			for (i = 0; i < nread; i += 4) {
				uint32_t a = READ32_BE(io->buf + i);
				io->buf[i + 0] = a & 0xff;
				io->buf[i + 1] = a >> 8;
				io->buf[i + 2] = a >> 16;
				io->buf[i + 3] = a >> 24;
			}

		if (fwrite(io->buf, 1, nread, fo) != nread) 
			ERR_EXIT("fwrite(dump) failed\n");

		if (!legacy) {
			if (usb_recv(io, 2) != 2 || READ16_BE(io->buf)) {
				DBG_LOG("unexpected response\n");
				break;
			}
		}

		off += nread;
		if (n != nread) break;
	}
	DBG_LOG("dump_mem: 0x%08x, target: 0x%x, read: 0x%x\n", start, len, off - start);
	fclose(fo);
	return off;
}

static void mtk_send_long(usbio_t *io, const uint8_t *buf, size_t size) {
	uint32_t i, n, step = 1024;
	for (i = 0; i < size; i += n) {
		n = size - i;
		if (n > step) n = step;
		usb_send(io, buf + i, n);
	}
}

static uint8_t* loadfile(const char *fn, size_t *num) {
	size_t n, j = 0; uint8_t *buf = 0;
	FILE *fi = fopen(fn, "rb");
	if (fi) {
		fseek(fi, 0, SEEK_END);
		n = ftell(fi);
		if (n) {
			fseek(fi, 0, SEEK_SET);
			buf = (uint8_t*)malloc(n);
			if (buf) j = fread(buf, 1, n, fi);
		}
		fclose(fi);
	}
	if (num) *num = j;
	return buf;
}

static uint32_t mtk_checksum(const uint8_t *buf, uint32_t size) {
	uint32_t i, chk = 0;

	for (i = 0; i < (size & -2); i += 2)
		chk ^= buf[i] | buf[i + 1] << 8;

	if (size & 1) chk ^= buf[i];
	return chk;
}

static uint32_t mtk_read16(usbio_t *io, uint32_t addr) {
	uint32_t val;
	mtk_echo8(io, CMD_READ16);
	mtk_echo32(io, addr);
	mtk_echo32(io, 1);
	mtk_status(io);
	val = mtk_recv16(io);
	mtk_status(io);
	return val;
}

static uint32_t mtk_read32(usbio_t *io, uint32_t addr) {
	uint32_t val;
	mtk_echo8(io, CMD_READ32);
	mtk_echo32(io, addr);
	mtk_echo32(io, 1);
	mtk_status(io);
	val = mtk_recv32(io);
	mtk_status(io);
	return val;
}

static void mtk_write16(usbio_t *io, uint32_t addr, uint32_t val) {
	mtk_echo8(io, CMD_WRITE16);
	mtk_echo32(io, addr);
	mtk_echo32(io, 1);
	mtk_status(io);
	mtk_echo16(io, val);
	mtk_status(io);
}

static void mtk_write32(usbio_t *io, uint32_t addr, uint32_t val) {
	mtk_echo8(io, CMD_WRITE32);
	mtk_echo32(io, addr);
	mtk_echo32(io, 1);
	mtk_status(io);
	mtk_echo32(io, val);
	mtk_status(io);
}

static uint64_t str_to_size(const char *str) {
	char *end; int shl = 0; uint64_t n;
	n = strtoull(str, &end, 0);
	if (*end) {
		if (!strcmp(end, "K")) shl = 10;
		else if (!strcmp(end, "M")) shl = 20;
		else if (!strcmp(end, "G")) shl = 30;
		else ERR_EXIT("unknown size suffix\n");
	}
	if (shl) {
		int64_t tmp = n;
		tmp >>= 63 - shl;
		if (tmp && ~tmp)
			ERR_EXIT("size overflow on multiply\n");
	}
	return n << shl;
}

#define REOPEN_FREQ 2

int main(int argc, char **argv) {
#if USE_LIBUSB
	libusb_device_handle *device;
#else
	int serial;
#endif
	usbio_t *io; int ret, i;
	int wait = 30 * REOPEN_FREQ;
	const char *tty = "/dev/ttyUSB0";
	int verbose = 0;
	uint32_t info[4] = { -1, -1, -1, -1 };

#if USE_LIBUSB
	ret = libusb_init(NULL);
	if (ret < 0)
		ERR_EXIT("libusb_init failed: %s\n", libusb_error_name(ret));
#endif

	while (argc > 1) {
		if (!strcmp(argv[1], "--tty")) {
			if (argc <= 2) ERR_EXIT("bad option\n");
			tty = argv[2];
			argc -= 2; argv += 2;
		} else if (!strcmp(argv[1], "--wait")) {
			if (argc <= 2) ERR_EXIT("bad option\n");
			wait = atoi(argv[2]) * REOPEN_FREQ;
			argc -= 2; argv += 2;
		} else if (!strcmp(argv[1], "--verbose")) {
			if (argc <= 2) ERR_EXIT("bad option\n");
			verbose = atoi(argv[2]);
			argc -= 2; argv += 2;
		} else if (argv[1][0] == '-') {
			ERR_EXIT("unknown option\n");
		} else break;
	}

	for (i = 0; ; i++) {
#if USE_LIBUSB
		device = libusb_open_device_with_vid_pid(NULL, 0x0e8d, 0x0003);
		if (device) break;
		if (i >= wait)
			ERR_EXIT("libusb_open_device failed\n");
#else
		serial = open(tty, O_RDWR | O_NOCTTY | O_SYNC);
		if (serial >= 0) break;
		if (i >= wait)
			ERR_EXIT("open(ttyUSB) failed\n");
#endif
		if (!i) DBG_LOG("Waiting for connection (%ds)\n", wait / REOPEN_FREQ);
		usleep(1000000 / REOPEN_FREQ);
	}

#if USE_LIBUSB
	io = usbio_init(device, 0);
#else
	io = usbio_init(serial, 0);
#endif
	io->verbose = verbose;

	while (argc > 1) {
		
		if (!strcmp(argv[1], "verbose")) {
			if (argc <= 2) ERR_EXIT("bad command\n");
			io->verbose = atoi(argv[2]);
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "bl_ver")) {
			uint8_t get_ver;

			get_ver = CMD_GET_BL_VER;
			usb_send(io, &get_ver, 1);
			mtk_recv8(io);
			if (io->buf[0] != get_ver)
				DBG_LOG("BOOTLOADER version: 0x%02x\n", io->buf[0]);
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "connect")) {
			uint8_t get_ver;
			uint32_t i, chip;

			mtk_handshake(io);

			get_ver = CMD_GET_VERSION;
			usb_send(io, &get_ver, 1);
			mtk_recv8(io);
			if (io->buf[0] != get_ver) {
				DBG_LOG("BROM version: 0x%02x\n", io->buf[0]);
				if (io->buf[0] < 5) ERR_EXIT("unexpected version\n");
			}

			get_ver = CMD_GET_BL_VER;
			usb_send(io, &get_ver, 1);
			mtk_recv8(io);
			if (io->buf[0] != get_ver)
				DBG_LOG("BOOTLOADER version: 0x%02x\n", io->buf[0]);

			for (i = 0; i < 4; i++) {
				mtk_echo8(io, CMD_LEGACY_READ);
				mtk_echo32(io, 0x80000000 + i * 4);
				mtk_echo32(io, 1);
				info[i] = mtk_recv16(io);
			}

			DBG_LOG("HW = %04X:%04X, SW = %04X:%04X\n",
					info[2], info[3], info[0], info[1]);

			chip = info[2];
			// disable watchdog
			if (chip == 0x6260 || chip == 0x6261)
				mtk_write16(io, 0xa0030000, 0x2200);

			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "handshake")) {
			mtk_handshake(io);
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "show_flash")) {
			uint32_t addr = 0xa0510000, val, val2, state;
			uint32_t chip = info[2];
			if (argc <= 2) ERR_EXIT("bad command\n");
			state = atoi(argv[2]);

			if (chip == 0x6260 || chip == 0x6261) {
				val = mtk_read32(io, addr);
				val2 = state ? val | 2 : val & ~2;
				if (val != val2)
					mtk_write32(io, addr, val2);
			}
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "reboot")) {
			uint32_t addr = 0xa003001c;
			uint32_t chip = info[2];

			if (chip == 0x6260 || chip == 0x6261) {
				mtk_write32(io, addr, 0x1209);
			}
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "jump_bl")) {
			mtk_echo8(io, CMD_JUMP_BL);
			mtk_status(io);
			mtk_status(io);
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "get_meid")) {
			uint32_t i, size, status;

			mtk_echo8(io, CMD_GET_ME_ID);
			size = mtk_recv32(io);
			ret = usb_recv(io, size);
			if (ret != (int)size)
				ERR_EXIT("unexpected response\n");

			DBG_LOG("MEID: ");
			for (i = 0; i < size; i++)
				DBG_LOG("%02x", io->buf[i]);
			DBG_LOG("\n");

			mtk_status(io);
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "read16")) {
			const char *fn; uint32_t addr, size;
			if (argc <= 4) ERR_EXIT("bad command\n");

			addr = str_to_size(argv[2]);
			size = str_to_size(argv[3]);
			fn = argv[4];
			dump_mem(io, addr, size, fn, CMD_READ16);
			argc -= 4; argv += 4;

		} else if (!strcmp(argv[1], "read32")) {
			const char *fn; uint32_t addr, size;
			if (argc <= 4) ERR_EXIT("bad command\n");

			addr = str_to_size(argv[2]);
			size = str_to_size(argv[3]);
			fn = argv[4];
			dump_mem(io, addr, size, fn, CMD_READ32);
			argc -= 4; argv += 4;

		} else if (!strcmp(argv[1], "legacy_read")) {
			const char *fn; uint32_t addr, size;
			if (argc <= 4) ERR_EXIT("bad command\n");

			addr = str_to_size(argv[2]);
			size = str_to_size(argv[3]);
			fn = argv[4];
			dump_mem(io, addr, size, fn, CMD_LEGACY_READ);
			argc -= 4; argv += 4;

		} else if (!strcmp(argv[1], "send_da")) {
			const char *fn; uint32_t addr, sig_len, chk1, chk2;
			uint8_t *mem; size_t size = 0;
			if (argc <= 4) ERR_EXIT("bad command\n");

			fn = argv[2];
			addr = str_to_size(argv[3]);
			sig_len = strtol(argv[4], NULL, 0);

			mem = loadfile(fn, &size);
			if (!mem) ERR_EXIT("loadfile(\"%s\") failed\n", fn);
			if (size >> 32) ERR_EXIT("file too big\n");

			mtk_echo8(io, CMD_SEND_DA);
			mtk_echo32(io, addr);
			mtk_echo32(io, size);
			mtk_echo32(io, sig_len);
			mtk_status(io);

			chk2 = mtk_checksum(mem, size);
			mtk_send_long(io, mem, size);
			chk1 = mtk_recv16(io);
			free(mem);

			if (chk1 != chk2)
				ERR_EXIT("bad checksum (recv 0x%04x, calc 0x%04x)\n", chk1, chk2);
			mtk_status(io);
			argc -= 4; argv += 4;

		} else if (!strcmp(argv[1], "send_epp")) {
			const char *fn; uint32_t addr, addr2, size2, chk1, chk2;
			uint8_t *mem; size_t size = 0;
			if (argc <= 5) ERR_EXIT("bad command\n");

			fn = argv[2];
			addr = str_to_size(argv[3]);
			addr2 = str_to_size(argv[4]);
			size2 = str_to_size(argv[5]);

			mem = loadfile(fn, &size);
			if (!mem) ERR_EXIT("loadfile(\"%s\") failed\n", fn);
			if (size >> 32) ERR_EXIT("file too big\n");

			mtk_echo8(io, CMD_SEND_EPP);
			mtk_echo32(io, addr);
			mtk_echo32(io, size);
			mtk_echo32(io, addr2);
			mtk_echo32(io, size2);
			mtk_status(io);

			chk2 = mtk_checksum(mem, size);
			mtk_send_long(io, mem, size);
			chk1 = mtk_recv16(io);
			free(mem);

			if (chk1 != chk2)
				ERR_EXIT("bad checksum (recv 0x%04x, calc 0x%04x)\n", chk1, chk2);
			mtk_status(io);

			// ...
			// mtk_status(io);
			// mtk_recv32(io);	// return value

			argc -= 5; argv += 5;

		} else if (!strcmp(argv[1], "auto_da")) {
			const char *fn; uint32_t addr, sig_len, chk1, chk2, entry;
			uint8_t *mem; size_t size = 0;
			const char *header = "MMM\1\x38\0\0\0FILE_INFO\0\0\0";

			if (argc <= 2) ERR_EXIT("bad command\n");
			fn = argv[2];

			mem = loadfile(fn, &size);
			if (!mem) ERR_EXIT("loadfile(\"%s\") failed\n", fn);
			if (size >> 32) ERR_EXIT("file too big\n");

			entry = READ32_LE(mem + 0x30);

			if (size < 0x38 || memcmp(mem, header, 0x14) ||
					size != (uint32_t)READ32_LE(mem + 0x20) || entry >= size)
				ERR_EXIT("unexpected header\n");
			addr = READ32_LE(mem + 0x1c);
			sig_len = READ32_LE(mem + 0x2c);

			DBG_LOG("addr = 0x%08x, size = 0x%x, sig_len = 0x%x, entry = 0x%x\n",
					addr, (int)size, sig_len, entry);

			mtk_echo8(io, CMD_SEND_DA);
			mtk_echo32(io, addr);
			mtk_echo32(io, size);
			mtk_echo32(io, sig_len);
			mtk_status(io);

			chk2 = mtk_checksum(mem, size);
			mtk_send_long(io, mem, size);
			chk1 = mtk_recv16(io);
			free(mem);

			if (chk1 != chk2)
				ERR_EXIT("bad checksum (recv 0x%04x, calc 0x%04x)\n", chk1, chk2);
			mtk_status(io);

			if (entry) {
				mtk_echo8(io, CMD_JUMP_DA);
				mtk_echo32(io, addr + entry);
				mtk_status(io);
			}
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "skip")) {
			uint32_t size;
			if (argc <= 2) ERR_EXIT("bad command\n");
			size = strtol(argv[2], NULL, 0);

			usb_recv(io, size);
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "jump_da")) {
			uint32_t addr;
			if (argc <= 2) ERR_EXIT("bad command\n");
			addr = str_to_size(argv[2]);

			mtk_echo8(io, CMD_JUMP_DA);
			mtk_echo32(io, addr);
			mtk_status(io);
			argc -= 2; argv += 2;

		} else {
			ERR_EXIT("unknown command\n");
		}
	}

	usbio_free(io);
#if USE_LIBUSB
	libusb_exit(NULL);
#endif
	return 0;
}
