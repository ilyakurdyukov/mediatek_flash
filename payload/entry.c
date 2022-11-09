#include <stdint.h>
#include <stddef.h>

typedef struct {
	int (*set_baud)(unsigned baud);
	int unknown[3];
	uint32_t (*get_timer)(void);
	int (*check_timer)(uint32_t start, int delay);
	uint32_t (*timer_to_us)(uint32_t timer);
	uint32_t (*us_to_timer)(uint32_t delay_us);
	uint32_t (*timer_to_ms)(uint32_t timer);
	uint32_t (*ms_to_timer)(uint32_t delay_ms);
	uint32_t (*usleep)(uint32_t delay_us);
	uint32_t (*msleep)(uint32_t delay_ms);
} timer_t;

typedef struct {
	int unknown[4];
	int (*recv8)(void);
	int (*send8)(int val, int /* 1 */);
	int (*recv16)(void);
	int (*send16)(int val, int /* 1 */);
	int (*recv32)(void);
	int (*send32)(int val, int /* 1 */);
	int (*recv_buf)(void *buf, int size, int with_checksum);
	int (*send_buf)(void *buf, int size, int unused /* 0 */);
} usbio_t;

enum {
	CMD_LEGACY_WRITE       = 0xa1,
	CMD_LEGACY_READ        = 0xa2,

	CMD_READ16             = 0xd0,
	CMD_READ32             = 0xd1,
	CMD_WRITE16            = 0xd2,
	CMD_WRITE16_NO_ECHO    = 0xd3,
	CMD_WRITE32            = 0xd4,
	CMD_JUMP_DA            = 0xd5,
	CMD_SEND_DA            = 0xd7
};

enum {
	FLAG_32BIT = 1,
	FLAG_LEGACY = 2,
	FLAG_NOECHO = 4
};

static void cmd_read(usbio_t *io, int flags) {
	uint32_t addr, size, i;

	addr = io->recv32(); io->send32(addr, 1);
	size = io->recv32(); io->send32(size, 1);

	if (!(flags & FLAG_LEGACY)) io->send16(0, 1);

	if (flags & FLAG_32BIT)
		for (i = 0; i < size; i++)
			io->send32(((uint32_t*)addr)[i], 1);
	else
		for (i = 0; i < size; i++)
			io->send16(((uint16_t*)addr)[i], 1);

	if (!(flags & FLAG_LEGACY)) io->send16(0, 1);
}

static void cmd_write(usbio_t *io, int flags) {
	uint32_t addr, size, i;

	addr = io->recv32(); io->send32(addr, 1);
	size = io->recv32(); io->send32(size, 1);

	if (!(flags & FLAG_LEGACY)) io->send16(1, 1);

	if (flags & FLAG_32BIT)
		for (i = 0; i < size; i++) {
			uint32_t val = io->recv32();
			io->send32(val, 1);
			((uint32_t*)addr)[i] = val;
		}
	else
		for (i = 0; i < size; i++) {
			uint32_t val = io->recv16();
			if (!(flags & FLAG_NOECHO))
				io->send16(val, 1);
			((uint16_t*)addr)[i] = val;
		}

	if (!(flags & FLAG_LEGACY)) io->send16(1, 1);
}

static void cmd_send_da(usbio_t *io) {
	uint32_t addr, size, sig_len, i, chk;

	addr = io->recv32(); io->send32(addr, 1);
	size = io->recv32(); io->send32(size, 1);
	sig_len = io->recv32(); io->send32(sig_len, 1);
	io->send16(0, 1);
#if 1
	(void)i; (void)chk;
	io->recv_buf((void*)addr, size, 1);
#else
	io->recv_buf((void*)addr, size, 0);
	chk = 0;
	for (i = 0; i < (size & -2); i++)
		chk ^= *(uint16_t*)(addr + i);
	if (size & 1) chk ^= *(uint8_t*)(addr + i);
	io->send16(chk, 1);
#endif
	io->send16(0, 1);
}

static void cmd_jump_da(usbio_t *io) {
	uint32_t addr;
	addr = io->recv32(); io->send32(addr, 1);
	io->send16(0, 1);
	((void(*)(void*))addr)(io);
}

#define MEM4(addr) *(volatile uint32_t*)(addr)

static inline int comm_check(volatile uint32_t *addr) {
	uint32_t a0 = addr[0], a1 = addr[1];
	// a0 = timer, a1 = usbio
	if ((a0 >> 16) == 0xfff0)
	if (a1 - a0 == sizeof(timer_t) && a0 - (uint32_t)addr == sizeof(usbio_t))
		return (uint32_t)a1;
	return 0;
}

#define METHOD 1

#if METHOD == 1
/* TODO: better to have own USB code, so not depend on the BROM version */
static usbio_t *find_usbio(void) {
	volatile uint32_t *addr = (uint32_t*)0xfff00000;
	volatile uint32_t *end = addr + 0x10000 / 4;
	for (; addr < end; addr++) {
		uint32_t found = comm_check(addr);
		if (found)
			return (usbio_t*)found;
	}
	return NULL;
}
#elif METHOD == 2
// alternative way, how stable is it?
// arg4 = da_arg, arg5 = arg_size
static usbio_t *usbio_from_arg(uint32_t arg4, uint32_t arg5) {
	uint32_t *addr, a0, a1, i;
	do {
		if (arg4 < 0x8e4) break;
		addr = *(uint32_t**)(arg4 + 0x8e0);
		if (((uint32_t)addr >> 16) != 0xfff0) break;
		a0 = addr[0]; a1 = addr[1];
		if ((a0 >> 16) != 0xfff0) break;
		if ((a1 >> 16) != 0xfff0) break;
		for (i = 0; i < sizeof(timer_t); i += 4)
			if ((*(uint32_t*)(a0 + i) >> 16) != 0xfff0) return NULL;
		for (i = 0; i < sizeof(usbio_t); i += 4)
			if ((*(uint32_t*)(a1 + i) >> 16) != 0xfff0) return NULL;
		return (usbio_t*)a1;
	} while (0);
	return NULL;
}
#endif

uint32_t entry_main(uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3,
		uint32_t arg4, uint32_t arg5) {
#if METHOD == 1
	usbio_t *io = find_usbio();
#elif METHOD == 2
	usbio_t *io = usbio_from_arg(arg4, arg5);
#else
	usbio_t *io = ((usbio_t**)(arg4 + 0x8e0))[1];
#endif
	if (!io) for (;;);

	// io->send32(0x12345678, 1);
	// io->send32(arg4, 1);
	// io->send32(arg5, 1);

	// enable flash ROM
	// MEM4(0xa0510000) |= 2;

	for (;;) {
		unsigned cmd;
		cmd = io->recv8();
		io->send8(cmd, 1);
		switch (cmd) {
		case CMD_LEGACY_READ:
			cmd_read(io, FLAG_LEGACY);
			break;
		case CMD_READ16:
			cmd_read(io, 0);
			break;
		case CMD_READ32:
			cmd_read(io, FLAG_32BIT);
			break;

		case CMD_LEGACY_WRITE:
			cmd_write(io, FLAG_LEGACY);
			break;
		case CMD_WRITE16:
			cmd_write(io, 0);
			break;
		case CMD_WRITE16_NO_ECHO:
			cmd_write(io, FLAG_NOECHO);
			break;
		case CMD_WRITE32:
			cmd_write(io, FLAG_32BIT);
			break;

		case CMD_JUMP_DA:
			cmd_jump_da(io);
			break;
		case CMD_SEND_DA:
			cmd_send_da(io);
			break;
		}
	}
}

