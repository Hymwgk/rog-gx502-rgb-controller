#!/usr/bin/env bash
set -euo pipefail
sudo apt-get update
sudo apt-get install --no-install-recommends gcc-arm-none-eabi binutils-arm-none-eabi libnewlib-dev openocd python3
# OpenOCD's packaged udev rules grant access to supported debug adapters.
sudo udevadm control --reload-rules
echo '安装完成。请重新插拔 ST-LINK，然后运行 ./build.sh。'
