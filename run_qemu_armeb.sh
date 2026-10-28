#!/bin/bash
# Собрать + запустить mini-DOPRA/TFTP под user-mode QEMU (big-endian ARM),
# используя sysroot прошивки как корень для динамического загрузчика. В WSL.
set -e
T=/mnt/d/project/huawei/packer/tools
D=$T/dopra_mini
bash "$D/build.sh"
echo
echo "=== запуск под qemu-armeb (BE ARM, glibc 2.29 из прошивки) ==="
"$D/bin/qemu-armeb" -L "$T/sysroot" "$D/dopra_tftp.out"
