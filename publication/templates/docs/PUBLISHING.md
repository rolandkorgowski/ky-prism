# Publishing ky-prism

Publish **this standalone directory only**, not the private remote-desktop
repository or its system backup. Review LICENSE, COPYRIGHT.md and
docs/LICENSING.md before making it public. The prepared license is MIT.

Create an empty GitHub repository named `ky-prism` under your own account.
From this standalone directory:

```bash
git init -b main
git add .
git commit -m "Publish ky-prism source and portable installer"
git remote add origin YOUR_GITHUB_REPOSITORY_URL
git push -u origin main
```

Use your real repository URL instead of the placeholder. No GitHub token,
private email or private SSH key belongs in a repository file. Alternatively,
upload the directory contents through GitHub's web interface, keeping `.github`.

The matrix workflow runs on push/PR and by **Actions → Build and package →
Run workflow**. It uploads build/test logs, native DEB/RPM files, SHA256SUMS
and the matching `git archive` source snapshot as workflow artifacts. Failed
tests stop package generation. The artifact name identifies the build distro.
GPU tests may be explicitly skipped on CPU-only runners: inspect logs.

For real RHEL 9/10 runners, configure your own subscribed desktop machines
with labels `rhel9` and `rhel10`; opt in through the separate manual workflow.
Do not expose self-hosted RHEL runners to untrusted pull request execution.

After validating the matrix and real desktop smoke tests, create a GitHub
Release manually, attach each distro's package, its checksums and the exact
matching source archive. Do not use an Arch binary as the Ubuntu/RHEL release.
CUDA-enabled binaries are deliberately not created on hosted CI: users can
build their own adapter using `bash install.sh --nvjpeg ON`.

Native artifacts currently rely on their build distribution's package ABI.
Build new packages for new distro releases instead of advertising one RPM or
DEB as universal. Retain corresponding source/notices when redistributing
libraries later. No automated job in this repository publishes a release or
changes visibility on your behalf.
