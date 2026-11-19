# Security policy

accel-virt treats the Windows guest as untrusted. Every command it sends is checked
before a host driver sees it, so that a guest can fault its own context and nothing
else. A way around that is a vulnerability: a guest that can crash QEMU or the
renderer process, hang or reset the host GPU, read or write host memory outside its
own allocations, or reach another guest's state.

## Reporting a vulnerability

Please report vulnerabilities privately, through
[a security advisory](https://github.com/acceleration3/accel-virt/security/advisories/new),
not in a public issue. Include the commit, the host GPU and driver, and the
program, command sequence or wire recording that shows it.

Once a fix is released, the advisory is published with credit to you, unless you
would rather stay anonymous.

## Supported versions

Only `master` gets security fixes.
