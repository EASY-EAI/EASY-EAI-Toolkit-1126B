#!/bin/sh

set -e

SHELL_FOLDER=$(cd "$(dirname "$0")";pwd)
cd $SHELL_FOLDER

CUR_DIR_NAME=`basename "$SHELL_FOLDER"`
BIN_NAME="${CUR_DIR_NAME}"
KO_DIR="${SHELL_FOLDER}/ko"
LIB_DIR="${SHELL_FOLDER}/lib"
JSON_DIR="${SHELL_FOLDER}/avs_json"
BOARD_BIN_DIR="/userdata/Solu/${CUR_DIR_NAME}"

usage() {
    echo "DESCRIPTION"
    echo "EASYEAI-1126B Solution Project - VI+AVS+VENC."
    echo " "
    echo "USAGE:"
    echo "  ./build.sh              : compile only (cmake + make -> Release/)"
    echo "  ./build.sh deploy       : deploy libs/json/ko to SYSROOT"
    echo "  ./build.sh all          : deploy + compile + copy files to SYSROOT"
    echo "  ./build.sh clear        : remove build/ and Release/"
    echo " "
    echo "ENVIRONMENT:"
    echo "  SYSROOT=path      : NFS rootfs path (required for deploy/all)"
    echo "                      eg: SYSROOT=/mnt ./build.sh all"
}

do_deploy() {
    if [ -z "${SYSROOT}" ]; then
        echo "Error: SYSROOT is not set."
        echo "  eg: SYSROOT=/mnt ./build.sh deploy"
        exit 1
    fi

    echo "Deploying libs to ${SYSROOT}/usr/lib/..."
    sudo cp -f ${LIB_DIR}/*.so "${SYSROOT}/usr/lib/"

    echo "Deploying avs_json to ${SYSROOT}/oem/usr/share/avs_json/..."
    sudo mkdir -p "${SYSROOT}/oem/usr/share/avs_json"
    sudo cp -f ${JSON_DIR}/* "${SYSROOT}/oem/usr/share/avs_json/"

    echo "deploy done."
}

do_build() {
    rm -rf build && mkdir build && cd build
    cmake .. \
        -DRKAIQ=ON \
        -DRKAIQ_GRP=ON \
        -DCMAKE_C_FLAGS="-Wno-unused-but-set-variable -Wno-unused-variable -Wno-unused-function -Wno-implicit-function-declaration" \
        -DCMAKE_CXX_FLAGS="-Wno-unused-but-set-variable -Wno-unused-variable -Wno-unused-function"
    make -j$(nproc)
    cd $SHELL_FOLDER

    mkdir -p Release
    find build -maxdepth 1 -type f -not -name "*.cmake" -not -name "Makefile" \
        | xargs -I{} cp {} Release/
    cp -f ${KO_DIR}/*.ko Release/
    chmod 777 Release -R
    echo "Build done. Binaries in Release/."

    ## copy to Board
    if [ -n "${SYSROOT}" ]; then
        mkdir -p "${SYSROOT}/${BOARD_BIN_DIR}"
        cp -f Release/$BIN_NAME "${SYSROOT}/${BOARD_BIN_DIR}/"
        cp -f Release/*.ko "${SYSROOT}/${BOARD_BIN_DIR}/"
        echo "Files deployed to ${SYSROOT}/${BOARD_BIN_DIR}/."
    fi
}

case "$1" in
    clear)
        rm -rf build Release
        echo "Cleaned."
        exit 0
        ;;
    deploy)
        do_deploy
        ;;
    all)
        do_deploy
        do_build
        ;;
    ""|-h|--help)
        if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
            usage
            exit 0
        fi
        do_build
        ;;
    *)
        echo "Unknown command: $1"
        usage
        exit 1
        ;;
esac

exit 0
