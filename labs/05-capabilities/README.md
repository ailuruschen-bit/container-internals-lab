# Labs — Chapter 05: Capabilities

These labs simulate container privilege with `capsh`, `setpriv`, and `unshare`,
so they need no container engine (Lab 04 has optional Docker steps). All create
their scratch files under `/tmp` or `/root` and clean up.

| Lab | Topic | Doc section |
|---|---|---|
| [lab-01-capability-sets](lab-01-capability-sets/) | reading sets; kernel checks capabilities not UID 0; bounding set | [§1](../../docs/05-capabilities/01-root-to-capabilities.md), [§2](../../docs/05-capabilities/02-capability-sets.md) |
| [lab-02-file-capabilities](lab-02-file-capabilities/) | `setcap`, effective bit, `nosuid`, `no_new_privs`, bounding mask | [§2](../../docs/05-capabilities/02-capability-sets.md) |
| [lab-03-capabilities-across-execve](lab-03-capabilities-across-execve/) | root rule, UID transitions, ambient capabilities, securebits | [§3](../../docs/05-capabilities/03-capabilities-across-execve.md) |
| [lab-04-root-in-a-container](lab-04-root-in-a-container/) | default set, ownership vs capabilities, user namespaces | [§4](../../docs/05-capabilities/04-capabilities-and-user-namespaces.md), [§5](../../docs/05-capabilities/05-root-in-a-container.md) |

Packages on Debian/Ubuntu:

```bash
sudo apt-get install -y libcap2-bin attr util-linux python3
```

Most labs need `sudo`; Lab 03 Part C and Lab 04 Part E run as your normal user.
Expected observations were written for Linux 6.x and libcap 2.44+. Capability
bit masks depend on the number of capabilities the kernel defines.
