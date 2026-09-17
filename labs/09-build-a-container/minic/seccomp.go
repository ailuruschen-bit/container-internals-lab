package main

import (
	"fmt"
	"runtime"
	"syscall"
	"unsafe"
)

// A minimal seccomp filter built as classic BPF, using only the standard
// library (Chapter 06). It is a DENY LIST: default action ALLOW, with a few
// syscalls returning EPERM, and a wrong architecture killing the process.
//
// A real profile is an ALLOW list of ~300 syscalls compiled by libseccomp; this
// small deny list keeps the example readable while demonstrating the mechanism.

// BPF classic opcodes.
const (
	bpfLD  = 0x00
	bpfW   = 0x00
	bpfABS = 0x20
	bpfJMP = 0x05
	bpfJEQ = 0x10
	bpfK   = 0x00
	bpfRET = 0x06
)

// seccomp return actions (from <linux/seccomp.h>).
const (
	seccompRetAllow       = 0x7fff0000
	seccompRetErrno       = 0x00050000
	seccompRetKillProcess = 0x80000000
)

// AUDIT_ARCH_* values (from <linux/audit.h>).
const (
	auditArchX86_64  = 0xC000003E
	auditArchAArch64 = 0xC00000B7
)

// offsets into struct seccomp_data.
const (
	offNr   = 0 // int nr
	offArch = 4 // __u32 arch
)

// archInfo returns this build's AUDIT_ARCH value and the numbers of the syscalls
// we block, which differ per architecture.
func archInfo() (arch uint32, blocked []uint32, err error) {
	switch runtime.GOARCH {
	case "amd64":
		return auditArchX86_64, []uint32{
			165, // mount
			166, // umount2
			175, // init_module
			101, // ptrace
			250, // keyctl (add_key=248, request_key=249, keyctl=250)
		}, nil
	case "arm64":
		return auditArchAArch64, []uint32{
			40,  // mount
			39,  // umount2
			105, // init_module
			117, // ptrace
			219, // keyctl
		}, nil
	default:
		return 0, nil, fmt.Errorf("unsupported GOARCH %q for the demo filter", runtime.GOARCH)
	}
}

func installSeccomp() error {
	arch, blocked, err := archInfo()
	if err != nil {
		return err
	}

	var f []syscall.SockFilter
	// 0: load seccomp_data.arch
	f = append(f, syscall.SockFilter{Code: bpfLD | bpfW | bpfABS, K: offArch})
	// 1: if arch != expected, jump to the KILL instruction (jf), else continue.
	//    jt/jf are filled after we know the final length.
	f = append(f, syscall.SockFilter{Code: bpfJMP | bpfJEQ | bpfK, K: arch})
	// 2: load seccomp_data.nr
	f = append(f, syscall.SockFilter{Code: bpfLD | bpfW | bpfABS, K: offNr})
	// 3..: for each blocked syscall, if nr == it, jump to the ERRNO instruction.
	for _, nr := range blocked {
		f = append(f, syscall.SockFilter{Code: bpfJMP | bpfJEQ | bpfK, K: nr})
	}
	allowIdx := len(f)     // RET ALLOW
	errnoIdx := allowIdx + 1
	killIdx := allowIdx + 2
	f = append(f,
		syscall.SockFilter{Code: bpfRET | bpfK, K: seccompRetAllow},
		syscall.SockFilter{Code: bpfRET | bpfK, K: seccompRetErrno | (uint32(syscall.EPERM) & 0xffff)},
		syscall.SockFilter{Code: bpfRET | bpfK, K: seccompRetKillProcess},
	)

	// Fix up the jump targets now that indices are known.
	// idx 1: arch check. jf -> killIdx.
	f[1].Jt = 0
	f[1].Jf = uint8(killIdx - 1 - 1)
	// blocked checks start at idx 3.
	for i := 0; i < len(blocked); i++ {
		idx := 3 + i
		f[idx].Jt = uint8(errnoIdx - idx - 1) // matched -> ERRNO
		f[idx].Jf = 0                          // not matched -> next instruction
	}
	_ = allowIdx // the fallthrough after the last JEQ lands on RET ALLOW

	prog := syscall.SockFprog{
		Len:    uint16(len(f)),
		Filter: &f[0],
	}
	if err := prctl(prSetSeccomp, seccompModeFilter, uintptr(unsafe.Pointer(&prog)), 0, 0); err != nil {
		return err
	}
	return nil
}
