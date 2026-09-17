package main

import (
	"os"
	"os/exec"
	"path/filepath"
	"syscall"
)

// child runs as PID 1 inside the new namespaces. It performs the in-container
// setup "in the gap" (Chapter 01 section 3) and then execve()s the command.
func child(cmd []string) {
	hostname := env(envHostname)
	rootfs := env(envRootfs)

	// Chapter 03 section 2: our own UTS namespace, so this only names us.
	must("sethostname", syscall.Sethostname([]byte(hostname)))

	if rootfs != "" {
		must("setup rootfs", setupRootfs(rootfs))
	} else {
		// No rootfs: at least give ourselves a fresh /proc for the PID namespace.
		must("make-rprivate", mountMakeRPrivate())
		_ = syscall.Unmount("/proc", syscall.MNT_DETACH)
		must("mount proc", mountProc("/proc"))
	}

	// Chapters 05-06: reduce privilege just before exec.
	must("drop capabilities", dropCapabilities())
	must("no_new_privs", setNoNewPrivs())
	if err := installSeccomp(); err != nil {
		logf("seccomp skipped: %v", err)
	}

	// Resolve the command against the (new) PATH and execve it. From here on the
	// process IS the container's PID 1 running the target program.
	path, err := exec.LookPath(cmd[0])
	if err != nil {
		path = cmd[0] // fall back to the literal path
	}
	must("execve", syscall.Exec(path, cmd, os.Environ()))
}

// setupRootfs implements the Chapter 07 sequence: private propagation, self-bind
// the rootfs, mount proc/dev/sys under it, pivot_root, detach the old root.
func setupRootfs(rootfs string) error {
	if err := mountMakeRPrivate(); err != nil {
		return err
	}
	// pivot_root needs new_root to be a mount point: bind rootfs onto itself.
	if err := syscall.Mount(rootfs, rootfs, "", syscall.MS_BIND|syscall.MS_REC, ""); err != nil {
		return err
	}

	// Pseudo-filesystems, mounted under the new root before pivoting.
	must("mkdir proc", os.MkdirAll(filepath.Join(rootfs, "proc"), 0555))
	must("mkdir sys", os.MkdirAll(filepath.Join(rootfs, "sys"), 0555))
	must("mkdir dev", os.MkdirAll(filepath.Join(rootfs, "dev"), 0755))
	must("mkdir oldroot", os.MkdirAll(filepath.Join(rootfs, "oldroot"), 0700))

	if err := mountProc(filepath.Join(rootfs, "proc")); err != nil {
		return err
	}
	// sysfs read-only; ignore failure (needs privileges over the net ns owner).
	_ = syscall.Mount("sysfs", filepath.Join(rootfs, "sys"), "sysfs",
		syscall.MS_RDONLY|syscall.MS_NOSUID|syscall.MS_NODEV|syscall.MS_NOEXEC, "")
	// A tmpfs /dev with a couple of device nodes; nodes may need CAP_MKNOD.
	_ = syscall.Mount("tmpfs", filepath.Join(rootfs, "dev"), "tmpfs", syscall.MS_NOSUID, "mode=0755")
	makeDevNodes(rootfs)

	// pivot_root(new_root=".", put_old="oldroot") after chdir into the rootfs.
	if err := os.Chdir(rootfs); err != nil {
		return err
	}
	if err := pivotRoot(".", "oldroot"); err != nil {
		return err
	}
	if err := os.Chdir("/"); err != nil {
		return err
	}
	// Detach the old host root: lazy unmount, then remove the mount point.
	if err := syscall.Unmount("/oldroot", syscall.MNT_DETACH); err != nil {
		return err
	}
	return os.Remove("/oldroot")
}

func mountMakeRPrivate() error {
	return syscall.Mount("", "/", "", syscall.MS_REC|syscall.MS_PRIVATE, "")
}

func mountProc(target string) error {
	return syscall.Mount("proc", target, "proc",
		syscall.MS_NOSUID|syscall.MS_NODEV|syscall.MS_NOEXEC, "")
}

func pivotRoot(newRoot, putOld string) error {
	return syscall.PivotRoot(newRoot, putOld)
}

// makeDevNodes creates a few standard device nodes in the new /dev. Failures are
// non-fatal: without CAP_MKNOD (e.g. some rootless setups) these are skipped and
// the shell still runs, it just cannot use those devices.
func makeDevNodes(rootfs string) {
	dev := filepath.Join(rootfs, "dev")
	nodes := []struct {
		name       string
		major, min uint32
	}{
		{"null", 1, 3}, {"zero", 1, 5}, {"random", 1, 8}, {"urandom", 1, 9}, {"tty", 5, 0},
	}
	for _, n := range nodes {
		p := filepath.Join(dev, n.name)
		_ = syscall.Mknod(p, syscall.S_IFCHR|0o666, int(mkdev(n.major, n.min)))
	}
}

func mkdev(major, minor uint32) uint32 {
	// Encode a device number the way the kernel expects for these small numbers.
	return (major << 8) | minor
}
