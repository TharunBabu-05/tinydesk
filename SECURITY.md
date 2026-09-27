# Security

This project is a developer preview. Please report suspected vulnerabilities
privately through this repository's **Security → Report a vulnerability** page.
Include the revision, board, connection type and steps to reproduce. Do not
post credentials or flash dumps in public issues.

Change the factory root password (`TinyDesk`) locally with `passwd` before
enabling remote access. Remote authentication fails until it is changed.
FTP and desktop Telnet are unencrypted; use SSH/SFTP where possible.
Desktop Telnet is opt-in and root-only because it controls the existing session.

Password recovery is restricted to the physical console. A remote takeover
permanently revokes that console's physical trust until reboot, including
after disconnect. A takeover is refused while local recovery is active.
Embedders must mark shared-console remote input with `tdsh_console_mark_remote()`
before accepting it. Separate SSH sessions are not physical consoles.

Keep board configuration, credentials, NVS and flash backups out of Git.
Firmware updates are not signed; verify release checksums and the source.
