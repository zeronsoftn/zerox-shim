#!/bin/bash

set -e

SCRIPT_DIR=$(dirname -- "${BASH_SOURCE[0]}")
SCRIPT_DIR=$(realpath ${SCRIPT_DIR})
PARENT_DIR=$(dirname -- "${SCRIPT_DIR}")
WORK_DIR=$PWD/.tmp

[ -d "${WORK_DIR}" ] && rm -rf ${WORK_DIR} || true
mkdir -p ${WORK_DIR}

SHIM_EFI=$1
/bin/bash ${SCRIPT_DIR}/test-prepare.sh ${SHIM_EFI} 02

rm -rf ${WORK_DIR}/guest.in ${WORK_DIR}/guest.out
mkfifo ${WORK_DIR}/guest.in ${WORK_DIR}/guest.out

fallocate -l 32M ${WORK_DIR}/disk2.img
mkfs.vfat ${WORK_DIR}/disk2.img
touch ${WORK_DIR}/dummy
mmd -i ${WORK_DIR}/disk2.img EFI EFI/Test
mcopy -i ${WORK_DIR}/disk2.img ${WORK_DIR}/dummy ::.zerox-boot.abcd
mcopy -vi ${WORK_DIR}/disk2.img ${WORK_DIR}/onetime.efi ::EFI/Test/onetime.efi

function cleanup() {
  echo "cleanup"
  rm -f ${WORK_DIR}/guest.in ${WORK_DIR}/guest.out || true
  [ -f "${WORK_DIR}/qemu.pid" ] && kill $(cat "${WORK_DIR}/qemu.pid") || true
}
QEMU_MACHINE="q35,smm=on"
[ -w /dev/kvm ] && QEMU_MACHINE="${QEMU_MACHINE},accel=kvm" || true

qemu-system-x86_64 \
  -machine ${QEMU_MACHINE} \
  -cpu kvm64 \
  -pidfile ${WORK_DIR}/qemu.pid \
  -daemonize \
  -D /dev/stderr \
  -no-reboot \
  -serial pipe:${WORK_DIR}/guest \
  -global driver=cfi.pflash01,property=secure,value=on \
  -drive file=${PARENT_DIR}/OVMF_CODE_4M.secboot.fd,if=pflash,format=raw,unit=0,readonly=on \
  -drive file=${PARENT_DIR}/provisioned.vars.4m.fd,if=pflash,format=raw,unit=1,readonly=on \
  -m 256m \
  -drive if=virtio,file=${WORK_DIR}/efiboot.img,format=raw,readonly=off \
  -drive if=virtio,file=${WORK_DIR}/disk2.img,format=raw,readonly=off

trap cleanup EXIT

check_1=0
check_2=0
check_3=0

while read -r line; do
  echo "QEMU: ${line}"
  echo "${line}" | grep 'onetime boot to' && check_1=1 || true
  echo "${line}" | grep 'EFI stub: UEFI Secure Boot is enabled.' && check_2=1 || true
  echo "${line}" | grep 'Linux version' && check_3=1 && break || true
done < ${WORK_DIR}/guest.out

kill $(cat "${WORK_DIR}/qemu.pid") || true

[ ${check_1} -eq 1 ] && [ ${check_2} -eq 1 ] && [ ${check_3} -eq 1 ] && (echo "SUCCESS"; exit 0) || (echo "FAILED"; exit 1)
