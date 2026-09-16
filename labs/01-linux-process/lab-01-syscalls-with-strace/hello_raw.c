// hello_raw.c — the same output produced three ways.
//
// Build: gcc -Wall -o hello_raw hello_raw.c
// Run:   strace -e trace=write,openat ./hello_raw
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

int main(void) {
    // 1. Buffered C stdio. printf does not always call write() immediately;
    //    fflush forces the buffered bytes into one write() syscall.
    printf("1: via printf\n");
    fflush(stdout);

    // 2. The libc wrapper function write(). A thin wrapper around the syscall.
    const char msg2[] = "2: via libc write()\n";
    write(STDOUT_FILENO, msg2, sizeof(msg2) - 1);

    // 3. The generic syscall() function: pass the syscall number yourself.
    //    SYS_write is the architecture-specific number (1 on x86-64, 64 on arm64).
    const char msg3[] = "3: via syscall(SYS_write, ...)\n";
    syscall(SYS_write, STDOUT_FILENO, msg3, sizeof(msg3) - 1);

    // 4. A failing syscall. The kernel returns -ENOENT; libc turns that into
    //    a return value of -1 and sets errno.
    int fd = open("/this/path/does/not/exist", O_RDONLY);
    if (fd == -1) {
        printf("4: open failed: return=-1 errno=%d (%s)\n", errno, strerror(errno));
    }
    return 0;
}
