// minic — a minimal container runtime for learning, using only the Go standard
// library. It is NOT production software; it is the synthesis of Chapters 01-08.
//
// Usage:
//   sudo ./minic run [flags] -- /bin/sh
// Flags (see config.go):
//   -rootfs DIR     rootfs to pivot into (default /tmp/rootfs). "" = keep host root.
//   -hostname NAME  hostname inside the container (default "minic")
//   -mem BYTES      memory.max for the container cgroup (0 = unlimited)
//   -pids N         pids.max for the container cgroup (0 = unlimited)
//   -userns         create a user namespace and map root->caller (rootless)
//   -net            create a network namespace (isolated; only lo, down)
//
// It re-executes itself as the container's PID 1 via /proc/self/exe with a
// hidden "child" first argument (see docs section 1).
package main

import (
	"fmt"
	"os"
)

func main() {
	if len(os.Args) < 2 {
		usage()
	}
	switch os.Args[1] {
	case "run":
		parent(os.Args[2:])
	case "child": // internal: the re-executed self, already in new namespaces
		child(os.Args[2:])
	default:
		usage()
	}
}

func usage() {
	fmt.Fprintln(os.Stderr, "usage: minic run [flags] -- COMMAND [args...]")
	os.Exit(2)
}

func must(what string, err error) {
	if err != nil {
		fmt.Fprintf(os.Stderr, "minic: %s: %v\n", what, err)
		os.Exit(1)
	}
}
