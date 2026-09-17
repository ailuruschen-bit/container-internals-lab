package main

import "syscall"

// Capability numbers we drop from the bounding set (Chapter 05). Dropping from
// the bounding set is what guarantees the container process (and anything it
// execs, even setuid-root) cannot regain them (Chapter 05 section 3).
const (
	capSysAdmin   = 21
	capSysModule  = 16
	capSysTime    = 25
	capSysBoot    = 22
	capSysRawio   = 17
	capSysPtrace  = 19
	capNetAdmin   = 12
	capMknod      = 27 // dropping this stops the container creating device nodes
	capSysResource = 24
	capSyslog     = 34
)

// prctl option numbers (from <linux/prctl.h>); not all are in std syscall.
const (
	prCapbsetDrop     = 24
	prSetNoNewPrivs   = 38
	prSetSeccomp      = 22
	seccompModeFilter = 2
)

func prctl(option, arg2, arg3, arg4, arg5 uintptr) error {
	_, _, errno := syscall.Syscall6(syscall.SYS_PRCTL, option, arg2, arg3, arg4, arg5, 0)
	if errno != 0 {
		return errno
	}
	return nil
}

// dropCapabilities removes a curated list of dangerous capabilities from the
// bounding set. A real runtime drops everything not in the OCI bounding set and
// also rewrites the effective/permitted/inheritable sets via capset(); here we
// keep it to the bounding set to stay dependency-free and readable.
func dropCapabilities() error {
	drop := []uintptr{
		capSysAdmin, capSysModule, capSysTime, capSysBoot, capSysRawio,
		capSysPtrace, capNetAdmin, capMknod, capSysResource, capSyslog,
	}
	for _, c := range drop {
		if err := prctl(prCapbsetDrop, c, 0, 0, 0); err != nil {
			// EINVAL for an unknown capability number is harmless; keep going.
			// EPERM means we lack CAP_SETPCAP (e.g. already unprivileged): report.
			if err == syscall.EPERM {
				return err
			}
		}
	}
	return nil
}

// setNoNewPrivs sets the one-way flag that stops execve() from granting new
// privileges, and lets us install seccomp without CAP_SYS_ADMIN (Chapter 06 §1).
func setNoNewPrivs() error {
	return prctl(prSetNoNewPrivs, 1, 0, 0, 0)
}
