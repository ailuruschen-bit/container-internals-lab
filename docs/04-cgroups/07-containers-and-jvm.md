# 7. Containers, Kubernetes, and the JVM

This section connects the controllers to the tools that configure them, and to
the runtime that reads them most carefully: the JVM.

## From a flag to a file

Every layer translates a setting, and the last layer writes a cgroup file:

```text
docker run --memory=512m --cpus=1.5 --pids-limit=200
   │  Docker Engine API: HostConfig.Memory, NanoCpus, PidsLimit
   ▼
OCI runtime config (config.json) — linux.resources          (Chapter 10)
   "memory": { "limit": 536870912 },
   "cpu":    { "quota": 150000, "period": 100000 },
   "pids":   { "limit": 200 }
   │  runc's cgroup manager (the cgroup code now lives in github.com/opencontainers/cgroups)
   ▼
/sys/fs/cgroup/system.slice/docker-<id>.scope/memory.max  = 536870912
                                             /cpu.max     = 150000 100000
                                             /pids.max    = 200
```

The OCI `linux.resources` structure was designed around cgroup v1 names
(`shares`, `quota`, `period`, `blkioWeight`). On v2 hosts, runtimes convert them,
and the `unified` map allows setting any v2 file directly
(`"unified": {"memory.high": "400M"}`).

| Docker flag | OCI field | cgroup v2 file |
|---|---|---|
| `--memory` | `memory.limit` | `memory.max` |
| `--memory-reservation` | `memory.reservation` | `memory.low` |
| `--memory-swap` | `memory.swap` (memory + swap) | `memory.swap.max` (swap only = difference) |
| `--cpus` | `cpu.quota`, `cpu.period` | `cpu.max` |
| `--cpu-shares` | `cpu.shares` | `cpu.weight` (converted) |
| `--cpuset-cpus` | `cpu.cpus` | `cpuset.cpus` |
| `--pids-limit` | `pids.limit` | `pids.max` |
| `--device-write-bps` | `blockIO.throttleWriteBpsDevice` | `io.max` |

The conversion from v1 CPU shares (2–262144, default 1024) to v2 weight
(1–10000, default 100) used by runc is:
`weight = 1 + ((shares − 2) × 9999) / 262142`.

## Kubernetes

The kubelet builds a cgroup hierarchy per node (systemd driver shown):

```text
/sys/fs/cgroup/kubepods.slice                                    node allocatable for pods
├── kubepods-besteffort.slice/…                                 QoS class BestEffort
├── kubepods-burstable.slice/
│   └── kubepods-burstable-pod<uid>.slice                       one pod
│       ├── cri-containerd-<pause-id>.scope                     the sandbox ("pause") container
│       └── cri-containerd-<app-id>.scope                       one container
└── kubepods-pod<uid>.slice                                     Guaranteed pods sit directly here
```

| Pod spec | cgroup v2 effect |
|---|---|
| `resources.requests.cpu` | `cpu.weight` of the container (and the sum on the pod cgroup) |
| `resources.limits.cpu` | `cpu.max` |
| `resources.limits.memory` | `memory.max` (container and pod) |
| `resources.requests.memory` | used for **scheduling**; not a cgroup limit (the optional Memory QoS feature maps it to `memory.min`/`memory.high`) |
| QoS class | which slice the pod is placed in; also influences `oom_score_adj` |

The pod cgroup has limits equal to the sum of its containers' limits, which is
the hierarchical limit rule from section 2 in action.

## How the JVM discovers its limits

Before container awareness, the JVM read `/proc/meminfo` and the number of
online CPUs, i.e. the **host's** resources. A JVM in a 512 MiB container on a
64 GiB, 32-core host would choose a 16 GiB default heap and 23 parallel GC
threads, and be OOM-killed or throttled.

Since JDK 10 (backported to 8u191), `-XX:+UseContainerSupport` is enabled by
default. cgroup v2 support arrived in JDK 15 and was backported to 11.0.16 and
8u372. At startup, the JVM:

1. reads `/proc/self/cgroup` and `/proc/self/mountinfo` to find the cgroup
   version, where the cgroup filesystem is mounted, and its own cgroup path;
2. reads the controller files for that cgroup: on v2, `memory.max`,
   `memory.swap.max`, `cpu.max`, `cpu.weight`, `cpuset.cpus.effective`, and
   `pids.max`;
3. uses the results in place of host values.

With the cgroup namespace (section 6), step 1 finds `0::/` and step 2 reads
directly under `/sys/fs/cgroup`.

### Memory

