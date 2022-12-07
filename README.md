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
* An example payload is [here](payload) (you can read the BROM with it).

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

#### Using libusb to connect

You can build the tool with `libusb` method: `make LIBUSB=1`

For Linux users, this method doesn't require the `ftdi_sio` kernel module, but `libusb/libusb-dev` packages must be installed.

For Windows users, this method is the only one available, should also require drivers (the same as needed for flashing tools).

### Useful links

1. [Fernly - some of the MT6260 reversed](https://github.com/xobs/fernly)
2. [MTKClient - tools for many MTK chipsets (in Python)](https://github.com/bkerler/mtkclient)
3. [decompressor for ALICE chunks in the firmware (in Python)](https://github.com/donnm/mtk_fw_tools)

* Also I have the [tool](https://github.com/ilyakurdyukov/spreadtrum_flash) for Spreadtrum chipsets.
