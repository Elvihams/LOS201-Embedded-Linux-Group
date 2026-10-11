# ASSIGNMENT-01 — triển khai trên QEMU vexpress-a9

Bộ nguồn này dựa trên cấu trúc driver LAB-02 của Nguyễn Huy Hoàng
(major 240, kernel 5.15, ARMv7), nhưng có hai module riêng. Hai script đã
chạy PASS trong QEMU theo transcript sinh viên gửi. Thư mục trao đổi này có
source, báo cáo PDF, log hai script và transcript; **chưa chứa** hai `.ko`,
binary test ARM, initramfs đã boot trên máy sinh viên, hoặc `dmesg_full.txt`.
Các artifact biên dịch ở trên Ubuntu của sinh viên phải được chép vào bộ nộp.
Không đổi tên binary host thành binary ARM hoặc gọi trích đoạn dmesg là log đầy đủ.

## 1. Những file đã có

- `task1_ioctl/asgn1_driver.c`, `asgn1_ioctl.h`: driver và ABI dùng chung.
- `task1_ioctl/asgn1_test.c`: chương trình C gọi bốn ioctl trong QEMU.
- `task1_ioctl/test_asgn1.sh`: nạp, kiểm thử và gỡ Task 1.
- `task2_misc/counter_driver.c`: misc device + `/proc/counter_info`.
- `task2_misc/counter_test.c`, `test_counter.sh`: kiểm tra đọc từng byte,
  nạp, kiểm thử và gỡ Task 2.
- `bao_cao/SE203347_NguyenHuyHoang_Asgn01_BaoCao.pdf`: báo cáo 15 trang,
  trình bày cả bốn task và phụ lục output test.
- `logs/test_asgn1_output.txt`, `logs/test_counter_output.txt`: output tách
  từ transcript QEMU; `logs/qemu_session.txt` là transcript gốc.
- `logs/dmesg_excerpt.txt`: riêng các dòng driver đã thu được, **không phải**
  bản dump đầy đủ của kernel ring buffer.

## 2. Build trên Ubuntu host

Theo LAB-01, mặc định Makefile trỏ đến
`~/embedded_lab1/kernel/linux-5.15`. Nếu lần làm lại của bạn dùng đường dẫn
khác, thay `KDIR` bằng cây **đã build** của đúng kernel QEMU:
Hai Makefile dùng `ccflags-y += -march=armv7-a`, giống Makefile LAB-02 của
bạn, để code atomic ARMv7 được biên dịch với đúng instruction set. Kiểm tra
`CONFIG_CPU_V7=y` và `CONFIG_ARCH_VEXPRESS=y` trong `.config` trước khi build.

```sh
KDIR="$HOME/embedded_lab1/kernel/linux-5.15"
test -f "$KDIR/.config" && test -f "$KDIR/include/generated/autoconf.h"
make -C task1_ioctl KDIR="$KDIR"
make -C task2_misc KDIR="$KDIR"
file task1_ioctl/asgn1_driver.ko task1_ioctl/asgn1_test task2_misc/counter_driver.ko task2_misc/counter_test
modinfo task1_ioctl/asgn1_driver.ko | grep vermagic
modinfo task2_misc/counter_driver.ko | grep vermagic
```

Mong đợi cả bốn binary là ARM 32-bit; `vermagic` phải khớp `uname -r` bên trong
QEMU. Giữ `.ko` để nộp; `make clean` sẽ xóa chúng. Nếu `-static` của chương
trình userspace thất bại vì thiếu thư viện ARM static, cài gói development
ARM của toolchain trên host rồi build lại.

## 3. Đưa chương trình vào rootfs hiện có

Ví dụ nếu rootfs LAB-02 thực tế của bạn ở `$LABROOT/rootfs/initramfs`, tạo
bản sao làm việc để giữ nguyên bài LAB-02. Thay đường dẫn cho đúng máy:

```sh
LABROOT="$HOME/embedded_lab_redo"
cp -a "$LABROOT/rootfs/initramfs" ./rootfs_assignment
KVER="$(make -s -C "$KDIR" kernelrelease ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-)"
mkdir -p "rootfs_assignment/lib/modules/$KVER" rootfs_assignment/usr/bin
cp task1_ioctl/asgn1_driver.ko task2_misc/counter_driver.ko "rootfs_assignment/lib/modules/$KVER/"
cp task1_ioctl/asgn1_test task1_ioctl/test_asgn1.sh task2_misc/counter_test task2_misc/test_counter.sh rootfs_assignment/usr/bin/
chmod +x rootfs_assignment/usr/bin/asgn1_test rootfs_assignment/usr/bin/test_asgn1.sh rootfs_assignment/usr/bin/counter_test rootfs_assignment/usr/bin/test_counter.sh
mkdir -p output
(cd rootfs_assignment && find . -print | cpio -H newc -o | gzip > ../output/initramfs_assignment.cpio.gz)
```

