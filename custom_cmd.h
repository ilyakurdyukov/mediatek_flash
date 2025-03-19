
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

static inline uint32_t sfi_read_status(usbio_t *io) {
	uint8_t msg[] = { 0x05 }; // Read Status Register
	sfi_cmd(io, 0, msg, 1, 1);
	return io->buf[0];
}

/* Serial Flash Discoverable Parameter */
static void sfi_read_sfdp(usbio_t *io, int addr, void *buf, unsigned size) {
	uint8_t msg[5];
	uint8_t *dst = (uint8_t*)buf, *end = dst + size;
	unsigned n;

	msg[0] = 0x5a;
	msg[4] = 0;
	while ((n = end - dst)) {
		if (n > 128) n = 128;
		msg[1] = addr >> 16;
		msg[2] = addr >> 8;
		msg[3] = addr;
		sfi_cmd(io, 0, msg, 5, n);
		memcpy(dst, io->buf, n);
		addr += n; dst += n;
	}
}

static void sfi_read(usbio_t *io, int addr, void *buf, unsigned size) {
	uint8_t msg[5];
	uint8_t *dst = (uint8_t*)buf, *end = dst + size;
	unsigned n, k;

	msg[0] = 0x03;
	while ((n = end - dst)) {
		k = 4;
		if (addr >> 24) {
			msg[0] = 0x13;
			msg[1] = addr >> 24;
			k++;
		}
		msg[k - 3] = addr >> 16;
		msg[k - 2] = addr >> 8;
		msg[k - 1] = addr;
		if (n > 128) n = 128; // max = 0x90 - k ?
		sfi_cmd(io, 0, msg, k, n);
		if (!dst) break;
		memcpy(dst, io->buf, n);
		addr += n; dst += n;
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

