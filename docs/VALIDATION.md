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

Raw captures, credentials and flash backups remain private. No hardware
performance claim is inferred from a successful build.

## After the first release

- Windows and Linux programs: the desktop follows the terminal's size
  (Windows Terminal maximised and restored); PuTTY resizing checked on a
  board.
- MQTT over TLS in the PC programs, built against mbedTLS v3.6.7: connected to
  `test.mosquitto.org`; the release workflows fail a build without TLS.
- `tdsh.exe` (TinyDesk Shell for Windows) in Windows Terminal: line editing,
  history, Tab completion, `ifconfig`, `ping`, `exit`; its CTest smoke test
  pipes a script through it. It needs only Windows system DLLs.

## Still to validate before a wider launch

Full Editor save/reopen interaction on every board, live MQTT/Modbus devices,
OTA updates, standalone Shell firmware on fresh boards, and independent
installation reports remain release checks. The installer is live at
https://schikani.github.io/tinydesk-docs/install/; first installations from
it on every advertised board still have to be recorded.

See [the release checklist](RELEASE_CHECKLIST.md) before tagging a release.
