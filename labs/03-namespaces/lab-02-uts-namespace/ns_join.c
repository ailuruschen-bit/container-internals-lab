// ns_join.c — a tiny nsenter: join one namespace by file, then exec a command.
//
// Build: gcc -Wall -o ns_join ns_join.c
// Run:   sudo ./ns_join /proc/<pid>/ns/uts hostname
//        sudo ./ns_join /proc/<pid>/ns/uts bash
//
// nstype 0 tells setns() to accept any namespace type; a real tool would pass
// the expected CLONE_NEW* flag to make sure the file is the right kind.
#define _GNU_SOURCE
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s /proc/<pid>/ns/<type> command [args...]\n", argv[0]);
        return 2;
    }
    int fd = open(argv[1], O_RDONLY | O_CLOEXEC);
    if (fd == -1) { perror("open"); return 1; }

    if (setns(fd, 0) == -1) { perror("setns"); return 1; }
    close(fd);

    // The namespace membership survives execve() (Chapter 01, section 3).
    execvp(argv[2], &argv[2]);
    perror("execvp");
    return 127;
}
