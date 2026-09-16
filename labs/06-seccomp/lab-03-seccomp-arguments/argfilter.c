// argfilter.c — a seccomp filter that matches syscall ARGUMENTS.
//
// It blocks clone/clone3/unshare that request a new user namespace
// (CLONE_NEWUSER in the flags argument), and allows everything else.
// This shows both the power (inspect register args) and the limit
// (cannot inspect the clone3 struct pointed to by clone3's argument).
//
// Build: gcc -Wall -o argfilter argfilter.c -lseccomp
// Run:   ./argfilter /bin/sh -c 'unshare -U echo hi; echo "exit: $?"'
#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <seccomp.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc < 2) { fprintf(stderr, "usage: %s command [args...]\n", argv[0]); return 2; }

    prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
    scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_ALLOW);   // allow by default
    if (!ctx) return 1;

    // clone(): the flags are argument 0 on most libc wrappers passing to the
    // raw syscall. Block when CLONE_NEWUSER is set in the masked flags.
    seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(clone), 1,
                     SCMP_A0(SCMP_CMP_MASKED_EQ, CLONE_NEWUSER, CLONE_NEWUSER));
    // unshare(): flags are argument 0.
    seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(unshare), 1,
                     SCMP_A0(SCMP_CMP_MASKED_EQ, CLONE_NEWUSER, CLONE_NEWUSER));
    // clone3(): flags live INSIDE a struct pointed to by argument 0, which a
    // seccomp filter cannot dereference. The best a filter can do is block the
    // whole syscall so callers fall back to clone(). We return ENOSYS so glibc
    // retries with clone().
    seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOSYS), SCMP_SYS(clone3), 0);

    if (seccomp_load(ctx) < 0) { fprintf(stderr, "seccomp_load failed\n"); return 1; }
    seccomp_release(ctx);

    execvp(argv[1], &argv[1]);
    perror("execvp");
    return 127;
}
