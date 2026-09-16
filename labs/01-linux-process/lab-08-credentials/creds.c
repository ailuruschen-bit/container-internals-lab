// creds.c — print the credentials the kernel holds for this process,
// then try to open a root-only file.
//
// Build: gcc -Wall -o creds creds.c
// Run:   ./creds [file-to-open]      (default: /etc/shadow)
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <stdio.h>
#include <string.h>
#include <sys/fsuid.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    const char *path = argc > 1 ? argv[1] : "/etc/shadow";
    uid_t ruid, euid, suid;
    gid_t rgid, egid, sgid;
    getresuid(&ruid, &euid, &suid);
    getresgid(&rgid, &egid, &sgid);
    // setfsuid(-1) is a trick: an invalid value changes nothing and returns the current fsuid.
    uid_t fsuid = setfsuid(-1);

    gid_t groups[64];
    int n = getgroups(64, groups);

    printf("pid=%d\n", getpid());
    printf("uid: real=%u effective=%u saved=%u fs=%u\n", ruid, euid, suid, fsuid);
    printf("gid: real=%u effective=%u saved=%u\n", rgid, egid, sgid);
    printf("supplementary groups (%d):", n);
    for (int i = 0; i < n; i++) printf(" %u", groups[i]);
    printf("\n");

    int fd = open(path, O_RDONLY);
    if (fd == -1) {
        printf("open(%s): %s\n", path, strerror(errno));
    } else {
        printf("open(%s): OK (fd %d)\n", path, fd);
    }
    return 0;
}
