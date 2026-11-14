# Security policy

SOBI OS is an educational kernel that runs in QEMU. It has not been audited
and must not be used to protect real data.

That said, isolation bugs are interesting and worth fixing: a user program
that can read or write kernel memory, another process's memory, or crash the
kernel through a system call. Please report them by opening a GitHub issue
(or by email to abhijaymishra07@gmail.com if you prefer), with a minimal user
program or test scenario that reproduces the problem.

Known gaps are listed in [`docs/limitations.md`](docs/limitations.md); SMEP,
SMAP and KASLR are not implemented yet.
