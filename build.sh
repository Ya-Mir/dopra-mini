#!/bin/bash
# Кросс-сборка mini-DOPRA + TFTP как big-endian (BE8) ARMv7 Linux-бинаря,
# слинкованного с glibc 2.29 из прошивки (.so от БС). Запускать внутри WSL.
set -e
T=/mnt/d/project/huawei/packer/tools
CLANG=$T/clangroot/usr/bin/clang-18
LD=$T/binutils/usr/bin/arm-linux-gnueabihf-ld
export LD_LIBRARY_PATH=$T/binutils/usr/lib/x86_64-linux-gnu
SR=$T/sysroot/lib
D=$T/dopra_mini
cd "$D"

TGT="--target=armv7eb-linux-gnueabihf -march=armv7-a -mfloat-abi=soft -mbig-endian"
CFLAGS="$TGT -O2 -fno-stack-protector -fno-pie -fno-builtin -Wall -Wextra -I."

echo "[1] компиляция (clang-18 -> объекты BE8 ARM)"
OBJS=""
for f in start.S vos.c rosa.c tftp.c main.c; do
    o=${f%.*}.o
    "$CLANG" $CFLAGS -c "$f" -o "$o"
    OBJS="$OBJS $o"
done

echo "[2] линковка (GNU ld --be8, динамически, .so из sysroot прошивки)"
"$LD" --be8 -EB -no-pie -dynamic-linker /lib/ld-linux.so.3 -rpath-link "$SR" \
    $OBJS "$SR/libpthread.so.0" "$SR/libc.so.6" \
    -o dopra_tftp.out

echo "[3] проверка ELF"
python3 -c "d=open('dopra_tftp.out','rb').read(20); print('  ELF class=%d(1=32) data=%d(2=MSB/BE) machine=0x%02x%02x(0x0028=ARM)'%(d[4],d[5],d[18],d[19]))"
echo "BUILD_OK -> $D/dopra_tftp.out"