Lưu ý: những lệnh này chỉ chạy sau khi `make` thành công. `LABROOT` trong ví dụ
là đường dẫn làm lại từng được dùng; kiểm tra nó tồn tại trước khi chạy.
Nếu đã có `lab2_driver` tự load trong `rcS`, cứ để nó hoạt động riêng ở major
240. Assignment nạp `asgn1_driver` ở 241 và misc driver ở major 10.

Boot bằng `zImage` và `.dtb` của **cùng cây kernel** đã build module. Ví dụ
LAB-01:

```sh
qemu-system-arm -M vexpress-a9 -cpu cortex-a9 -m 512M -smp 2 -nographic \
  -kernel "$HOME/embedded_lab1/output/zImage" \
  -dtb "$HOME/embedded_lab1/output/vexpress-v2p-ca9.dtb" \
  -initrd "$PWD/output/initramfs_assignment.cpio.gz" \
  -append 'console=ttyAMA0,115200 rdinit=/sbin/init mem=512M'
```

Trước khi chạy, xác nhận `zImage`/`.dtb` có thật và kernel version khớp.
Trong QEMU chạy `uname -r`, `test_asgn1.sh`, `test_counter.sh`. Mỗi script
chủ động unload module cũ trước khi nạp để giá trị khởi đầu nhất quán; không
chạy khi process khác đang giữ device mở.

## 4. Thu bằng chứng thật và đóng gói

Hai file output của script và transcript gốc đã có. Để bổ sung dump dmesg
đúng yêu cầu, trên Ubuntu host bắt đầu `script -f logs/dmesg_capture_session.txt`
rồi boot QEMU như mục 3. Trong guest chạy hai test script, sau đó chạy
`echo DMESG_BEGIN; dmesg; echo DMESG_END`. Thoát QEMU bằng `Ctrl+A`, `X`,
rồi thoát `script` bằng `exit`. Trên host tách các dòng giữa hai marker:

```sh
awk '/^DMESG_BEGIN/{p=1;next}/^DMESG_END/{p=0}p' logs/dmesg_capture_session.txt \
  | sed 's/\r//g' > logs/dmesg_full.txt
```

Kiểm tra đầu/cuối file và sửa nếu transcript chèn ký tự điều khiển/echo lệnh.

Lưu ba bản chụp `/proc/counter_info`: trước write, sau write 100, sau reset 0.
Trong báo cáo, ghi **giá trị thực đã thấy**, kể cả lỗi và cách sửa. Không điền
phần kết quả bằng output dự đoán trong README.

Sau khi chép `.ko`, binary ARM, initramfs và dump dmesg từ máy Ubuntu,
đóng gói `SE203347_NguyenHuyHoang_Asgn01.tar.gz` và nộp thêm PDF riêng
`SE203347_NguyenHuyHoang_Asgn01_BaoCao.pdf`. Kiểm tra nội dung bằng
`tar -tzf SE203347_NguyenHuyHoang_Asgn01.tar.gz` trước khi nộp.
Chạy `./finalize_submission.sh` tại thư mục bài để kiểm tra file bắt buộc và
tạo archive ở thư mục cha. Script sẽ dừng và liệt kê file còn thiếu; nó không
tạo binary hoặc thay log thật bằng dữ liệu mẫu.

Đề bài quy định không nộp nguyên code do AI tạo. Trước khi nộp, tự kiểm tra,
chỉnh sửa và giải thích từng phần code, ghi nguồn tham khảo đúng quy định môn.

## 5. Quy ước thiết kế cần trình bày khi vấn đáp

- Task 1: một write thay thế buffer; read_count đếm read có dữ liệu, không
  đếm EOF; last_write_size là số byte user gửi, không tính tiền tố.
- RESET xóa buffer, `write_count`, `read_count`, `last_write_size`; giữ
  `open_count` và mode. Mặc định mode 1. Prefix mode 0 là `[KERNEL]`.
- Task 2: total_reads đếm một lần lấy giá trị trên mỗi lần mở thiết bị, kể cả
  khi ứng dụng đọc chuỗi theo nhiều mảnh. Giá trị trả về là giá trị **trước**
  khi tăng. Mỗi write hợp lệ tăng total_writes một lần.
- `atomic_t` cho primitive của counter; mutex giữ cả chuỗi read/increment
  hoặc parse/set để giá trị và thống kê nhất quán khi có nhiều process.
