# 5. Chapter Summary: The Description Layer

## The model you should now have

The OCI is three specifications that **describe**, in a vendor-neutral way, the
container mechanisms you built by hand:

```text
Distribution spec ── pull/push by digest ──►  Image spec (index→manifest→config+layers)
                                                     │ unpack + translate config
                                                     ▼
                                      Runtime spec: bundle = config.json + rootfs/
                                                     │ create → start → kill → delete
                                                     ▼
                              Linux mechanisms (Chapters 01-08), applied by runc
```

- **config.json** is `minic`'s flags, formalized: `process`, `root`, `hostname`,
  `mounts`, and `linux.{namespaces,resources,devices,seccomp,maskedPaths,...}`,
  each a Chapter 01–08 mechanism.
- The **lifecycle** (created → running → stopped) with `create`/`start` split and
  **hooks** is the proper version of Chapter 09's setup-then-exec, and the seam
  where CNI wires networking.
- The **image** is a content-addressed graph; its config supplies
  `Entrypoint`/`Cmd`/`Env`/`User`/`WorkingDir` to the runtime config.

## The sentence to remember

> The OCI adds no new mechanism. It standardizes how a container's configuration
> and filesystem are described, so any image runs under any runtime and any tool
> can produce or consume them.

## Self-check questions

1. What are the two things in an OCI bundle, and what produces each? (§2, §4)
2. Pick five `config.json` fields and name the Linux mechanism and chapter for
   each. (§2)
3. Why does the runtime spec separate `create` from `start`, and what fits in
   between? (§3)
4. A container must get a network interface. Which OCI feature attaches it, and
   which component does the work? (§3, and Ch. 03 §6)
5. Trace an image from a tag to a running process: which objects are resolved, in
   what order, and where does the image config meet the runtime config? (§4)
6. Why can a registry deduplicate layers and verify integrity? (§4)
7. How would you express `minic run -userns -mem 512m -pids 200 -- /bin/sh` as
   `config.json` fields? (§2)

## Next: Chapter 11 — runc internals

Chapter 10 defined the contract; Chapter 11 reads the code that fulfills it.
We trace `runc run` through libcontainer: the `nsexec` C bootstrap, namespace and
user-namespace setup, the rootfs and `pivot_root`, cgroups, capabilities,
seccomp, and the final `execve` — each step linked back to the experiment in
Chapters 01–09 that reproduces it.
