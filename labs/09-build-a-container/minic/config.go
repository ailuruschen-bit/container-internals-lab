package main

import (
	"flag"
	"fmt"
	"os"
)

// config holds everything the parent decides and passes to the child through
// environment variables (a simple, explicit channel; runc uses a pipe + JSON).
type config struct {
	rootfs   string
	hostname string
	memBytes int64
	pidsMax  int64
	userns   bool
	netns    bool
	cmd      []string // the command to run inside, after "--"
}

func parseConfig(args []string) config {
	fs := flag.NewFlagSet("run", flag.ExitOnError)
	rootfs := fs.String("rootfs", "/tmp/rootfs", "rootfs to pivot into (\"\" keeps host root)")
	hostname := fs.String("hostname", "minic", "container hostname")
	mem := fs.Int64("mem", 0, "memory.max in bytes (0 = unlimited)")
	pids := fs.Int64("pids", 0, "pids.max (0 = unlimited)")
	userns := fs.Bool("userns", false, "create a user namespace (rootless)")
	netns := fs.Bool("net", false, "create a network namespace")

	// Split at "--": flags before, command after.
	var flagsArgs, cmd []string
	split := -1
	for i, a := range args {
		if a == "--" {
			split = i
			break
		}
	}
	if split >= 0 {
		flagsArgs = args[:split]
		cmd = args[split+1:]
	} else {
		flagsArgs = args
	}
	must("parse flags", fs.Parse(flagsArgs))
	if len(cmd) == 0 {
		cmd = []string{"/bin/sh"}
	}

	return config{
		rootfs:   *rootfs,
		hostname: *hostname,
		memBytes: *mem,
		pidsMax:  *pids,
		userns:   *userns,
		netns:    *netns,
		cmd:      cmd,
	}
}

// The child reads its configuration from these environment variables, set by the
// parent. Using env keeps the example simple and mirrors how a runtime passes an
// explicit, minimal environment to the container process (Chapter 01 section 3).
const (
	envRootfs   = "MINIC_ROOTFS"
	envHostname = "MINIC_HOSTNAME"
)

func (c config) childEnv() []string {
	return []string{
		envRootfs + "=" + c.rootfs,
		envHostname + "=" + c.hostname,
		// A minimal PATH so the shell can find programs inside the rootfs.
		"PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
		"PS1=minic:\\w# ",
		"TERM=" + os.Getenv("TERM"),
	}
}

func env(name string) string { return os.Getenv(name) }

func logf(format string, a ...any) {
	fmt.Fprintf(os.Stderr, "[minic] "+format+"\n", a...)
}
