#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.1.10
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-
SCRIPT_DIR="$( cd "$(dirname "${BASH_SOURCE[0]}")" ; pwd -P )"

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # Add your kernel build steps here
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} modules
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} dtbs
fi

echo "Adding the Image in outdir"
cp "${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image" "$OUTDIR"

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# Create necessary base directories
mkdir "$OUTDIR/rootfs"
cd "$OUTDIR/rootfs"
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # Configure busybox
    make distclean
    make defconfig
else
    cd busybox
fi

# Make and install busybox
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
make CONFIG_PREFIX=${OUTDIR}/rootfs ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install
cd "$OUTDIR/rootfs"

echo "Library dependencies"
${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"

# Add library dependencies to rootfs
# ${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter" | sed 's/^.*\///' | sed 's/.$//' | xargs -I '{}' find ${TOOLCHAIN_DIR} -name '{}' | xargs -I '{}' cp '{}' ${OUTDIR}/rootfs/lib64
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library" | sed 's/^.*: \[//' | sed 's/.$//' | xargs -I '{}' find ${TOOLCHAIN_DIR} -name '{}' | xargs -I '{}' cp '{}' ${OUTDIR}/rootfs/lib64
${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter" | sed 's/^.*\///' | sed 's/.$//' | xargs -I '{}' find ${TOOLCHAIN_DIR} -name '{}' | xargs -I '{}' cp '{}' ${OUTDIR}/rootfs/lib
# ${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library" | sed 's/^.*: \[//' | sed 's/.$//' | xargs -I '{}' find ${TOOLCHAIN_DIR} -name '{}' | xargs -I '{}' cp '{}' ${OUTDIR}/rootfs/lib

# Make device nodes
sudo mknod -m 666 dev/tty c 5 1
sudo mknod -m 666 dev/null c 1 5

# Clean and build the writer utility
cd $SCRIPT_DIR
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} clean
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all
cd ..

# Copy the finder related scripts and executables to the /home directory
# on the target rootfs
cp "$SCRIPT_DIR/"finder.sh "$OUTDIR/rootfs/home"
cp "$SCRIPT_DIR/"finder-test.sh "$OUTDIR/rootfs/home"
mkdir "$OUTDIR/rootfs/home/conf"
cp "$SCRIPT_DIR/conf/"*.txt "$OUTDIR/rootfs/home/conf"
cp "$SCRIPT_DIR/writer" "$OUTDIR/rootfs/home"
cp "$SCRIPT_DIR/autorun-qemu.sh" "$OUTDIR/rootfs/home"

# Chown the root directory
sudo chown root:root "$OUTDIR/rootfs"

# Create initramfs.cpio.gz
cd "$OUTDIR/rootfs"
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
cd "$OUTDIR"
gzip -f initramfs.cpio
