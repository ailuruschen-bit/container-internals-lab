# How Does the Operating System Take Back the CPU?

[中文](../how-the-kernel-takes-back-the-cpu.md) | **English** | [日本語](../ja/how-the-kernel-takes-back-the-cpu.md)

A single CPU core can execute only one instruction stream at a time. If one process held on to the CPU indefinitely, no other process would ever get a chance to run. The operating system must be able to regain control when necessary and then decide which process runs next.

## Ordinary Jumps Alone Cannot Hand the CPU Back to the OS

The CPU has no idea which "process" it is running. It simply keeps fetching and executing the instruction that the PC (program counter) points to, under the current register state.

A program can change the PC with jumps, function calls, and returns, but these instructions are still executed voluntarily by the running program itself. If a user program neither calls into the operating system nor gives up the CPU on its own, software control flow alone never gives the kernel a chance to regain execution.

What is needed is an entry point that does not depend on the running program's cooperation: the **interrupt and exception mechanism**. In what follows, I use *trap* as an umbrella term for any controlled transfer from the running program into the kernel. Processor architectures do not classify "trap" consistently, so the word serves here only as a convenient shorthand.

## Hardware Provides Pre-Registered "Emergency Entrances"

Think of the interrupt mechanism as a set of "emergency entrances" built into the CPU. An ordinary program proceeds along its own instruction stream. A device or timer can only raise an interrupt request; it cannot set the PC to an arbitrary address directly. The CPU typically checks for and accepts a pending request after finishing the current instruction, then enters a handler through an entry point that the kernel registered in advance.

Specifically, following the interrupt vector table configured by the kernel, the processor:

1. Saves the minimum state needed to return, such as the original PC and status register;
2. Switches to the kernel privilege level and the kernel stack, if necessary;
3. Looks up the corresponding kernel entry point in the vector table;
4. Transfers control to the entry code.

The critical steps (saving state, changing the privilege level, and jumping to the entry point) are performed automatically by the processor, but the entry addresses and handling rules have already been configured by the kernel. From the entry point onward, it is operating system code that runs.

## Three Common Reasons for Entering the Kernel

| Type | When it occurs | Examples |
|---|---|---|
| Interrupt | Asynchronously to the current instruction; raised by external hardware | Timer expiry, NIC receiving data |
| System call | The program executes a dedicated instruction to request a kernel service | `read()`, `write()`, `fork()` |
| Exception | Synchronously, while the current instruction executes | Page fault, division by zero, illegal instruction |

**An interrupt does not mean entering the kernel periodically.** Devices such as NICs and disks raise interrupts at irregular times; only timer interrupts may recur periodically. Modern Linux can also run without a periodic tick (tickless operation), so periodicity is not an inherent property of the interrupt mechanism.

A system call can be understood as the program entering the kernel voluntarily, but on real hardware it does not necessarily use the same instruction or entry point as an external interrupt. Modern x86-64, for example, typically uses the `syscall` instruction.

Nor does an exception necessarily mean the program has a bug. A page fault may simply indicate that the required page has not been loaded yet; after the kernel handles it, the original instruction can be executed again.

## Entering the Kernel Does Not Necessarily Mean Switching Processes

This is the point most easily confused: **a trap only brings the CPU into the kernel; whether to switch processes is up to the scheduler.**

For example, after a NIC raises an interrupt and the kernel finishes processing the data, it will quite likely return to the process that was running. A system call that completes quickly can also return straight to the original process. The scheduler performs a context switch only when the current process should not keep running, or when another process is a better candidate to run.

Common opportunities include:

- The timer fires and the kernel finds that the current process has run long enough;
- The current process makes a system call such as `read()` and blocks waiting for data;
- A higher-priority process is woken up;
- The current process voluntarily yields the CPU.

## What Happens in a Preemptive Switch

Suppose a single-core CPU is running process A, and process B is also runnable:

```text
Process A user code
    │
    │ Timer raises an interrupt request
    ▼
CPU automatically saves return state and enters the kernel entry point
    │
    │ Kernel handles the interrupt; scheduler decides to run B instead
    ▼
Save A's context, restore B's context
    │
    │ Return from the kernel to user mode
    ▼
Process B resumes where it last left off
```

The context includes not only the PC but also the stack pointer, the general-purpose registers, and address-space-related state. When A is selected again later, the kernel restores this state, and A behaves as if it had merely "paused for a moment."

The timer interrupt therefore gives the kernel a chance to reclaim control **without relying on the process's voluntary cooperation**, and the scheduler uses that chance to decide whether to switch. Together they form an essential foundation of preemptive multitasking, but the timer interrupt is not the only path that leads to scheduling.

## My Current Understanding

The whole mechanism can be condensed into one sentence:

> The CPU enters the kernel in a controlled way through hardware-defined entry points for interrupts, exceptions, and system calls; the kernel handles the reason for entry and, when needed, the scheduler saves the current process and restores another, so that multiple processes take turns getting to run.

What really needs to be kept apart are three actions: **entering the kernel, making a scheduling decision, and performing a context switch.** They often happen back to back, but they are not the same thing.
