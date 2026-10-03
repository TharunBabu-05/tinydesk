# Developer-preview validation

These are the checks completed during preparation of the initial source
release in September 2026. They are a record of tested behavior, not a claim
that every app, peripheral or terminal has been validated.

## Local checks

- Windows GCC/CMake desktop build: all 7 CTest programs passed.
- Linux/WSL shell build: all 7 CTest programs passed, including memory stress,
  scripting and shared-console recovery policy.
- Host desktop simulator: embedded shell executed a command.
- Source archive: private-file exclusion and opt-in documentation tests passed.
- Documentation site: all pages checked with no internal link errors; all 7
  release-import validation tests passed.

## Physical boards

| Detected hardware | Desktop checks completed |
| --- | --- |
| ESP32-C6 rev 0.2, 8 MB flash | Flash and boot, shell, file write/read/remove, mouse window drag, recovery and first-use networking checks |
| ESP32-D0WD-V3 rev 3.0, 16 MB flash, working PSRAM | Flash and boot, shell, file write/read/remove, System Monitor, uptime beyond 10 minutes |
| ESP32-D0WDQ6 rev 1.0, 4 MB flash | Flash and boot, shell, file write/read/remove |

On the C6, physical recovery succeeded; non-root Telnet login was refused;
remote takeover during recovery was refused; recovery over root Telnet and
after disconnect was refused. A reboot restored physical recovery access.
With fresh NVS, Telnet defaulted off and Telnet/SSH/FTP setup was blocked
until the factory root password changed. Original device settings were restored.

A successful build alone is not counted as a hardware test.

## After the first release

- Windows and Linux programs: the desktop follows the terminal's size
  (Windows Terminal maximised and restored); PuTTY resizing checked on a
  board.
- MQTT over TLS in the PC programs, built against mbedTLS v3.6.7: connected to
  `test.mosquitto.org`; the release workflows fail a build without TLS.
- `tdsh.exe` (TinyDesk Shell for Windows) in Windows Terminal: line editing,
  history, Tab completion, `ifconfig`, `ping`, `exit`; its CTest smoke test
  pipes a script through it. It needs only Windows system DLLs.

## 0.1.2

- SD card on the ESP32-C6 (16 GB SDHC on the W6100's SPI bus): `hwtest sd`,
  `sd mount`, write and read back a file with a long name, the card in Files
  and the Editor, `hwtest sd` on the mounted card, `sd umount`, mounting at
  boot (`sd.automount = 1`), and Ethernet pings with the card mounted and
  after unmounting. Internal RAM free with the card, LAN and Wi-Fi in use:
  140 KB. On the ESP32 with PSRAM and the 4 MB ESP32 (no card wired) `sd`
  reports the pins as not configured.
- `ping -c` and ping output in the Terminal window on all three boards.

## 0.1.3

- Every app opened one after another through the Start menu, then the Task
  Manager, on all three boards (scripted, reading the screen): ESP32 with
  PSRAM 13 of 13 apps, 4 MB ESP32 12, ESP32-C6 11; the rest said *Too many
  windows*; the Task Manager listed the tasks with everything open; no
  restart. Before: the 4 MB ESP32 restarted when the Log Viewer opened after
  seven apps. The PC build (tdsim): all 13.
- nano in the Terminal window (80x25 boards, 160x50): Ctrl+C shows
  "line 2/2, column 7" and the help lines are visible.
- `passwd` names the factory password only while root has it (WROVER yes;
  4 MB ESP32 and C6, whose root password was changed, no); `ssh start`
  names it.
- Official updates: before 0.1.3 was published, `ota official` and *Check
  for official updates* on the ESP32-C6 reached the official site over
  HTTPS and reported that it had no feed yet (404); the setting persists
  (`ota notify`). After publishing, the ESP32 with PSRAM read the 0.1.3
  feed: `ota official` and the button say "TinyDesk 0.1.3, the newest
  release, is installed." and fill in the image's URL. Not tested yet:
  finding a newer release and installing it from the feed.
- Software Update on the 4 MB ESP32 shows the new explanation; internal RAM
  free on the C6 with LAN, Wi-Fi and SD: 133.7 KB.

## Not validated yet

- The Editor's save and reopen on every board.
- MQTT and Modbus with live devices.
- Installing a newer official release over the air.
- The Shell firmware on fresh boards.
- Installations from the web installer
  (https://schikani.github.io/tinydesk-docs/install/) on every board it
  offers, and installations by other people.

The release process is in [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md).
