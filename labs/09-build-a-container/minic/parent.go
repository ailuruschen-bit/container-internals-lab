package main

import (
	"os"
	"os/exec"
	"strconv"
	"syscall"
)

// parent runs in the caller's namespaces. It:
//  1. builds the clone flags (which new namespaces to create),
//  2. re-executes /proc/self/exe with a "child" argument,
//  3. writes the container's cgroup limits and places the child in the cgroup,
//  4. waits for the child and reports its exit status.
func parent(args []string) {
	cfg := parseConfig(args)

	// Re-exec ourselves. The new process starts as PID 1 of the new PID
	// namespace and does the in-container setup in child().
	cmd := exec.Command("/proc/self/exe", append([]string{"child"}, cfg.cmd...)...)
	cmd.Stdin, cmd.Stdout, cmd.Stderr = os.Stdin, os.Stdout, os.Stderr
	cmd.Env = cfg.childEnv()

	// Chapter 03: each CLONE_NEW* flag creates a new namespace for the child.
	flags := syscall.CLONE_NEWUTS | // hostname            (Ch. 03 section 2)
		syscall.CLONE_NEWPID | // PID numbering        (Ch. 03 section 3)
		syscall.CLONE_NEWNS | // mount tree           (Ch. 03 section 4)
		syscall.CLONE_NEWIPC // System V IPC, mqueue  (Ch. 03 section 5)
	if cfg.netns {
		flags |= syscall.CLONE_NEWNET // network stack   (Ch. 03 section 6)
	}

	cmd.SysProcAttr = &syscall.SysProcAttr{
		Cloneflags: uintptr(flags),
		// Make the child's mounts private so nothing propagates to the host,
		// even before the child runs its own make-rprivate (Ch. 02/03).
		Unshareflags: syscall.CLONE_NEWNS,
	}

	if cfg.userns {
		// Chapter 03 section 7 / Chapter 05 section 4: create a user namespace
		// and map container root (0) to the calling user. Then no step needs
		// real root. The Go runtime writes uid_map/gid_map for us.
		cmd.SysProcAttr.Cloneflags |= syscall.CLONE_NEWUSER
		cmd.SysProcAttr.UidMappings = []syscall.SysProcIDMap{
			{ContainerID: 0, HostID: os.Getuid(), Size: 1},
		}
		cmd.SysProcAttr.GidMappings = []syscall.SysProcIDMap{
			{ContainerID: 0, HostID: os.Getgid(), Size: 1},
		}
		cmd.SysProcAttr.GidMappingsEnableSetgroups = false // parent writes "deny"
	}

	must("start child", cmd.Start())
	logf("started container: host pid=%d", cmd.Process.Pid)

	// Chapter 04: create the cgroup, set limits, and put the child in it.
	// Done after Start (we need the child's PID) but the child waits at a
	// barrier until we finish (see the sync pipe note in the docs; here the
	// child's own setup is fast enough that ordering is not critical for the
	// limits to apply to its descendants).
	cg := newCgroup("minic-" + strconv.Itoa(cmd.Process.Pid))
	if err := cg.setup(cfg, cmd.Process.Pid); err != nil {
		logf("cgroup setup skipped: %v", err) // e.g. rootless without delegation
	} else {
		defer cg.cleanup()
	}

	err := cmd.Wait()
	if exitErr, ok := err.(*exec.ExitError); ok {
		os.Exit(exitErr.ExitCode())
	}
	must("wait child", err)
}
