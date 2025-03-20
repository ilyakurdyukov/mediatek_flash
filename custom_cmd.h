
static unsigned spd_checksum(const void *src, int len) {
	uint16_t *s = (uint16_t*)src;
	uint32_t crc = 0;
	for (; len >= 2; len -= 2) crc += *s++;
	crc = (crc >> 16) + (crc & 0xffff);
	crc += crc >> 16;
	return ~crc & 0xffff;
}

static void sfi_cmd(usbio_t *io, int qpi, uint8_t *msg, unsigned mlen, unsigned rlen) {
	uint16_t *data = (uint16_t*)io->buf;
	uint8_t *buf = (uint8_t*)io->buf + 4;
	int rlen2;

	if (mlen + rlen > 256 + 6)
		ERR_EXIT("unexpected size\n");
	mtk_echo8(io, 0x55);
	memmove(buf, msg, mlen);
	data[0] = mlen | qpi << 15;
	data[1] = rlen;
	if (mlen & 1) buf[mlen++] = 0;
	*(uint16_t*)&buf[mlen] = spd_checksum(data, 4 + mlen);
	usb_send(io, NULL, 4 + mlen + 2);

	rlen2 = (rlen + 3) & ~1;
	if (usb_recv(io, rlen2) != rlen2)
		ERR_EXIT("unexpected response\n");
	if (spd_checksum(io->buf, rlen2))
		ERR_EXIT("bad checksum\n");
}

static void sfi_cmd_addr(usbio_t *io, unsigned cmd,
		unsigned addr, unsigned alen, unsigned rlen) {
	uint8_t msg[5];
	msg[0] = cmd;
	if (alen > 3) msg[1] = addr >> 24;
	msg[alen - 2] = addr >> 16;
	msg[alen - 1] = addr >> 8;
	msg[alen] = addr;
	sfi_cmd(io, 0, msg, alen + 1, rlen);
}

static inline uint32_t sfi_read_status(usbio_t *io) {
	uint8_t msg[] = { 0x05 }; // Read Status Register
	sfi_cmd(io, 0, msg, 1, 1);
	return io->buf[0];
}

/* Serial Flash Discoverable Parameter */
static void sfi_read_sfdp(usbio_t *io, uint32_t addr, void *buf, unsigned size) {
	uint8_t *dst = (uint8_t*)buf, *end = dst + size;
	unsigned n;
	while ((n = end - dst)) {
		if (n > 128) n = 128;
		sfi_cmd_addr(io, 0x5a, addr << 8, 4, n);
		memcpy(dst, io->buf, n);
		addr += n; dst += n;
	}
}

static void sfi_read(usbio_t *io, uint32_t addr, void *buf, unsigned size) {
	uint8_t *dst = (uint8_t*)buf, *end = dst + size;
	unsigned n;
	// DBG_LOG("sfi_read 0x%x, 0x%x\n", addr, size);
	while ((n = end - dst)) {
		unsigned cmd = 0x03, k = 3;
		if (addr >> 24) cmd = 0x13, k++;
		if (n > 128) n = 128; // max = 0x90 - k - 1 ?
		sfi_cmd_addr(io, cmd, addr, k, n);
		if (!dst) break;
		memcpy(dst, io->buf, n);
		addr += n; dst += n;
	}
}

static void sfi_write_enable(usbio_t *io) {
	uint8_t msg[] = { 0x06 }; // Write Enable
	sfi_cmd(io, 0, msg, 1, 0);
	while (!(sfi_read_status(io) & 2));
}

#define SFI_WRITE_WAIT \
	usleep(30); \
	/* wait for completion */ \
	while (sfi_read_status(io) & 1);

static void sfi_erase(usbio_t *io, uint32_t addr, int cmd, int addr_len) {
	sfi_write_enable(io);
	// DBG_LOG("sfi_erase 0x%x, 0x%x\n", addr, cmd);
	sfi_cmd_addr(io, cmd, addr, addr_len, 0);
	SFI_WRITE_WAIT
}

static void sfi_write(usbio_t *io, uint32_t addr, const void *buf, unsigned size) {
	uint8_t msg[128 + 5];
	const uint8_t *src = (const uint8_t*)buf, *end = src + size;
	unsigned n, k;

	// DBG_LOG("sfi_write 0x%x, 0x%x\n", addr, size);
	msg[0] = 0x02; // Page Program
	while ((n = end - src)) {
		k = 256 - (addr & 255);
		if (n > k) n = k;
		k = 4;
		if (addr >> 24)
			msg[0] = 0x12, msg[1] = addr >> 24, k++;
		msg[k - 3] = addr >> 16;
		msg[k - 2] = addr >> 8;
		msg[k - 1] = addr;
		if (n > 128) n = 128;
		memcpy(msg + k, src, n);
		sfi_write_enable(io);
		sfi_cmd(io, 0, msg, k + n, 0);
		SFI_WRITE_WAIT
		addr += n; src += n;
	}
}

