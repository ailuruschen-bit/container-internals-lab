// fd_inherit.c — shared offsets after fork, and close-on-exec at execve.
//
//   fd A: opened normally          → should survive execve
//   fd B: opened with O_CLOEXEC    → should be closed by execve
//
// Build: gcc -Wall -o fd_inherit fd_inherit.c
// Run:   ./fd_inherit
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static void read_some(const char *who, int fd, int n) {
    char buf[64] = {0};
    ssize_t got = read(fd, buf, n);
    off_t pos = lseek(fd, 0, SEEK_CUR); // current offset of the open file description
    printf("%-7s read %zd bytes \"%s\" from fd %d, offset now %lld\n",
           who, got, buf, fd, (long long)pos);
    fflush(stdout);
}

int main(void) {
    const char *path = "/tmp/fd_inherit_data.txt";
    FILE *f = fopen(path, "w");
    if (!f) { perror("fopen"); return 1; }
    fputs("0123456789ABCDEFGHIJ", f);
    fclose(f);

    int fd_a = open(path, O_RDONLY);             // inheritable
    int fd_b = open(path, O_RDONLY | O_CLOEXEC);  // closed on execve
    if (fd_a == -1 || fd_b == -1) { perror("open"); return 1; }
    printf("parent  pid=%d fd_a=%d (no CLOEXEC) fd_b=%d (CLOEXEC)\n", getpid(), fd_a, fd_b);
    fflush(stdout);

    pid_t pid = fork();
    if (pid == 0) {
        // Part 1: the child reads from the SAME open file description.
        read_some("child", fd_a, 5);
        _exit(0);
    }
    waitpid(pid, NULL, 0);

    // The parent never read from fd_a, yet its offset has moved.
    read_some("parent", fd_a, 5);

    pid = fork();
    if (pid == 0) {
        // Part 2: after execve, which of our descriptors still exist?
        // /proc/self/fd is listed by the NEW program (ls), not by this code.
        printf("child   pid=%d exec'ing ls; expect fd %d present, fd %d absent\n",
               getpid(), fd_a, fd_b);
        fflush(stdout);
        execlp("ls", "ls", "-l", "/proc/self/fd", (char *)NULL);
        perror("execlp");
        _exit(127);
    }
    waitpid(pid, NULL, 0);
    return 0;
}
