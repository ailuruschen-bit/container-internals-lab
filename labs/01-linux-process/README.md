# Labs — Chapter 01: Linux Process Fundamentals

Each lab follows the same structure: Goal, Prerequisites, Background,
Experiment, Expected observations, Why this happens, Connection to containers,
and Questions to think about. Many steps ask you to **predict** the result
before running a command. Write the prediction down; that is where most of the
learning happens.

All labs require Linux. Run them in a disposable VM (see the root
[README](../../README.md#environment)).

| Lab | Topic | Root needed | Doc section |
|---|---|---|---|
| [lab-01-syscalls-with-strace](lab-01-syscalls-with-strace/) | System calls, libc vs syscall names, errno, vDSO | No | [§1](../../docs/01-linux-process/01-processes-and-system-calls.md) |
| [lab-02-process-tree-and-reaping](lab-02-process-tree-and-reaping/) | PPID, zombies, orphans, child subreapers | No | [§2](../../docs/01-linux-process/02-process-tree.md) |
| [lab-03-fork-and-exec](lab-03-fork-and-exec/) | Create, configure in the gap, execute | No | [§3](../../docs/01-linux-process/03-fork-exec-clone.md) |
| [lab-04-clone-flags](lab-04-clone-flags/) | Sharing vs copying with `clone()`; threads as tasks | No | [§3](../../docs/01-linux-process/03-fork-exec-clone.md) |
| [lab-05-inspecting-proc](lab-05-inspecting-proc/) | procfs, `cmdline`, `environ`, magic links, namespace inodes | Part F only | [§4](../../docs/01-linux-process/04-proc-filesystem.md) |
| [lab-06-file-descriptors](lab-06-file-descriptors/) | Shared offsets, close-on-exec, inherited access | Part C only | [§5](../../docs/01-linux-process/05-file-descriptors.md) |
| [lab-07-signals](lab-07-signals/) | Dispositions, exit codes 143/137, inheritance, forwarding | No | [§6](../../docs/01-linux-process/06-signals.md) |
| [lab-08-credentials](lab-08-credentials/) | UIDs/GIDs, privilege dropping, setuid, `no_new_privs` | Yes | [§7](../../docs/01-linux-process/07-credentials.md) |

Install everything needed for this chapter on Debian/Ubuntu:

```bash
sudo apt-get install -y build-essential strace procps psmisc util-linux python3
```

## Verification status

These labs were written against documented kernel and glibc behavior (see
[references](../../references/01-linux-process.md)). Exact output such as PIDs,
flags printed by `strace`, and capability masks varies by distribution, kernel,
and library version. If an observation differs from the documented one in a
way that is not explained, please record it; it is either an environment
difference worth noting or an error to fix.
