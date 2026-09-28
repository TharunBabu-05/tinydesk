# Developer-preview release gate

Source lives in `schikani/tinydesk` with `schikani/tinydesk-shell` pinned as a
submodule. The documentation and web installer live in
`schikani/tinydesk-docs` and are published with GitHub Pages at
https://schikani.github.io/tinydesk-docs/.

## Required evidence

- Run host CMake/CTest on Linux and Windows and all POSIX shell tests.
- Build Desktop for ESP32-C6 8 MB, ESP32 16 MB with PSRAM, and ESP32 4 MB,
  the Shell edition for ESP32-C6 and ESP32, and the Windows and Linux programs
  of both editions (`tinydesk.exe`, `tdsh.exe`, and their Linux builds).
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

1. Push shell changes first. For a shell release, set its `VERSION` file and
   tag `tinydesk-shell` with `v<VERSION>`: its release workflow builds the
   Shell firmware, `tdsh.exe` and the Linux program into a **draft
   prerelease**. Review and publish it.
2. Record the tested submodule revision in the desktop repository and push.
   Confirm CI succeeds from a clean recursive checkout.
3. Set the version: `include/tinydesk/td.h` (`TD_VERSION`), `PROJECT_VER`
   in `ports/esp32c6`, `ports/esp32` and `ports/esp32-4mb`, the README line,
   `RELEASE_NOTES.md`, and the docs site's changelog and `api/core.md`. The
   site's cover and installer take theirs from the release when the Pages
   workflow runs. Then tag the tested revision. The release workflow calls CI, tests its release
   host binaries (MQTT over TLS must be built in), builds firmware, and
   creates a **draft prerelease**.
4. Review the draft: version, all five board/edition images, manifests,
   the four PC downloads, checksums, source and licence material, and the
   release text.
5. Publish the draft, then run the docs site's *GitHub Pages* workflow
   (Actions, Run workflow): `tools/fetch_release.py` copies the newest
   published release into the installer after checking `SHA256SUMS.txt`, and
   `tools/check_links.py` checks the links.
6. Test an installation from the live site on each advertised board before
   announcing the release. ESP Web Tools requires HTTPS and resolves firmware
   paths relative to the manifest:
   [official integration documentation](https://esphome.github.io/esp-web-tools/).

Use [LAUNCH_KIT.md](LAUNCH_KIT.md) for the filmed demo and preview announcement.
Record hardware tests in [VALIDATION.md](VALIDATION.md); a build alone is not
a passed hardware test.