| JVM default | Derived from |
|---|---|
| Physical memory seen by the JVM | `memory.max` (if set) |
| Maximum heap (`MaxHeapSize`) | `MaxRAMPercentage` (default **25%**) of that value; for very small memory sizes `MinRAMPercentage` (default 50%) applies |
| Initial heap | `InitialRAMPercentage` (default 1.5625%) |
| GC choice | if the machine is not "server class" (fewer than 2 active processors or less than about 1792 MB memory), the JVM selects **Serial GC** instead of G1 |

The JVM's total footprint is larger than the heap:

```text
memory.max ─────────────────────────────────────────────────────────┐
│ Java heap (-Xmx) │ metaspace │ code cache │ thread stacks │ GC data │ direct buffers │ native libs │ page cache │
└── the kernel counts all of this against memory.max ────────────────┘
```

Two different failures are therefore possible:

| Symptom | Cause | Signal to look for |
|---|---|---|
| `java.lang.OutOfMemoryError: Java heap space`, process may continue | the heap reached `-Xmx` | Java exception, heap dump if enabled |
| Process disappears, exit code **137**, `OOMKilled` | the **total** footprint reached `memory.max` | `memory.events` `oom_kill`, kernel log; no Java exception, no shutdown hooks |

This is why fixing `-Xmx` to the container limit is a mistake, and why teams use
`-XX:MaxRAMPercentage` values such as 50–75% plus Native Memory Tracking
(`-XX:NativeMemoryTracking=summary`, `jcmd <pid> VM.native_memory`) to find the
right headroom.

### CPU

The JVM computes an **active processor count**, returned by
`Runtime.getRuntime().availableProcessors()`:

- start from the CPUs in the process's affinity mask (`cpuset.cpus`, section 4);
- if `cpu.max` has a quota, use `ceil(quota / period)` if that is smaller.
- Older JVMs also used CPU shares (`cpu.weight`) as a hint; this was removed in
  JDK 19 (JDK-8281181) and in later update releases of older JDKs, because a
  weight is not a limit.

The active processor count sizes `ParallelGCThreads`, `ConcGCThreads`, JIT
compiler threads, the `ForkJoinPool.commonPool()` parallelism, and many
framework thread pools. It can be overridden with `-XX:ActiveProcessorCount=N`.

Consequence for Kubernetes: a pod with a CPU **request** but no CPU **limit** has
`cpu.max = max`, so the JVM sees all node CPUs and sizes its thread pools for the
whole node, even though under contention it only receives its requested share.

### Diagnostics

```bash
java -XshowSettings:system -version          # "Operating System Metrics": provider, CPU count, memory limit
java -Xlog:os+container=trace -version       # every cgroup file the JVM reads and the value it derived
java -XX:+PrintFlagsFinal -version | grep -E 'MaxHeapSize|ActiveProcessorCount|ParallelGCThreads|Use(Serial|G1)GC'
jcmd <pid> VM.info                            # container information of a running JVM
```

## Evidence

Lab: [`lab-06-cgroup-namespace-and-jvm`](../../labs/04-cgroups/lab-06-cgroup-namespace-and-jvm/)

## Further Reading

- OCI Runtime Specification, [config-linux.md: Control groups](https://github.com/opencontainers/runtime-spec/blob/main/config-linux.md#control-groups)
  — the `resources` structure and the `unified` map. Preview of Chapter 10.
- runc documentation: [cgroup v2](https://github.com/opencontainers/runc/blob/main/docs/cgroup-v2.md)
  — requirements and the mapping of OCI fields to v2 files, including the
  shares-to-weight conversion.
- Kubernetes documentation: [Resource Management for Pods and Containers](https://kubernetes.io/docs/concepts/configuration/manage-resources-containers/),
  [Pod Quality of Service Classes](https://kubernetes.io/docs/concepts/workloads/pods/pod-qos/),
  and [About cgroup v2](https://kubernetes.io/docs/concepts/architecture/cgroups/).
- OpenJDK source: [`src/hotspot/os/linux/cgroupV2Subsystem_linux.cpp`](https://github.com/openjdk/jdk/blob/master/src/hotspot/os/linux/cgroupV2Subsystem_linux.cpp)
  and `cgroupSubsystem_linux.cpp` — exactly which files HotSpot reads and how it
  computes the processor count. Short, readable C++.
- OpenJDK issues: [JDK-8230305](https://bugs.openjdk.org/browse/JDK-8230305)
  (cgroup v2 support) and [JDK-8281181](https://bugs.openjdk.org/browse/JDK-8281181)
  (stop using CPU shares for the processor count) — the reasoning behind the
  current behavior, in the words of the JVM engineers.
- Oracle, [Java SE 21 Virtual Machine Guide — Ergonomics](https://docs.oracle.com/en/java/javase/21/gctuning/ergonomics.html)
  — how default GC and heap sizes are selected.
