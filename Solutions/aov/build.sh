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

if [ "$1" = "clear" ]; then
	rm -rf build
	rm -rf Release
	exit 0
fi

# build this project
rm -rf build && mkdir build && cd build
cmake .. \
  -DUSE_RKAIQ=ON \
  -DENABLE_COMMON_FUNCTIONS=ON \
  -DENABLE_AOV=ON \
  -DENABLE_FILE_CACHE=ON \
  -DENABLE_JPEG_SLICE=ON \
  -DENABLE_STORAGE=ON \
  -DENABLE_DISPLAY=ON \
  -DENABLE_PLAYER=ON \
  -DBUILD_EXAMPLES=ON \
  -DOS_LINUX=ON \
  -DRKADK_CHIP=rv1126b \
  -DCMAKE_C_FLAGS="$(CFLAGS) -Wno-error=format-security -Wno-unused-result -Wno-unused-function -Wno-format-truncation" \
  -DCMAKE_CXX_FLAGS="$(CXXFLAGS) -Wno-error=format-security -Wno-unused-result -Wno-unused-function -Wno-format-truncation"
make -j24

# make Release files
mkdir -p "../Release" && cp $CUR_DIR_NAME "../Release" && cp ../config/* "../Release"
chmod 755 ../Release -R

## copy to Board
mkdir -p $SYSROOT/userdata/Solu/
cp ../Release/* $SYSROOT/userdata/Solu
exit 0
