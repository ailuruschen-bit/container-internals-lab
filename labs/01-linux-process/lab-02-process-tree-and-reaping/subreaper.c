// subreaper.c — observe orphan reparenting to a child subreaper.
//
// Process layout created by this program:
//
//   subreaper (this program, marks itself PR_SET_CHILD_SUBREAPER)
//     └── middle      (exits immediately after creating grandchild)
//           └── grandchild  (becomes an orphan → reparented to subreaper)
//
// Build: gcc -Wall -o subreaper subreaper.c
// Run:   ./subreaper          (with the subreaper flag)
//        ./subreaper --no     (without it, for comparison)
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    int use_subreaper = !(argc > 1 && strcmp(argv[1], "--no") == 0);

    if (use_subreaper && prctl(PR_SET_CHILD_SUBREAPER, 1) == -1) {
        perror("prctl");
        return 1;
    }
    printf("[subreaper]  pid=%d  subreaper=%s\n", getpid(), use_subreaper ? "yes" : "no");
    fflush(stdout); // flush before fork so buffered text is not duplicated

    pid_t middle = fork();
    if (middle == 0) {
        pid_t grandchild = fork();
        if (grandchild == 0) {
            printf("[grandchild] pid=%d  ppid=%d (parent is middle)\n", getpid(), getppid());
            fflush(stdout);
            sleep(1); // give "middle" time to exit
            printf("[grandchild] pid=%d  ppid=%d (after middle exited)\n", getpid(), getppid());
            fflush(stdout);
            _exit(42);
        }
        printf("[middle]     pid=%d  exiting now\n", getpid());
        fflush(stdout);
        _exit(0);
    }

    // Reap every child that is (or becomes) ours until none are left.
    int status;
    pid_t pid;
    while ((pid = wait(&status)) > 0) {
        if (WIFEXITED(status)) {
            printf("[subreaper]  reaped pid=%d  exit status=%d\n", pid, WEXITSTATUS(status));
        }
    }
    printf("[subreaper]  no children left\n");
    return 0;
}
