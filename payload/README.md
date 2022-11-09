## Payload code source

Tested ONLY on MT6260/MT6261. Doesn't have own USB code, so depends on the API found in BROM. This may not work if your BROM is much different from the ones I've seen.

### Usage

* BROM is located at 0xfff00000.
* BROM use on-chip RAM at 0x70000000, which is also used for preloader.

An example of how to read BROM:
```
sudo ./mtk_dump connect \
	send_da payload.bin 0x70008000 0 jump_da 0x70008000 \
	read32 0xfff00000 0x10000 brom.bin
```

### Build

#### with GCC from the old NDK

* GCC has been removed since r18, and hasn't updated since r13. But sometimes it makes the smallest code.

```
NDK=$HOME/android-ndk-r15c
SYSROOT=$NDK/platforms/android-21/arch-arm
TOOLCHAIN=$NDK/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64/bin/arm-linux-androideabi

make all TOOLCHAIN="$TOOLCHAIN" SYSROOT="$SYSROOT"
```

#### with Clang from the old NDK

* NDK, SYSROOT, TOOLCHAIN as before.

```
CLANG="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/clang -target armv7-none-linux-androideabi -gcc-toolchain $NDK/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64"

make all TOOLCHAIN="$TOOLCHAIN" SYSROOT="$SYSROOT" CC="$CLANG"
```

#### with Clang from the newer NDK

```
NDK=$HOME/android-ndk-r25b
TOOLCHAIN=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm
CLANG=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi21-clang

make all TOOLCHAIN=$TOOLCHAIN CC=$CLANG
```

