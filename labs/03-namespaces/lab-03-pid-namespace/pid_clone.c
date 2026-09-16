// pid_clone.c — a child as PID 1 of a new PID namespace, with its own /proc.
//
// Build: gcc -Wall -o pid_clone pid_clone.c
// Run:   sudo ./pid_clone
#define _GNU_SOURCE
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <unistd.h>

#define STACK_SIZE (1024 * 1024)

static void print_nspid(const char *who) {
    FILE *f = fopen("/proc/self/status", "r");
    char line[256];
    while (f && fgets(line, sizeof(line), f)) {
        if (strncmp(line, "NSpid:", 6) == 0) printf("%-6s %s", who, line);
    }
    if (f) fclose(f);
}

static int child_fn(void *arg) {
    (void)arg;
    // We are PID 1 of the new PID namespace, and in a new mount namespace.
    // Stop mount events from propagating back to the host (Chapter 02, section 4).
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) == -1) { perror("make-rprivate"); return 1; }

    printf("child  getpid()=%d getppid()=%d\n", getpid(), getppid());
    print_nspid("child"); // still the OLD /proc: shows the host's view of this process
    fflush(stdout);

    // Mount a procfs instance that belongs to this PID namespace.
    if (mount("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) == -1) {
        perror("mount proc");
        return 1;
    }
    print_nspid("child"); // new /proc: this namespace's view
    printf("child  processes visible through the new /proc:\n");
    fflush(stdout);
    execlp("ps", "ps", "-o", "pid,ppid,comm", NULL);
    perror("execlp");
    return 127;
}

int main(void) {
    char *stack = malloc(STACK_SIZE);
    if (!stack) { perror("malloc"); return 1; }

    printf("parent getpid()=%d\n", getpid());
    fflush(stdout);

    pid_t pid = clone(child_fn, stack + STACK_SIZE, CLONE_NEWPID | CLONE_NEWNS | SIGCHLD, NULL);
    if (pid == -1) { perror("clone (are you root?)"); return 1; }
    printf("parent sees the child as pid=%d\n", pid);
    fflush(stdout);

    waitpid(pid, NULL, 0);
    free(stack);
    return 0;
}
