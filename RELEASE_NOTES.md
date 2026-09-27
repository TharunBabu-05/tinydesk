# TinyDesk developer preview

A mouse-operated desktop running on an ESP32, displayed in your terminal.
Drag windows, browse files, edit and save text, and use the embedded shell.
MQTT and Modbus apps connect the desktop to real projects.

This release is a developer preview. Review the board test report and try
a fresh installation before promoting it more broadly.

## Before connecting

Use a UTF-8 terminal with ANSI/VT cursor control and xterm mouse reporting.
ESP32-C6 uses built-in USB; classic ESP32 Desktop uses 921600 baud.
On the local console, run `passwd` as root (initial password: `TinyDesk`).
Remote access is blocked until that password changes. Telnet starts disabled;
enable it explicitly in Network only on a trusted network. It is unencrypted
and root-only because it takes over the existing desktop. SSH provides a
separate encrypted shell, not the windowed desktop.

Physical password recovery is unavailable after a Telnet takeover until the
board reboots. Remote takeover is refused while physical recovery is active.

## Choose the exact board

| Desktop target | Required hardware | Limits |
| --- | --- | --- |
| ESP32-C6 | 8 MB flash, built-in USB Serial/JTAG | Check the selected board's wiring |
| ESP32 with PSRAM | 16 MB flash plus PSRAM | Check PSRAM at boot |
| ESP32 4 MB | 4 MB flash, PSRAM optional | 80×25, no SSH server, no OTA |

Factory flashing can erase user data. Back up an existing board first.
Check SHA256SUMS.txt against the release assets. Firmware is not signed.

## Changes in this preview

- Physical recovery uses a transport trust policy, not just shell task identity.
- Telnet is opt-in; Telnet, SSH, and FTP reject factory-password setup.
- Tests gate release builds; release artifacts are created as a draft preview.
- Documentation describes terminal requirements and paste-time allocation.

The maintainer must attach the tested revision, board/terminal test matrix,
measured RAM/flash figures, and working HTTPS installer URL before publishing.
