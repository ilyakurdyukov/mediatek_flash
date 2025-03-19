
#define SFI_BASE 0xa0140000

static void sfi_cmd(int qpi, uint8_t *msg, uint8_t *ret, int mlen, int rlen) {
	volatile uint32_t *ptr32 = (uint32_t*)(SFI_BASE + 0x800);
	volatile uint8_t *ptr8;
	int i = 0, j;

	while (i < mlen) {
		uint32_t t = 0;
		for (j = 0; i < mlen && j < 32; j += 8)
			t |= msg[i++] << j;
		*ptr32++ = t;
	}

	MEM4(SFI_BASE + 0x10) = mlen;
	MEM4(SFI_BASE + 0x14) = rlen;
	{
		uint32_t val = MEM4(SFI_BASE) | 0x18;
		if (!qpi) val &= ~0x10;
		// brom code waits no more than 1ms
		while (!(MEM4(SFI_BASE + 8) & 0x100));
		MEM4(SFI_BASE) = val | 0xc;
	}
	while (!(MEM4(SFI_BASE) & 2));
	while (MEM4(SFI_BASE) & 1);
	MEM4(SFI_BASE) &= ~0x1c;

	ptr8 = (uint8_t*)(SFI_BASE + 0x800) + mlen;
	for (i = 0; i < rlen; i++) ret[i] = ptr8[i];
}

static unsigned spd_checksum(const void *src, int len) {
	uint16_t *s = (uint16_t*)src;
	uint32_t crc = 0;
	for (; len >= 2; len -= 2) crc += *s++;
	crc = (crc >> 16) + (crc & 0xffff);
	crc += crc >> 16;
	return ~crc & 0xffff;
}

static void cmd_custom_sfi(usbio_t *io) {
	uint16_t data[(4 + 256 + 6 + 2) / 2];
	uint8_t *buf = (uint8_t*)data + 4;
	unsigned mlen, rlen, mlen2;

	io->recv_buf(data, 4, 0);
	mlen = data[0] & 0x7fff; rlen = data[1];
	if (mlen + rlen > sizeof(data) - 6) for (;;);
	mlen2 = (mlen + 3) & ~1;
	io->recv_buf(buf, mlen2, 0);
	if (spd_checksum(data, 4 + mlen2)) for (;;);
	if (mlen + rlen)
		sfi_cmd(data[0] >> 15, buf, buf, mlen, rlen);
	if (rlen & 1) buf[rlen++] = 0;
	*(uint16_t*)&buf[rlen] = spd_checksum(buf, rlen);
	io->send_buf(buf, rlen + 2, 0);
}

