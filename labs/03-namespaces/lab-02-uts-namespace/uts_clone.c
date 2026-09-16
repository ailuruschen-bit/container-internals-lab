// uts_clone.c — create a child in a new UTS namespace with clone().
//
// Build: gcc -Wall -o uts_clone uts_clone.c
// Run:   sudo ./uts_clone container-1
//
// The child sets its hostname and sleeps, so you can inspect it from another
// terminal (for example with ./ns_join). The parent prints its own hostname
// to prove it was not changed.
#define _GNU_SOURCE
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#define STACK_SIZE (1024 * 1024)

static void print_uts(const char *who) {
    struct utsname u;
    char link[64] = {0};
    uname(&u);
    readlink("/proc/self/ns/uts", link, sizeof(link) - 1);
    printf("%-6s pid=%d nodename=%s release=%s ns=%s\n", who, getpid(), u.nodename, u.release, link);
    fflush(stdout);
}

static int child_fn(void *arg) {
    const char *name = arg;
    if (sethostname(name, strlen(name)) == -1) {
        perror("sethostname");
        return 1;
    }
    print_uts("child");
    sleep(60); // keep the namespace alive for inspection
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <new-hostname>\n", argv[0]);
        return 2;
    }
    char *stack = malloc(STACK_SIZE);
    if (!stack) { perror("malloc"); return 1; }

    print_uts("parent");

    // The same clone() as Chapter 01 Lab 04, plus one flag: CLONE_NEWUTS.
    pid_t pid = clone(child_fn, stack + STACK_SIZE, CLONE_NEWUTS | SIGCHLD, argv[1]);
    if (pid == -1) { perror("clone (are you root?)"); return 1; }
    printf("parent created child pid=%d\n", pid);
    fflush(stdout);

    sleep(1); // let the child set its hostname first
    print_uts("parent");

    waitpid(pid, NULL, 0);
    free(stack);
    return 0;
}
