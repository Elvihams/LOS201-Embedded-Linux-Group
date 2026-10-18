/* ARM userspace test: calls all four ioctl commands from one shared header. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "asgn1_ioctl.h"

#define DEVICE "/dev/asgn1"

/* Stop on a failed system call; label identifies the operation. */
static void fail(const char *label)
{
    perror(label);
    exit(EXIT_FAILURE);
}

/* Open a fresh file descriptor, resetting the read offset. */
static int open_device(int flags)
{
    int fd = open(DEVICE, flags);
    if (fd < 0)
        fail("open /dev/asgn1");
    return fd;
}

/* Replace the driver's contents with a short test string. */
static void write_text(const char *message)
{
    size_t length = strlen(message);
    int fd = open_device(O_WRONLY);
    ssize_t actual = write(fd, message, length);
    if (actual != (ssize_t)length)
        fail("write");
    close(fd);
}

/* Read once from a fresh open and compare with expected bytes. */
static void expect_text(const char *expected)
{
    char actual[128] = {0};
    size_t length = strlen(expected);
    int fd = open_device(O_RDONLY);
    ssize_t n = read(fd, actual, sizeof(actual) - 1);
    if (n < 0)
        fail("read");
    if (n != (ssize_t)length || memcmp(actual, expected, length)) {
        fprintf(stderr, "FAIL: expected [%s] (%zu bytes), got [%s] (%zd bytes)\n",
                expected, length, actual, n);
        exit(EXIT_FAILURE);
    }
    if (n && read(fd, actual, sizeof(actual)) != 0) {
        fprintf(stderr, "FAIL: read did not reach EOF\n");
        exit(EXIT_FAILURE);
    }
    close(fd);
    printf("READ OK: [%s], length=%zd\n", expected, n);
}

/* Exercise version, mode, statistics and reset ioctl commands. */
int main(void)
{
    int fd = open_device(O_RDWR);
    int mode;
    char version[ASGN1_VERSION_SIZE] = {0};
    struct asgn1_stats stats = {0};

    if (ioctl(fd, ASGN1_GET_VERSION, version) < 0)
        fail("GET_VERSION");
    printf("GET_VERSION: %s\n", version);

    mode = 0;
    if (ioctl(fd, ASGN1_SET_MODE, &mode) < 0)
        fail("SET_MODE 0");
    printf("SET_MODE: 0 (prefix)\n");
    write_text("abc");
    expect_text("[KERNEL]abc");

    if (ioctl(fd, ASGN1_GET_STATS, &stats) < 0)
        fail("GET_STATS");
    printf("GET_STATS: open_count=%d write_count=%d read_count=%d "
           "buffer_len=%d last_write_size=%d\n", stats.open_count,
           stats.write_count, stats.read_count, stats.buffer_len,
           stats.last_write_size);
    if (stats.write_count < 1 || stats.read_count < 1 ||
        stats.buffer_len != 11 || stats.last_write_size != 3) {
        fprintf(stderr, "FAIL: GET_STATS fields inconsistent\n");
        return EXIT_FAILURE;
    }

    mode = 1;
    if (ioctl(fd, ASGN1_SET_MODE, &mode) < 0)
        fail("SET_MODE 1");
    printf("SET_MODE: 1 (echo)\n");
    mode = 2;
    errno = 0;
    if (ioctl(fd, ASGN1_SET_MODE, &mode) != -1 || errno != EINVAL) {
        fprintf(stderr, "FAIL: invalid mode should return EINVAL\n");
        return EXIT_FAILURE;
    }
    puts("INVALID MODE: EINVAL OK");
    write_text("xyz");
    expect_text("xyz");

    if (ioctl(fd, ASGN1_RESET_BUFFER) < 0)
        fail("RESET_BUFFER");
    printf("RESET_BUFFER: called\n");
    expect_text("");
    if (ioctl(fd, ASGN1_GET_STATS, &stats) < 0)
        fail("GET_STATS after reset");
    printf("AFTER RESET: open_count=%d write_count=%d read_count=%d "
           "buffer_len=%d last_write_size=%d\n", stats.open_count,
           stats.write_count, stats.read_count, stats.buffer_len,
           stats.last_write_size);
    if (stats.write_count || stats.read_count || stats.buffer_len ||
        stats.last_write_size) {
        fprintf(stderr, "FAIL: reset did not clear state\n");
        return EXIT_FAILURE;
    }

    errno = 0;
    if (ioctl(fd, _IO(ASGN1_IOC_MAGIC, 99)) != -1 || errno != ENOTTY) {
        fprintf(stderr, "FAIL: unknown ioctl should return ENOTTY\n");
        return EXIT_FAILURE;
    }
    printf("UNKNOWN COMMAND: ENOTTY OK\n");
    close(fd);
    puts("TASK 1 TEST PASSED");
    return EXIT_SUCCESS;
}
