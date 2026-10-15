#!/bin/sh
# Kiểm tra bằng chứng và đóng gói đúng tên nộp bài; không tự tạo log/binary.
set -eu

cd "$(dirname "$0")"
root=$(pwd)
parent=$(dirname "$root")
name=$(basename "$root")
expected=SE203347_NguyenHuyHoang_Asgn01
if [ "$name" != "$expected" ]; then
    echo "Tên thư mục phải là $expected" >&2
    exit 1
fi

required='task1_ioctl/asgn1_driver.c
task1_ioctl/asgn1_ioctl.h
task1_ioctl/Makefile
task1_ioctl/asgn1_driver.ko
task1_ioctl/test_asgn1.sh
task2_misc/counter_driver.c
task2_misc/Makefile
task2_misc/counter_driver.ko
task2_misc/test_counter.sh
logs/test_asgn1_output.txt
logs/test_counter_output.txt
logs/dmesg_full.txt
bao_cao/SE203347_NguyenHuyHoang_Asgn01_BaoCao.pdf'
missing=0
oldifs=$IFS
IFS='
'
for item in $required; do
    if [ ! -s "$item" ]; then
        echo "Thiếu hoặc rỗng: $item" >&2
        missing=1
    fi
done
IFS=$oldifs
if [ "$missing" -ne 0 ]; then
    echo 'Hãy chép .ko từ Ubuntu và thu dmesg đầy đủ trong QEMU trước khi nén.' >&2
    exit 1
fi

for spec in 'logs/test_asgn1_output.txt:TASK 1 SCRIPT PASSED' \
            'logs/test_counter_output.txt:TASK 2 SCRIPT PASSED' \
            'logs/dmesg_full.txt:asgn1:' \
            'logs/dmesg_full.txt:counter_driver:'; do
    f=${spec%%:*}
    marker=${spec#*:}
    if ! grep -Fq "$marker" "$f"; then
        echo "Thiếu dấu mốc '$marker' trong $f" >&2
        exit 1
    fi
done

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp/$name/task1_ioctl" "$tmp/$name/task2_misc" \
         "$tmp/$name/logs" "$tmp/$name/bao_cao"
for item in $required; do
    cp "$root/$item" "$tmp/$name/$item"
done
IFS=$oldifs
# Giữ chương trình C test và file hướng dẫn để người chấm có thể tái lập.
for item in task1_ioctl/asgn1_test.c task2_misc/counter_test.c \
            logs/qemu_session.txt README.md; do
    [ ! -f "$item" ] || cp "$root/$item" "$tmp/$name/$item"
done

archive="$parent/$name.tar.gz"
tar -C "$tmp" -czf "$archive" "$name"
echo "Đã tạo: $archive"
echo "PDF nộp riêng: $root/bao_cao/SE203347_NguyenHuyHoang_Asgn01_BaoCao.pdf"
tar -tzf "$archive"
