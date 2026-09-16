// clone_flags.c — the same child function run with and without sharing flags.
//
// The child function changes three things:
//   - a global variable          (memory:     CLONE_VM)
//   - its working directory      (fs context: CLONE_FS)
//   - opens a new file descriptor (fd table:  CLONE_FILES)
//
// After the child exits, the parent checks whether it can see those changes.
//
// Build: gcc -Wall -o clone_flags clone_flags.c
// Run:   ./clone_flags copy      # flags = SIGCHLD                       (like fork)
//        ./clone_flags share     # flags = CLONE_VM|CLONE_FS|CLONE_FILES|SIGCHLD
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define STACK_SIZE (1024 * 1024)

static int counter = 0;

// Runs in the child. Deliberately avoids stdio: with CLONE_VM the child shares
// the parent's memory, including stdio buffers and locks.
// The return value becomes the child's exit status: we return the fd number.
static int child_fn(void *arg) {
    (void)arg;
    counter = 42;
    if (chdir("/") == -1) return 255;
    int fd = open("/etc/hostname", O_RDONLY);
    return fd == -1 ? 255 : fd;
}

int main(int argc, char *argv[]) {
    if (argc != 2 || (strcmp(argv[1], "copy") != 0 && strcmp(argv[1], "share") != 0)) {
        fprintf(stderr, "usage: %s copy|share\n", argv[0]);
        return 2;
    }
    int share = strcmp(argv[1], "share") == 0;
    int flags = SIGCHLD; // signal the parent receives when the child exits
    if (share) flags |= CLONE_VM | CLONE_FS | CLONE_FILES;

    if (chdir("/tmp") == -1) { perror("chdir"); return 1; }

    // clone() requires the caller to provide the child's stack.
    // On x86-64 and arm64 the stack grows downward, so pass its top address.
    char *stack = malloc(STACK_SIZE);
    if (!stack) { perror("malloc"); return 1; }

    printf("mode=%s  parent pid=%d\n", argv[1], getpid());
    printf("before: counter=%d cwd=/tmp\n", counter);
    fflush(stdout);

    pid_t pid = clone(child_fn, stack + STACK_SIZE, flags, NULL);
    if (pid == -1) { perror("clone"); return 1; }

    int status;
    if (waitpid(pid, &status, 0) == -1) { perror("waitpid"); return 1; }

    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) strcpy(cwd, "?");

    int child_fd = WEXITSTATUS(status); // the fd number the child opened
    if (child_fd == 255) { fprintf(stderr, "child failed\n"); return 1; }
    int fd_open_in_parent = fcntl(child_fd, F_GETFD) != -1; // F_GETFD fails with EBADF if not open

    printf("child pid=%d opened fd %d\n", pid, child_fd);
    printf("after:  counter=%d cwd=%s fd %d in parent: %s\n",
           counter, cwd, child_fd, fd_open_in_parent ? "OPEN" : "not open (EBADF)");
    free(stack);
    return 0;
}
