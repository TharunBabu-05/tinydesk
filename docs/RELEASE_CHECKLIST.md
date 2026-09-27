# Developer-preview release gate

Source lives in `schikani/tinydesk` with `schikani/tinydesk-shell` pinned as a
submodule. The documentation and installer are maintained separately and are
currently previewed locally. A public HTTPS address is still pending.

## Required evidence

- Run host CMake/CTest on Linux and Windows and all POSIX shell tests.
- Build Desktop for ESP32-C6 8 MB, ESP32 16 MB with PSRAM, and ESP32 4 MB.
  Build both Shell editions before including them in a public release.
- Record the source revision and SHA-256 of every tested image. Never reuse
  an older dist folder as evidence for changed source.
- On each board: cold boot, drag/resize, open/save/reopen a file, shell command,
  memory figures, and a ten-minute interaction/idle test with no reset.
- On factory NVS: Telnet off; Telnet/SSH/FTP cannot authenticate the factory
  root password. Change password locally and explicitly enable a service.
- Recovery: physical success; SSH refusal; Telnet non-root login refusal;
  root Telnet recovery refusal; refusal after disconnect; local recovery
  works after reboot. Try takeover during both password prompts: refuse it.
- Back up existing flash before hardware tests; keep backups private because
  they may contain credentials. Restore test account/settings changes.
- Inspect every selected installer board variant and verify the binary named
  by its manifest exists and matches SHA256SUMS.txt. Test missing assets,
  corrupt assets, rapid board changes, and unavailable desktop downloads.
- Confirm a fresh install from the actual HTTPS site, including first serial
  connection and password setup, on each advertised board variant.
- Obtain 3–5 independent first-install reports before wider promotion.

## Release process

1. Push shell changes first and record the tested submodule revision in the
   desktop repository. Confirm CI succeeds from a clean recursive checkout.
2. Configure the public HTTPS site address when available and verify all links.
3. Tag the tested revision. The release workflow calls CI, tests its release
   host binaries, builds firmware, and creates a **draft prerelease**.
4. Review the draft: version, all five board/edition images, manifests,
   desktop downloads, checksums, source and licence material.
5. Import the release into the website with `tools/fetch_release.py`; run
   the site's link checker and review its installer locally.
6. Publish only after the live HTTPS installation test and evidence above.
   ESP Web Tools requires HTTPS and resolves firmware paths relative to the
   manifest: [official integration documentation](https://esphome.github.io/esp-web-tools/).

Use [LAUNCH_KIT.md](LAUNCH_KIT.md) for the filmed demo and preview announcement.
Hardware tests in this workspace are recorded separately in the local review
report; a build alone is not a passed hardware test.