static void sfi_write_cmp(usbio_t *io, uint32_t addr, const uint8_t *orig, const uint8_t *src, unsigned size) {
	const uint8_t *end = src + size;
	unsigned n, i, k;

	while ((n = end - src)) {
		k = 256 - (addr & 255);
		if (n > k) n = k;
		if (orig) {
			for (i = 0; i < n && orig[i] == src[i]; i++);
			orig += i;
		} else for (i = 0; i < n && 0xff == src[i]; i++);
		n -= i; addr += i; src += i;
		if (n > 128) n = 128;
		if (orig) {
			for (k = n; k && orig[k - 1] == src[k - 1]; k--);
			orig += n;
		} else for (k = n; k && 0xff == src[k - 1]; k--);
		if (k) sfi_write(io, addr, src, k);
		addr += n; src += n;
	}
}

static unsigned dump_flash(usbio_t *io,
		uint32_t start, uint32_t len, const char *fn) {
	uint32_t n, off, step = 128;
	FILE *fo;

	fo = fopen(fn, "wb");
	if (!fo) ERR_EXIT("fopen(dump) failed\n");

	for (off = start; off < start + len; off += n) {
		n = start + len - off;
		if (n > step) n = step;
		sfi_read(io, off, NULL, n);

		if (fwrite(io->buf, 1, n, fo) != n)
			ERR_EXIT("fwrite(dump) failed\n");
	}
	DBG_LOG("dump_flash: 0x%08x, target: 0x%x, read: 0x%x\n", start, len, off - start);
	fclose(fo);
	return off;
}

static unsigned erase_cmd = 0x20, erase_blk = 0x1000;

static void erase_flash(usbio_t *io,
		uint32_t addr, uint32_t size) {
	uint32_t end = addr + size;

	if ((addr | size) & (erase_blk - 1))
		ERR_EXIT("unaligned erase\n");

	for (; addr < end; addr += erase_blk)
		sfi_erase(io, addr, erase_cmd, 3);
}

static int flash_cmp(const uint8_t *s, const uint8_t *d, unsigned n) {
	unsigned i;
	for (i = 0; i < n; i++)
		if (~s[i] & d[i]) return 1;
	return 0;
}

static void write_flash_buf(usbio_t *io,
		const uint8_t *mem, uint32_t size, uint32_t addr) {
	uint32_t n, k, l, blk = erase_blk;
	uint32_t end = addr + size;

	if (blk > 0x1000)
		ERR_EXIT("unsupported erase block size\n");

	for (; addr < end; mem += n, addr += n) {
		uint8_t buf[0x1000];
		uint32_t i, n2, t, mask = 0;
		k = (addr & -blk) + blk;
		if (k > end) k = end;
		n = k - addr;

		// check if erasing is necessary
		for (i = addr; i < k; i += n2) {
			t = i & ~127; n2 = t + 128;
			if (n2 > k) n2 = k;
			n2 -= i;
			l = t & (blk - 1);
			mask |= 1 << (l >> 7);
			sfi_read(io, t, buf + l, 128);
			if (flash_cmp(buf + (i & (blk - 1)), mem + (i - addr), n2)) break;
		}

		if (i < k) {
			i = addr & -blk;
			// read missing parts
			for (l = i + blk; i < l; i += 128)
				if ((i < addr || i + 128 > addr + n) &&
						!(mask >> ((i & (blk - 1)) >> 7) & 1))
					sfi_read(io, i, buf + (i & (blk - 1)), 128);
			l -= blk;
			memcpy(buf + (addr & (blk - 1)), mem, n);
			sfi_erase(io, l, erase_cmd, 3);
			sfi_write_cmp(io, l, NULL, buf, blk);
		} else {
			sfi_write_cmp(io, addr, buf + (addr & (blk - 1)), mem, n);
		}
	}
}

static void write_flash(usbio_t *io, const char *fn,
		unsigned src_offs, uint32_t src_size, uint32_t addr) {
	uint8_t *mem; size_t size = 0;
	mem = loadfile(fn, &size);
	if (!mem) ERR_EXIT("loadfile(\"%s\") failed\n", fn);
	if (size >> 32) ERR_EXIT("file too big\n");
	if (size < src_offs)
		ERR_EXIT("data outside the file\n");
	size -= src_offs;
	if (src_size) {
		if (size < src_size)
			ERR_EXIT("data outside the file\n");
		size = src_size;
	}
	write_flash_buf(io, mem + src_offs, size, addr);
	free(mem);
}

