#!/bin/sh
# Run as root inside QEMU after placing counter_driver.ko in /lib/modules.
set -eu
MOD="/lib/modules/$(uname -r)/counter_driver.ko"
echo '=== TASK 2: misc counter device ==='
if grep -q '^counter_driver ' /proc/modules; then
    rmmod counter_driver
fi
insmod "$MOD"
if [ ! -c /dev/counter ]; then
    MINOR="$(awk '$2 == "counter" { print $1 }' /proc/misc)"
    [ -n "$MINOR" ] || { echo 'FAIL: no counter minor in /proc/misc'; exit 1; }
    mknod /dev/counter c 10 "$MINOR"
fi
chmod 600 /dev/counter
ls -l /dev/counter
echo '[1] Before write'
cat /proc/counter_info

FIRST="$(cat /dev/counter)"
SECOND="$(cat /dev/counter)"
[ "$FIRST" = '0' ] && [ "$SECOND" = '1' ] || {
    echo "FAIL: expected 0 then 1; got $FIRST then $SECOND"; exit 1;
}
echo "read sequence: $FIRST, $SECOND"

echo '[2] Set 100'
printf '100\n' > /dev/counter
cat /proc/counter_info
VALUE="$(cat /dev/counter)"
[ "$VALUE" = '100' ] || { echo "FAIL: expected 100, got $VALUE"; exit 1; }
echo "read after write: $VALUE"

echo '[3] Reset to zero'
printf '0\n' > /dev/counter
cat /proc/counter_info
VALUE="$(cat /dev/counter)"
[ "$VALUE" = '0' ] || { echo "FAIL: expected 0, got $VALUE"; exit 1; }
echo "read after reset: $VALUE"

echo '[4] Reject negative and invalid input'
if printf '%s\n' '-1' > /dev/counter 2>/dev/null; then
    echo 'FAIL: negative value accepted'; exit 1
fi
if printf '%s\n' 'abc' > /dev/counter 2>/dev/null; then
    echo 'FAIL: nonnumeric input accepted'; exit 1
fi
echo 'invalid inputs rejected'
cat /proc/counter_info

echo '[5] Partial reads: one increment per open'
/usr/bin/counter_test

rmmod counter_driver
dmesg | grep 'counter_driver:' | tail -n 15
echo 'TASK 2 SCRIPT PASSED'
