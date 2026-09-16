// seccomp_demo.c — install a seccomp filter with libseccomp, then run a command.
//
// The filter ALLOWS a small set of syscalls and applies a chosen default action
// to everything else, so you can watch what happens when a program calls a
// blocked syscall.
//
// Build: gcc -Wall -o seccomp_demo seccomp_demo.c -lseccomp
// Run:   ./seccomp_demo errno   /bin/echo hello      # blocked calls fail with EPERM
//        ./seccomp_demo kill    /bin/echo hello      # blocked calls kill the process
//        ./seccomp_demo errno   /usr/bin/uname -a    # uname() is blocked on purpose
//
// No root needed: the program sets no_new_privs first, so seccomp needs no capability.
#define _GNU_SOURCE
#include <errno.h>
#include <seccomp.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s errno|kill|log command [args...]\n", argv[0]);
        return 2;
    }

    uint32_t def;
    if (!strcmp(argv[1], "errno")) def = SCMP_ACT_ERRNO(EPERM);
    else if (!strcmp(argv[1], "kill")) def = SCMP_ACT_KILL_PROCESS;
    else if (!strcmp(argv[1], "log")) def = SCMP_ACT_LOG;   // allow but log
    else { fprintf(stderr, "mode must be errno|kill|log\n"); return 2; }

    // Not strictly required (seccomp_load sets it), but explicit for the lesson:
    prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);

    scmp_filter_ctx ctx = seccomp_init(def);   // default action for unlisted syscalls
    if (!ctx) { fprintf(stderr, "seccomp_init failed\n"); return 1; }

    // The minimum a dynamically linked program needs to start and run /bin/echo.
    int allow[] = {
        SCMP_SYS(execve), SCMP_SYS(brk), SCMP_SYS(arch_prctl), SCMP_SYS(access),
        SCMP_SYS(openat), SCMP_SYS(open), SCMP_SYS(read), SCMP_SYS(pread64),
        SCMP_SYS(write), SCMP_SYS(close), SCMP_SYS(fstat), SCMP_SYS(newfstatat),
        SCMP_SYS(mmap), SCMP_SYS(mprotect), SCMP_SYS(munmap), SCMP_SYS(set_tid_address),
        SCMP_SYS(set_robust_list), SCMP_SYS(rseq), SCMP_SYS(prlimit64),
        SCMP_SYS(getrandom), SCMP_SYS(exit_group), SCMP_SYS(exit),
        SCMP_SYS(rt_sigprocmask), SCMP_SYS(rt_sigaction),
    };
    for (size_t i = 0; i < sizeof(allow) / sizeof(*allow); i++) {
        if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, allow[i], 0) < 0)
            fprintf(stderr, "warn: could not add rule for syscall index %zu\n", i);
    }

    // NOTE: uname() is deliberately NOT in the list, so `uname` will be blocked.

    if (seccomp_load(ctx) < 0) {   // compiles to cBPF, sets no_new_privs, calls seccomp()
        fprintf(stderr, "seccomp_load failed\n");
        return 1;
    }
    seccomp_release(ctx);

    execvp(argv[2], &argv[2]);
    // If write is allowed, this message appears; the failing syscall is execvp's target's, not this.
    perror("execvp");
    return 127;
}
