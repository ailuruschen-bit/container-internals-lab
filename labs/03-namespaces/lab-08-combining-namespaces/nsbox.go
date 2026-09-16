// nsbox — run a command in new user, UTS, PID, mount, IPC, and network
// namespaces, using only the Go standard library. No root required (on systems
// that allow unprivileged user namespaces).
//
// Build: go build -o nsbox nsbox.go
// Run:   ./nsbox /bin/bash
//
// Every field of SysProcAttr below corresponds to a Linux mechanism from
// Chapter 03; see docs/03-namespaces/08-combining-namespaces.md.
package main

import (
	"errors"
	"fmt"
	"os"
	"os/exec"
	"syscall"
)

func main() {
	if len(os.Args) < 2 {
		fmt.Fprintln(os.Stderr, "usage: nsbox command [args...]")
		os.Exit(2)
	}

	cmd := exec.Command(os.Args[1], os.Args[2:]...)
	cmd.Stdin, cmd.Stdout, cmd.Stderr = os.Stdin, os.Stdout, os.Stderr

	// envp for execve(): only what we pass here (Chapter 01, section 3).
	cmd.Env = []string{
		"PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
		"PS1=nsbox# ",
		"TERM=" + os.Getenv("TERM"),
	}

	cmd.SysProcAttr = &syscall.SysProcAttr{
		// Flags for the raw clone() syscall. The kernel creates the user
		// namespace first, so the other five are owned by it.
		Cloneflags: syscall.CLONE_NEWUSER |
			syscall.CLONE_NEWUTS |
			syscall.CLONE_NEWPID |
			syscall.CLONE_NEWNS |
			syscall.CLONE_NEWIPC |
			syscall.CLONE_NEWNET,

		// Written by THIS (parent) process to /proc/<child>/uid_map and gid_map
		// while the child waits on a pipe, before the child calls execve().
		UidMappings: []syscall.SysProcIDMap{{ContainerID: 0, HostID: os.Getuid(), Size: 1}},
		GidMappings: []syscall.SysProcIDMap{{ContainerID: 0, HostID: os.Getgid(), Size: 1}},

		// false → the parent writes "deny" to /proc/<child>/setgroups first,
		// which an unprivileged gid_map write requires.
		GidMappingsEnableSetgroups: false,
	}

	if err := cmd.Start(); err != nil {
		fmt.Fprintf(os.Stderr, "nsbox: start: %v\n", err)
		os.Exit(1)
	}
	fmt.Fprintf(os.Stderr, "nsbox: parent pid=%d started child with host pid=%d\n", os.Getpid(), cmd.Process.Pid)

	err := cmd.Wait()
	var exitErr *exec.ExitError
	switch {
	case err == nil:
		os.Exit(0)
	case errors.As(err, &exitErr):
		os.Exit(exitErr.ExitCode())
	default:
		fmt.Fprintf(os.Stderr, "nsbox: wait: %v\n", err)
		os.Exit(1)
	}
}
