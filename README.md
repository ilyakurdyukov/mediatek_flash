## MediaTek firmware dumper for Linux

Currently only for MT6260/MT6261 chipset. You can edit the code to work with other MediaTek chipsets.

* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, USE AT YOUR OWN RISK!

### Build

There are two options:

1. Using `libusb` for Linux and Windows (MSYS2):  
Use `make`, `libusb/libusb-dev` packages must be installed.

* For Windows users - please read how to install a [driver](https://github.com/libusb/libusb/wiki/Windows#driver-installation) for `libusb`.

2. Using the USB serial, **Linux only and doesn't work with smartphones**:  
Use `make LIBUSB=0`.
If you're using this mode, you must initialize the USB serial driver before using the tool (every boot):
```
$ sudo modprobe ftdi_sio
$ echo 0e8d 0003 | sudo tee /sys/bus/usb-serial/drivers/generic/new_id
```

* On Linux you must run the tool with `sudo`, unless you are using special udev rules (see below).

### Instructions

Run this command and connect your device to USB:
```
$ sudo ./mtk_dump connect  show_flash 1  read32 0 0x400000 dump.bin
```

* Where 0x400000 (4MB) is the expected length of flash in bytes (may be more or less).
* An example payload is [here](payload) (you can read the BROM with it).

#### Commands

Basic commands supported by the chip's boot ROM (BROM):

`bl_ver` - print bootloader version.  
`connect` - try to connect to the phone.  
`show_flash [0|1]` - maps SPI flash to memory at address 0.  
`reboot` - reboot the device.  
`get_meid` - print serial number.  
`read16 <addr> <size> <output_file>` - read memory in 16-bits chunks.  
`read32 <addr> <size> <output_file>` - read memory in 32-bits chunks.  
`legacy_read <addr> <size> <output_file>` - read memory in 16-bits chunks (legacy command).  
`send_da <file> <addr> <sig_len>` - load DA (Download Agent) at the specified memory address.  
`auto_da <file>` - parse DA headers, then load and execute DA.  
`jump_da <addr>` - execute code at the specified address.  
`simple_da <file> <addr>` - equivalent to `send_da <file> <addr> 0 jump_da <addr>`.  

The commands below require loading the payload binary that comes with the tool (using the command `simple_da payload.bin 0x70008000`).

* The payload binary supports only a subset of the commands listed above.

`flash_id` - info about SPI flash.  
`read_flash <addr> <size> <output_file>`  
`erase_flash <addr> <size>` - erases flash in 4K sectors.  
`write_flash <addr> <file_offset> <size> <input_file>` - zero size means until the end of the file.  

#### Using the tool without sudo

If you create `/etc/udev/rules.d/80-spd-mtk.rules` with these lines:
```
# Spreadtrum
SUBSYSTEMS=="usb", ATTRS{idVendor}=="1782", ATTRS{idProduct}=="4d00", MODE="0666", TAG+="uaccess"
# MediaTek
SUBSYSTEMS=="usb", ATTRS{idVendor}=="0e8d", ATTRS{idProduct}=="0003", MODE="0666", TAG+="uaccess"
```
...then you can run `mtk_dump` without root privileges.

* As you can see this file for both Spreadtrum and MediaTek chipsets.

### Useful links

1. [Fernly - some of the MT6260 reversed](https://github.com/xobs/fernly)
2. [MTKClient - tools for many MTK chipsets (in Python)](https://github.com/bkerler/mtkclient)
3. [decompressor for ALICE chunks in the firmware (in Python)](https://github.com/donnm/mtk_fw_tools)

* Also I have the [tool](https://github.com/ilyakurdyukov/spreadtrum_flash) for Spreadtrum chipsets.
