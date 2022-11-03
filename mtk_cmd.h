enum {
	CMD_SEND_IMAGE         = 0x70,
	CMD_BOOT_IMAGE         = 0x71,
	CMD_STAY_STILL         = 0x80,

	CMD_LEGACY_WRITE       = 0xa1,
	CMD_LEGACY_READ        = 0xa2,

	CMD_I2C_INIT           = 0xb0,
	CMD_I2C_DEINIT         = 0xb1,
	CMD_I2C_WRITE8         = 0xb2,
	CMD_I2C_READ8          = 0xb3,
	CMD_I2C_SET_SPEED      = 0xb4,

	CMD_PWR_INIT           = 0xc4,
	CMD_PWR_DEINIT         = 0xc5,
	CMD_PWR_READ16         = 0xc6,
	CMD_PWR_WRITE16        = 0xc7,

	CMD_READ16             = 0xd0,
	CMD_READ32             = 0xd1,
	CMD_WRITE16            = 0xd2,
	CMD_WRITE16_NO_ECHO    = 0xd3,
	CMD_WRITE32            = 0xd4,
	CMD_JUMP_DA            = 0xd5,
	CMD_JUMP_BL            = 0xd6,
	CMD_SEND_DA            = 0xd7,
	CMD_GET_TARGET_CONFIG  = 0xd8,
	CMD_SEND_EPP           = 0xd9, // SEND_ENV_PREPARE
	CMD_UART1_LOG_EN       = 0xdb,
	CMD_SET_BAUD           = 0xdc,

	CMD_SEND_CERT          = 0xe0,
	CMD_GET_ME_ID          = 0xe1,
	CMD_SEND_AUTH          = 0xe2,
	CMD_GET_SOC_ID         = 0xe7,

	CMD_ZEROIZATION        = 0xf0,
	CMD_GET_PL_CAP         = 0xfb,
	CMD_GET_HW_SW_VER      = 0xfc,
	CMD_GET_HW_CODE        = 0xfd,
	CMD_GET_BL_VER         = 0xfe,
	CMD_GET_VERSION        = 0xff
};
