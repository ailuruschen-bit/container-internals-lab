package main

import (
	"os"
	"path/filepath"
	"strconv"
)

// cgroup manages a single cgroup v2 directory for the container (Chapter 04).
type cgroup struct {
	root string // e.g. /sys/fs/cgroup/minic-1234
}

func newCgroup(name string) *cgroup {
	return &cgroup{root: filepath.Join("/sys/fs/cgroup", name)}
}

// setup creates the cgroup, writes the limits, and moves the child (pid) into
// it. Because membership is inherited across fork (Chapter 04 section 2), every
// process the container later creates is automatically contained.
func (c *cgroup) setup(cfg config, pid int) error {
	// Ensure the controllers we need are delegated to children of the root.
	// This may fail without privilege; the caller treats that as "skip cgroups".
	_ = os.WriteFile("/sys/fs/cgroup/cgroup.subtree_control", []byte("+memory +pids"), 0644)

	if err := os.Mkdir(c.root, 0755); err != nil && !os.IsExist(err) {
		return err
	}
	if cfg.memBytes > 0 {
		if err := c.write("memory.max", strconv.FormatInt(cfg.memBytes, 10)); err != nil {
			return err
		}
	}
	if cfg.pidsMax > 0 {
		if err := c.write("pids.max", strconv.FormatInt(cfg.pidsMax, 10)); err != nil {
			return err
		}
	}
	// Move the container's PID 1 into the cgroup.
	return c.write("cgroup.procs", strconv.Itoa(pid))
}

func (c *cgroup) write(file, value string) error {
	return os.WriteFile(filepath.Join(c.root, file), []byte(value), 0644)
}

// cleanup removes the cgroup after the container exits. rmdir only succeeds when
// the cgroup has no processes and no children (Chapter 04 section 2).
func (c *cgroup) cleanup() {
	// Best effort: kill any stragglers, then remove.
	_ = os.WriteFile(filepath.Join(c.root, "cgroup.kill"), []byte("1"), 0644)
	_ = os.Remove(c.root)
}
