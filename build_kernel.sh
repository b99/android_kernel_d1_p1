#!/bin/bash
##############################################################################
#
#                           Kernel Build Script
#
##############################################################################
# 2014-06-21 Shev_t       : modified
# 2011-10-24 effectivesky : modified
# 2010-12-29 allydrop     : created
##############################################################################

##############################################################################
# set toolchain
##############################################################################
export ARCH=arm
export SUBARCH=arm
export CROSS_COMPILE=~/AndroidSources/linaro-4.9.4-a9/bin/arm-eabi-
export LOCALVERSION="-31_stable"
export CCACHE_DIR=~/.ccache/kernel31
export MY_CONFIG=front_defconfig
ccache -M 5G

##############################################################################
# set variables
##############################################################################
export KERNELDIR=`pwd`
KERNEL_OUT=$KERNELDIR/obj/KERNEL_OBJ
STRIP=${CROSS_COMPILE}strip

##############################################################################
# make zImage
##############################################################################
mkdir -p $KERNEL_OUT
mkdir -p $KERNEL_OUT/tmp/kernel
mkdir -p $KERNEL_OUT/tmp/system/lib/modules

make O=$KERNEL_OUT $MY_CONFIG
make -j10 O=$KERNEL_OUT

if [ -f $KERNEL_OUT/arch/arm/boot/zImage ]
then
    cp -f $KERNEL_OUT/arch/arm/boot/zImage ./
    mv -f $KERNEL_OUT/arch/arm/boot/zImage $KERNEL_OUT/tmp/kernel/zImage
fi

if [ -f ./zImage ]
then
    ###########################
    # make SGX module if need #
    ###########################
    SGX_MODULE=`grep "CONFIG_PVR_SGX=y" $KERNEL_OUT/.config`
    if [ ! "$SGX_MODULE" ]
    then
        make clean -C $KERNELDIR/pvr-source/eurasiacon/build/linux2/omap4430_android
        cp $KERNELDIR/drivers/video/omap2/omapfb/omapfb.h $KERNEL_OUT/drivers/video/omap2/omapfb/omapfb.h
        make -j10 -C $KERNELDIR/pvr-source/eurasiacon/build/linux2/omap4430_android ARCH=arm KERNELDIR=$KERNEL_OUT TARGET_PRODUCT="blaze_tablet" BUILD=release TARGET_SGX=540 PLATFORM_VERSION=4.0
        mv $KERNELDIR/pvr-source/eurasiacon/binary2_540_120_omap4430_android_release/target/kbuild/pvrsrvkm_sgx540_120.ko $KERNEL_OUT
        $STRIP --strip-unneeded $KERNEL_OUT/pvrsrvkm_sgx540_120.ko
        make clean -C $KERNELDIR/pvr-source/eurasiacon/build/linux2/omap4430_android
        rm -r $KERNELDIR/pvr-source/eurasiacon/binary2_540_120_omap4430_android_release
        cp $KERNEL_OUT/pvrsrvkm_sgx540_120.ko ./pvrsrvkm_sgx540_120.ko
        mv $KERNEL_OUT/pvrsrvkm_sgx540_120.ko $KERNEL_OUT/tmp/system/lib/modules/pvrsrvkm_sgx540_120.ko
    fi

    CURRENT_DATE=`date +%Y%m%d-%H%M`
    KERNEL_FNAME=kernel$LOCALVERSION-$CURRENT_DATE.zip
    cp ./android/blank_any_kernel.zip $KERNEL_FNAME
    pushd $KERNEL_OUT/tmp
    zip -r ../../../$KERNEL_FNAME ./kernel/
    if [ ! "$SGX_MODULE" ]
    then
        zip -r ../../../$KERNEL_FNAME ./system/
    fi
fi
