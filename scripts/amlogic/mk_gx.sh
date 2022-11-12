#! /bin/bash

export CROSS_COMPILE=/opt/gcc-linaro-5.3-2016.02-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-

make ARCH=arm64 meson64_defconfig
make ARCH=arm64  -j8 Image  UIMAGE_LOADADDR=0x1080000 || echo "Compile Image Fail !!"

make ARCH=arm64  -j8 Image.gz  UIMAGE_LOADADDR=0x1080000 || echo "Compile Image Fail !!"


./scripts/amlogic/mk_dtb.sh
