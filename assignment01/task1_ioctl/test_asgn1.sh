#!/bin/sh
# Execute inside QEMU, as root, after placing the module and asgn1_test binary.
set -eu
MOD="/lib/modules/$(uname -r)/asgn1_driver.ko"
echo '=== TASK 1: ioctl character driver ==='
if grep -q '^asgn1_driver ' /proc/modules; then
    rmmod asgn1_driver
fi
insmod "$MOD"
if [ ! -c /dev/asgn1 ]; then
    mknod /dev/asgn1 c 241 0
fi
chmod 600 /dev/asgn1
ls -l /dev/asgn1

echo '[1] Default echo mode: write and read'
printf 'hello' > /dev/asgn1
ACTUAL="$(cat /dev/asgn1)"
[ "$ACTUAL" = 'hello' ] || { echo "FAIL default echo: [$ACTUAL]"; exit 1; }
echo "read: [$ACTUAL]"

echo '[2] ioctl tests, both modes and reset'
/usr/bin/asgn1_test

echo '[3] Buffer must be empty after RESET'
[ -z "$(cat /dev/asgn1)" ] || { echo 'FAIL reset'; exit 1; }
echo 'empty buffer OK'

echo '[4] Unload and inspect cleanup log'
rmmod asgn1_driver
dmesg | grep 'asgn1:' | tail -n 30
echo 'TASK 1 SCRIPT PASSED'
