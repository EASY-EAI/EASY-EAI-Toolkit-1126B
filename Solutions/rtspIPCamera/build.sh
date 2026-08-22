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
	echo "./build.sh cpres  : build and copy all resources to board"
	echo " "
}

# clear
if [ "$1" = "clear" ]; then
	rm -rf build
	rm -rf Release
	exit 0
fi

# build this project
rm -rf build && mkdir build && cd build
cmake ..
make -j24

# make Release files
mkdir -p "../Release" && cp $CUR_DIR_NAME "../Release"
chmod 777 ../Release -R

## copy to Board
sudo mkdir -p $SYSROOT/userdata/Solu/$CUR_DIR_NAME
sudo mkdir -p $SYSROOT/userdata/logs
if [ "$1" = "cpres" ]; then
	sudo cp ../Release/* $SYSROOT/userdata/Solu/$CUR_DIR_NAME
	sudo cp ../../../easyeai-api/common/log_manager_pro/log $SYSROOT/userdata/Solu/$CUR_DIR_NAME/
else
	sudo cp ../Release/$CUR_DIR_NAME $SYSROOT/userdata/Solu/$CUR_DIR_NAME
fi
