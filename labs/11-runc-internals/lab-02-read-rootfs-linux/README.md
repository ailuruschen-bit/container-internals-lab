# Lab 02 — A Guided Reading of rootfs_linux.go

## Goal

This is a **reading** lab, not a run lab. You will read the single most valuable
file in the whole stack, `libcontainer/rootfs_linux.go`, and confirm that every
function implements a mechanism you have already reproduced by hand.

## Prerequisites

- The runc source. Either clone it or read on GitHub:
  ```bash
  git clone https://github.com/opencontainers/runc
  cd runc && git checkout "$(runc --version | awk '/spec:/{next} /^runc version/{print $3}')" 2>/dev/null || true
  git log --oneline -1
  ```
  (Check out the tag matching your installed `runc --version`; if unsure, read the
  `main` branch and note the differences.)
- Read: Chapter 11 [§3](../../../docs/11-runc-internals/03-init-rootfs.md),
  and have Chapters 02 §5 and 07 §3–§4 open.

## Reading tasks

Open `libcontainer/rootfs_linux.go`. For each function, answer the question, then
check it against the cited chapter.

### 1. prepareRootfs

- Find where it sets mount propagation on `/`. Which flag, and why before
  everything else? (Chapter 03 §4)
- Find the loop over `config.Mounts`. Each iteration calls `mountToRootfs`. What
  are the OCI mount fields it reads? (Chapter 10 §2)

### 2. mountToRootfs

- How does it handle `type == "bind"` vs `type == "proc"`/`"tmpfs"`? (Chapter 02
  §2, §5)
- Where does it apply per-mount flags like `nosuid`, `nodev`, `noexec`, `ro`?
  (Chapter 02 §2)

### 3. pivotRoot

- Compare its sequence with Chapter 07 §3 and with `minic`'s `setupRootfs`
  (Chapter 09 §3). What is the `pivot_root(".", ".")` idiom, and how is the old
  root detached? (Chapter 07 §3)
- What is the fallback when `pivot_root` is unavailable? (Chapter 07 §3)

### 4. createDevices / bindMountDeviceNode / mknodDevice

- How are the default devices provided: `mknod` or bind mount? What decides?
  (Chapter 02 §5)
- Where is the device **cgroup** (eBPF) set up? (It is in the cgroup manager, not
  here — note the separation.) (Chapter 04 §8)

### 5. maskPath and readonlyPath

- How does `maskPath` hide a file vs a directory? (Chapter 07 §4)
- How does `readonlyPath` make a path read-only? Which Chapter 02 trick does it
  use? (Chapter 02 §3, Chapter 07 §4)

### 6. finalizeRootfs

- What does it do if `config.Root.Readonly` is set? (Chapter 07 §4)

## Deliverable

Write, in your own words, a one-line mapping for each of the six functions to the
chapter and mechanism it implements. If any function does something you cannot
map to Chapters 01–08, that is a gap worth investigating — note it.

## Why this matters

- Confirming that you can read production runtime code and recognize every
  operation is the goal of the whole repository. `rootfs_linux.go` is the best
  place to prove it to yourself.

## Questions to think about

1. Which function makes the host filesystem **absent** rather than hidden, and how
   does that differ from `chroot` (Chapter 07 §2)?
2. `minic` created a few device nodes with `mknod` and no device cgroup. What does
   runc add, and why is a container with `mknod` but a restrictive device cgroup
   still unable to use the host disk? (Chapter 02 §5, Chapter 04 §8)
3. Find one corner case runc handles that `minic` does not, and explain why it
   matters.
