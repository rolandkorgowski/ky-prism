# RHEL 9 and RHEL 10

Use a registered, updated 64-bit RHEL installation with a graphical desktop,
OpenGL drivers, and access to BaseOS/AppStream. A headless server does not
gain a working GPU desktop merely by installing ky-prism.

```bash
bash install.sh --setup-rhel-repos
```

The flag is explicit consent to configure additional repositories. The
installer enables CodeReady Builder through `subscription-manager`, then
installs EPEL and Remi release packages. Remi-safe provides libvips including
its C++ development package. These are community repositories; installing
them does not make ky-prism or those packages Red Hat-supported products.
It does not register a machine or ask for Red Hat account credentials.
If registration/entitlement is required, complete it separately with Red Hat.

Before importing downloaded keys it verifies the primary OpenPGP fingerprint
with GnuPG. Only a single primary key is accepted per download:

| Key | Primary fingerprint |
| --- | --- |
| EPEL 9 | `FF8AD1344597106ECE813B918A3872BF3228467C` |
| EPEL 10 | `7D8D15CBFC4E62688591FB2633D98517E37ED158` |
| Remi EL9 (2021) | `B1ABF71E14C9D74897E198A8B19527F1478F8947` |
| Remi EL10 (2024) | `CF1DF0057CE85DFF5B2F2A37C2FD3B2C2A0948E4` |

Remote release RPMs retain `localpkg_gpgcheck=1`. There is no `--nogpgcheck`.
Key rotation intentionally stops setup until an installer update verifies
new fingerprints. Existing administrator-managed repositories can be used
without this flag; `bash install.sh --plan` shows dependencies. Where external
repositories are prohibited, ask the administrator to provide equivalent
packages and use `--no-deps` after dependencies pass `--check`.

References checked 2026-10-23:

* [Red Hat: EPEL support boundaries](https://access.redhat.com/solutions/3358)
* [Fedora package signing keys](https://fedoraproject.org/security/)
* [Remi repository setup and fingerprints](https://blog.remirepo.net/pages/Config-en)
* [Remi libvips package](https://rpms.remirepo.net/enterprise/10/safe/x86_64/repoview/vips-devel.html)

AlmaLinux/Rocky/CentOS 9/10 use `crb` instead of Subscription Manager.
CI checks on AlmaLinux are compatibility evidence, not actual RHEL desktop
or NVIDIA-driver certification. RHEL 10's own CPU baseline still applies.
