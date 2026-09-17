# 5. Chapter Summary and the Whole Stack

## The complete mental model

You can now traverse the entire stack in both directions and, at every
transition, answer *why this layer exists, what it does, what lower mechanism
implements it, how to observe it, and where to read the code.*

```text
docker run nginx
      │  Engine API (REST)                                   Ch. 13 §1
dockerd (Moby): build, networking (libnetwork), volumes      Ch. 13 §2-3, Ch. 02 §3, Ch. 03 §6
      │  gRPC
containerd: images, content, snapshots, containers, tasks    Ch. 12
      │  per-container shim (supervises, reaps, owns stdio)   Ch. 12 §4, Ch. 01 §2
containerd-shim-runc-v2
      │
runc / OCI runtime: apply one OCI bundle                     Ch. 11, Ch. 10
      │  clone/unshare/setns · mount · pivot_root · cgroupfs · prctl · seccomp · execve
Linux system calls                                           Ch. 01 §1
      │
Linux kernel:
   namespaces  ── PID, mount, net, UTS, IPC, user, cgroup    Ch. 03
   cgroups     ── CPU, memory, PIDs, I/O                      Ch. 04
   filesystem  ── rootfs, mounts, pivot_root, OverlayFS       Ch. 02, 07, 08
   security    ── capabilities, seccomp, no_new_privs         Ch. 05, 06
```

## The one idea, restated for the last time

> A container is a Linux process. Everything above the kernel — runc, the shim,
> containerd, dockerd, the CLI — exists to **describe** the configuration of that
> process (OCI), **apply** it correctly and securely (runc), **supply** its
> filesystem from image layers (OverlayFS/containerd), **supervise** it (the
> shim), and **manage and build** many of them with a friendly API (Moby). The
> kernel gives the process an isolated and restricted view; the rest is tooling.

## What each layer contributed, in one line each

- **Chapter 01 (process):** the thing being contained; create-configure-execute;
  reaping (which the shim needs).
- **Chapter 02 (filesystem):** mounts, bind mounts, propagation, special
  filesystems — the substrate for rootfs and volumes.
- **Chapter 03 (namespaces):** the isolated *views*.
- **Chapter 04 (cgroups):** the resource *limits*.
- **Chapter 05 (capabilities):** splitting root; why container root is confined.
- **Chapter 06 (seccomp):** shrinking the syscall attack surface.
- **Chapter 07 (rootfs/pivot_root):** giving the process its own `/`.
- **Chapter 08 (OverlayFS):** layers, shared and private, as the rootfs.
- **Chapter 09 (minic):** you assembled 01–08 into a working runtime.
- **Chapter 10 (OCI):** the standard description of all of it.
- **Chapter 11 (runc):** the production application of the description.
- **Chapter 12 (containerd):** images, storage, supervision, API.
- **Chapter 13 (Moby):** build, networking, volumes, the developer API — and the
  full `docker run nginx` trace.

## Final self-check

If you can answer these without notes, you have met the repository's ultimate
standard (root README §29):

1. Trace `docker run -p 8080:80 nginx` from the CLI to the nginx process,
   naming the component and the chapter at each transition. (§4)
2. At the runc→kernel transition, list every syscall runc makes and the
   mechanism each configures. (Ch. 11 §4)
3. Why is the container's parent a shim and not containerd, and what breaks
   without it? (Ch. 12 §4, Ch. 01 §2)
4. Where does nginx's `/` come from, layer by layer, and which part is writable?
   (Ch. 08, Ch. 12 §3)
5. A request to host `:8080` reaches nginx. Trace the packet through the network
   namespace and NAT. (§2, Ch. 03 §6-7)
6. nginx runs as root inside the container. List every mechanism that limits what
   that root can do, and the one thing that would still be dangerous (a writable
   host bind mount). (Ch. 03, 04, 05, 06; Ch. 05 §5)
7. `docker stop` the container. Describe the exact signal sequence and why the
   exit code is 143 or 137. (Ch. 01 §6, Ch. 03 §3)

## Where to go next

- **Read the source.** You now have the map (Chapters 11–13's reference lists) to
  read runc, containerd, and Moby productively. Start with runc's
  `rootfs_linux.go` (Chapter 11 Lab 02).
- **Extend `minic`** (Chapter 09) with something it lacks: an allow-list seccomp
  profile, a veth/bridge, or an OverlayFS rootfs from image layers. Each is a
  chapter you have already read.
- **Go deeper into one primitive.** The mechanism chapters' Further Reading
  sections (kernel docs, LWN, man-pages) go well beyond this repository.

This is the end of the main path. The repository is designed to keep growing:
add labs, deepen a chapter, or trace another command (`docker exec`, `kubectl
run`) using the same method.
