/* ARM userspace test: verify partial reads share one counter increment. */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Report a failed syscall or assertion and stop. */
static void die(const char *message)
{
    perror(message);
    exit(EXIT_FAILURE);
}

/* Set a known value, then read the decimal line one byte at a time. */
int main(void)
{
    char text[32] = {0};
    char byte;
    ssize_t n;
    size_t length = 0;
    int fd = open("/dev/counter", O_WRONLY);
    FILE *proc;
    int current = -1, reads = -1, writes = -1;

    if (fd < 0)
        die("open counter for write");
    if (write(fd, "100\n", 4) != 4)
        die("write counter");
    close(fd);

    fd = open("/dev/counter", O_RDONLY);
    if (fd < 0)
        die("open counter for read");
    while ((n = read(fd, &byte, 1)) == 1) {
        if (length + 1 >= sizeof(text)) {
            fprintf(stderr, "FAIL: counter line too long\n");
            return EXIT_FAILURE;
        }
        text[length++] = byte;
    }
    if (n < 0)
        die("read one byte");
    close(fd);
    if (strcmp(text, "100\n")) {
        fprintf(stderr, "FAIL: got [%s], expected [100\\n]\n", text);
        return EXIT_FAILURE;
    }
    proc = fopen("/proc/counter_info", "r");
    if (!proc)
        die("open proc");
    if (fscanf(proc, "counter: %d\n", &current) != 1 ||
        fscanf(proc, "total_reads: %d\n", &reads) != 1 ||
        fscanf(proc, "total_writes: %d\n", &writes) != 1) {
        fprintf(stderr, "FAIL: malformed proc stats\n");
        return EXIT_FAILURE;
    }
    fclose(proc);
    if (current != 101) {
        fprintf(stderr, "FAIL: partial reads incremented counter more than once: %d\n",
                current);
        return EXIT_FAILURE;
    }
    printf("PARTIAL READ OK: line=[100\\n], counter=%d reads=%d writes=%d\n",
           current, reads, writes);
    return EXIT_SUCCESS;
}
