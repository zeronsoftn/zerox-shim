#!/bin/bash

set -e

SCRIPT_DIR=$(dirname -- "${BASH_SOURCE[0]}")
SCRIPT_DIR=$(realpath ${SCRIPT_DIR})
PARENT_DIR=$(dirname -- "${SCRIPT_DIR}")
WORK_DIR=$PWD/.tmp

[ -d "${WORK_DIR}" ] && rm -rf ${WORK_DIR} || true
mkdir -p ${WORK_DIR}

SHIM_EFI=$1
TESTCASE=$2

sbsign --key ${PARENT_DIR}/platform-secure-boot.key --cert ${PARENT_DIR}/platform-secure-boot.crt ${SHIM_EFI} --output ${WORK_DIR}/shimx64.efi
sbsign --key ${PARENT_DIR}/shim-vendor.key --cert ${PARENT_DIR}/shim-vendor.crt ${PARENT_DIR}/vmlinuz.unsigned --output ${WORK_DIR}/vmlinuz.signed

echo -n "console=ttyS0" > "${WORK_DIR}/cmdline.txt"

objcopy \
	--add-section .linux=${WORK_DIR}/vmlinuz.signed --change-section-vma .linux=0x2000000 \
	--add-section .cmdline=${WORK_DIR}/cmdline.txt --change-section-vma .cmdline=0x30000 \
	${PARENT_DIR}/linuxx64.efi.stub \
	${WORK_DIR}/onetime.efi.unsigned

sbsign --key ${PARENT_DIR}/shim-vendor.key --cert ${PARENT_DIR}/shim-vendor.crt ${WORK_DIR}/onetime.efi.unsigned --output ${WORK_DIR}/onetime.efi

fallocate -l 32M ${WORK_DIR}/efiboot.img
mkfs.vfat ${WORK_DIR}/efiboot.img
mmd -i ${WORK_DIR}/efiboot.img EFI EFI/BOOT EFI/ZeronsoftN EFI/ZeronsoftN/driverx64
mcopy -vi ${WORK_DIR}/efiboot.img ${WORK_DIR}/shimx64.efi ::EFI/BOOT/BOOTX64.EFI
mcopy -vi ${WORK_DIR}/efiboot.img ${SCRIPT_DIR}/case${TESTCASE}.zerox-boot.cfg ::zerox-boot.cfg
mcopy -vi ${WORK_DIR}/efiboot.img ${SCRIPT_DIR}/zerox-boot.onetime ::zerox-boot.onetime
mcopy -vi ${WORK_DIR}/efiboot.img ${WORK_DIR}/onetime.efi ::EFI/onetime.efi
