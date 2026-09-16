# Labs — Chapter 06: seccomp and no_new_privs

| Lab | Topic | Root needed | Doc section |
|---|---|---|---|
| [lab-01-no-new-privs](lab-01-no-new-privs/) | the flag disarms setuid and file capabilities | Yes (to make setuid copies) | [§1](../../docs/06-seccomp/01-no-new-privs.md) |
| [lab-02-seccomp-filter](lab-02-seccomp-filter/) | install a filter; EPERM vs SIGSYS; inheritance | No | [§2](../../docs/06-seccomp/02-seccomp-modes-and-filters.md) |
| [lab-03-seccomp-arguments](lab-03-seccomp-arguments/) | argument matching; the no-path-filter limit; clone3 fallback | No | [§3](../../docs/06-seccomp/03-actions-and-arguments.md) |
| [lab-04-default-profile](lab-04-default-profile/) | the default container profile; seccomp vs capabilities | Docker | [§4](../../docs/06-seccomp/04-container-profile.md) |

Packages on Debian/Ubuntu:

```bash
sudo apt-get install -y gcc libseccomp-dev seccomp util-linux libcap2-bin strace python3
```

The C programs link against libseccomp (`-lseccomp`). Labs 02 and 03 run
entirely as a normal user, because they set `no_new_privs` before calling
`seccomp()`. Expected observations were written for Linux 6.x and
libseccomp 2.5+.
