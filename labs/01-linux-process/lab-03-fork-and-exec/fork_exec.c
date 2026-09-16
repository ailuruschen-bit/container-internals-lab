// fork_exec.c — create, configure in the gap, execute.
//
// The child:
//   1. modifies a variable (to show that memory was copied, not shared)
//   2. redirects its stdout to a file        (configuration in the gap)
//   3. changes its working directory to /tmp (configuration in the gap)
//   4. execve()s /bin/sh with a tiny, explicit environment
//
// The new program reports its PID, working directory, and environment,
// proving which properties survived execve().
//
// Build: gcc -Wall -o fork_exec fork_exec.c
// Run:   ./fork_exec
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static int counter = 0;

int main(void) {
    const char *out_path = "/tmp/fork_exec_output.txt";

    printf("[parent] pid=%d counter=%d\n", getpid(), counter);
    fflush(stdout); // otherwise buffered text would be copied into the child

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        // ---- child, still running this program ----
        counter = 100;
        printf("[child]  pid=%d ppid=%d counter=%d (about to configure and exec)\n",
               getpid(), getppid(), counter);
        fflush(stdout);

        int fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) { perror("open"); _exit(127); }
        if (dup2(fd, STDOUT_FILENO) == -1) { perror("dup2"); _exit(127); } // fd 1 → file
        close(fd);

        if (chdir("/tmp") == -1) { perror("chdir"); _exit(127); }

        char *argv[] = {
            "sh", "-c",
            "echo \"[exec'd] pid=$$ cwd=$(pwd)\"; "
            "echo \"[exec'd] GREETING=${GREETING:-<unset>} HOME=${HOME:-<unset>}\"; "
            "exit 7",
            NULL,
        };
        char *envp[] = { "GREETING=hello-from-the-gap", NULL };

        execve("/bin/sh", argv, envp);
        perror("execve"); // only reached if execve failed
        _exit(127);
    }

    // ---- parent ----
    int status;
    if (waitpid(pid, &status, 0) == -1) {
        perror("waitpid");
        return 1;
    }
    printf("[parent] child %d exited with status %d\n", pid, WEXITSTATUS(status));
    printf("[parent] counter in parent is still %d\n", counter);

    printf("[parent] contents of %s:\n", out_path);
    fflush(stdout);
    execlp("cat", "cat", out_path, (char *)NULL); // the parent can exec too
    perror("execlp");
    return 1;
}
