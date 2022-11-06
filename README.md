## MediaTek firmware dumper for Linux

Currently only for MT6260/MT6261 chipset. You can edit the code to work with other MediaTek chipsets.

* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, USE AT YOUR OWN RISK!

### Instructions

1. Initialize the USB serial driver:
```
$ sudo modprobe ftdi_sio
$ echo 0e8d 0003 | sudo tee /sys/bus/usb-serial/drivers/generic/new_id
```
2. Run this command and connect your device to USB:
```
$ sudo ./mtk_dump connect  show_flash 1  read32 0 0x400000 dump.bin
```

* Where 0x400000 (4MB) is the expected length of flash in bytes (may be more or less).

### Useful links

1. [Fernly - some of the MT6260 reversed](https://github.com/xobs/fernly)
2. [MTKClient - tools for many MTK chipsets (in Python)](https://github.com/bkerler/mtkclient)
3. [decompressor for ALICE chunks in the firmware (in Python)](https://github.com/donnm/mtk_fw_tools)
