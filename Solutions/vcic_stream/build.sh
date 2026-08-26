#!/bin/sh

set -e

SHELL_FOLDER=$(cd "$(dirname "$0")";pwd)
cd $SHELL_FOLDER

CUR_DIR_NAME=`basename "$SHELL_FOLDER"`

# clear
if [ "$1" = "clear" ]; then
	rm -rf build
	rm -rf Release
	exit 0
fi

# build
rm -rf build && mkdir build && cd build
cmake ..
make -j24

# release
mkdir -p "../Release"
cp vcic_stream_server vcic_stream_client "../Release/"
cp ../../../easyeai-api/common/log_manager_pro/log "../Release/"
chmod 755 ../Release -R

## copy to Board
if [ -n "$SYSROOT" ]; then
	sudo mkdir -p $SYSROOT/userdata/Solu/$CUR_DIR_NAME
	sudo cp ../Release/* $SYSROOT/userdata/Solu/$CUR_DIR_NAME
fi
