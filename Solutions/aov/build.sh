#!/bin/sh

set -e

SHELL_FOLDER=$(cd "$(dirname "$0")";pwd)
cd $SHELL_FOLDER

CUR_DIR_NAME=`basename "$SHELL_FOLDER"`

DO_CPRES=0
DO_ALL=0

warring() {
	echo "DESCRIPTION"
	echo "EASYEAI-1126B Solution Project."
	echo " "
	echo "./build.sh              : build aov"
	echo "./build.sh cpres        : copy to board + copy resources (不编译)"
	echo "./build.sh all          : build + copy to board + copy resources (同 cpres)"
	echo "./build.sh clear        : clear all compiled files"
	echo " "
}

# 解析参数 (大小写不敏感)
for arg in "$@"; do
	case "$(echo "$arg" | tr '[:upper:]' '[:lower:]')" in
		clear)
			rm -rf build
			rm -f Release/$CUR_DIR_NAME
			exit 0
			;;
		cpres)
			DO_CPRES=1
			;;
		all)
			DO_ALL=1
			DO_CPRES=1
			;;
	esac
done

# build this project (cpres 跳过编译，仅拷贝)
if [ "$DO_ALL" = "1" ] || [ "$DO_CPRES" = "0" ]; then
	rm -rf build && mkdir build && cd build

	# cmake 参数说明:
	#   USE_RKAIQ         : 启用 Rockchip AIQ ISP 调试框架 (链接 lib_rkaiq.so)
	#   ENABLE_AOV        : 启用 AOV 低功耗常亮宏 (CMake 传递 -DENABLE_AOV)
	#   OS_LINUX          : 目标系统 Linux (CMake 传递 -DOS_LINUX)
	cmake .. \
	  -DUSE_RKAIQ=ON \
	  -DENABLE_AOV=ON \
	  -DOS_LINUX=ON \
	  -DCMAKE_C_FLAGS="-Wno-error=format-security -Wno-unused-result -Wno-unused-function -Wno-format-truncation" \
	  -DCMAKE_CXX_FLAGS="-Wno-error=format-security -Wno-unused-result -Wno-unused-function -Wno-format-truncation"

	make -j24

	# make Release files
	chmod 777 $CUR_DIR_NAME
	mkdir -p "../Release" && cp $CUR_DIR_NAME "../Release"

	# 拷贝资源文件到 Release/（供 cpres/all 一并发布）
	if [ "$DO_CPRES" = "1" ]; then
		cp -f ../osd_aov.bmp "../Release/" 2>/dev/null || true
	fi
else
	echo "skip build, copy only"
fi

## copy to Board
if [ ! -d "$SYSROOT/userdata/Solu/$CUR_DIR_NAME" ]; then
	echo "mkdir $SYSROOT/userdata/Solu/$CUR_DIR_NAME"
	sudo mkdir -p $SYSROOT/userdata/Solu/$CUR_DIR_NAME
fi
if [ "$DO_CPRES" = "1" ]; then
	sudo cp ../Release/* $SYSROOT/userdata/Solu/$CUR_DIR_NAME
else
	sudo cp ../Release/$CUR_DIR_NAME $SYSROOT/userdata/Solu/$CUR_DIR_NAME
fi
