// chroot_escape.c — demonstrate the classic chroot escape.
//
// A privileged process that keeps access to a directory outside the new root
// (here, by not chdir-ing into it) can climb back to the real root.
//
// Build: gcc -Wall -o chroot_escape chroot_escape.c
// Run:   sudo ./chroot_escape /tmp/rootfs
//        (compare with the "correct" use in the README)
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    const char *jail = argc > 1 ? argv[1] : "/tmp/rootfs";

    if (chroot(jail) != 0) { perror("chroot"); return 1; }
    // Deliberately DO NOT chdir("/") into the jail: cwd is still the old root.
    printf("chrooted into %s; cwd is still outside the new root\n", jail);

    // Climb up: from a cwd above the new root, ".." keeps going toward the real /.
    for (int i = 0; i < 1024; i++) {
        if (chdir("..") != 0) { perror("chdir"); return 1; }
    }
    // Re-root at the current directory, which is now the real filesystem root.
    if (chroot(".") != 0) { perror("chroot(.)"); return 1; }

    printf("escaped. Listing the REAL root filesystem:\n");
    fflush(stdout);
    execl("/bin/ls", "ls", "-1", "/", (char *)NULL);
    // If the jail's /bin/ls is not the host's, this may fail; try /usr/bin/ls.
    perror("execl");
    return 127;
}
