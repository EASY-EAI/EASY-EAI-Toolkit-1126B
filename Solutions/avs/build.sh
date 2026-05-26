#!/bin/sh

set -e

SHELL_FOLDER=$(cd "$(dirname "$0")";pwd)
cd $SHELL_FOLDER

CUR_DIR_NAME=`basename "$SHELL_FOLDER"`
warring() {
	echo "DESCRIPTION"
	echo "EASYEAI-1126B Solution Project."
	echo " "
	echo "./build.sh       : build solution"
	echo "./build.sh clear : clear all compiled files(just preserve source code)"
	echo " "
}

# clear
if [ "$1" = "clear" ]; then
	rm -rf build
	rm Release/$CUR_DIR_NAME -f
	exit 0
fi

# build this project
rm -rf build && mkdir build && cd build
# cmake 参数说明:
#   USE_RKAIQ         : 启用 Rockchip AIQ ISP 调试框架 (链接 lib_rkaiq.so)
#   ENABLE_AOV        : 启用 AOV 低功耗常亮宏 (CMake 传递 -DENABLE_AOV)
#   ENABLE_FILE_CACHE : 启用文件缓存支持
#   OS_LINUX          : 目标系统 Linux (CMake 传递 -DOS_LINUX)
cmake .. \
  -DUSE_RKAIQ=ON \
  -DENABLE_AOV=ON \
  -DENABLE_FILE_CACHE=ON \
  -DOS_LINUX=ON \
  -DCMAKE_C_FLAGS="-Wno-error=format-security -Wno-unused-result -Wno-unused-function -Wno-format-truncation" \
  -DCMAKE_CXX_FLAGS="-Wno-error=format-security -Wno-unused-result -Wno-unused-function -Wno-format-truncation"
make -j24

# make Release files
chmod 777 $CUR_DIR_NAME
mkdir -p "../Release" && cp $CUR_DIR_NAME "../Release"

## copy to Board
mkdir -p $SYSROOT/userdata/Solu/$CUR_DIR_NAME
if [ "$1" = "cpres" ]; then
	cp ../Release/* $SYSROOT/userdata/Solu/$CUR_DIR_NAME
else
	cp ../Release/$CUR_DIR_NAME $SYSROOT/userdata/Solu/$CUR_DIR_NAME
fi
