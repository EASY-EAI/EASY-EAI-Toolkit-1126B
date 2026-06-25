#!/bin/sh

set -e

SHELL_FOLDER=$(cd "$(dirname "$0")";pwd)
cd $SHELL_FOLDER

CUR_DIR_NAME=`basename "$SHELL_FOLDER"`

# clear
if [ "$1" = "clear" ]; then
	rm -rf build
	rm -f Release/$CUR_DIR_NAME
	exit 0
fi

# 部署 IQ/AIBNR 模型到板端（仅 res，不含可执行文件）
deploy_res() {
	# 部署 IQ json (ainr 版，aibnr.en=1)
	IQDIR=$SYSROOT/etc/iqfiles
	mkdir -p $IQDIR
	cp $SHELL_FOLDER/res/iqfiles/sc450ai_CRK4F4209_styleTstP0.json $IQDIR/
	cp $SHELL_FOLDER/res/iqfiles/sc450ai_CRK4F4209_styleTstP1.json $IQDIR/
	cp $SHELL_FOLDER/res/iqfiles/sc450ai_CRK4F4209_styleTstP2.json $IQDIR/
	# 软链：rkaiq 按 sensor module 查找 sc450ai_default_default.json
	ln -sf sc450ai_CRK4F4209_styleTstP0.json $IQDIR/sc450ai_default_default.json

	# 部署 AIBNR 模型 blob
	MODELDIR=$IQDIR/sc450ai/bnr/combo_x1_G8
	mkdir -p $MODELDIR
	cp $SHELL_FOLDER/res/iqfiles/sc450ai/bnr/combo_x1_G8/*.bin $MODELDIR/

	echo "res deployed to $IQDIR"
}

# 拷贝可执行文件到板端
deploy_bin() {
	BOARD_SOLU=$SYSROOT/userdata/Solu/$CUR_DIR_NAME
	mkdir -p $BOARD_SOLU
	cp $SHELL_FOLDER/Release/$CUR_DIR_NAME $BOARD_SOLU
	echo "binary deployed to $BOARD_SOLU"
}

# deploy only — 只部署 res，不编译
if [ "$1" = "deploy" ]; then
	deploy_res
	exit 0
fi

# build this project
rm -rf build && mkdir build && cd build

cmake .. \
  -DUSE_RKAIQ=ON \
  -DENABLE_FILE_CACHE=ON \
  -DOS_LINUX=ON \
  -DCMAKE_C_FLAGS="-Wno-error=format-security -Wno-unused-result -Wno-unused-function -Wno-format-truncation" \
  -DCMAKE_CXX_FLAGS="-Wno-error=format-security -Wno-unused-result -Wno-unused-function -Wno-format-truncation"
make -j24

# make Release files
chmod 777 $CUR_DIR_NAME
mkdir -p "../Release" && cp $CUR_DIR_NAME "../Release"

# 编译后拷贝可执行文件到板端（无入参或入参为 all 均执行）
cd $SHELL_FOLDER
deploy_bin

# all = 部署 res + 编译 + 拷贝可执行文件
if [ "$1" = "all" ]; then
	deploy_res
fi
