# Desktop demo

[Video (MP4)](desktop-demo-esp32.mp4) · [Looping GIF](desktop-demo-esp32.gif) ·
[Poster (PNG)](desktop-demo-esp32.png)

Recorded on 28 September 2026 from a physical ESP32-WROVER-IE
(ESP32-D0WD-V3 rev 3.0, 16 MB flash, 8 MB PSRAM) running TinyDesk 0.1.0
with its default Dark theme. The board's USB-UART console ran at 921600
baud; the terminal was 100×30.

In one continuous 45-second take:

1. Editor opens from its desktop icon, is resized and moved to the
   bottom right, and two lines are typed into it.
2. Files opens from its icon and goes to the bottom left.
3. System Monitor opens from the Start menu and goes to the top left.
4. Terminal opens from the Start menu, goes to the top right and runs
   `version`, `uptime` and `ls /` in TinyDesk Shell.
5. About opens on top of the four windows.

A script sent ordinary xterm mouse and keyboard sequences over the serial
port and saved every byte the board sent back, with its arrival time. The
typing is paced like a person's: uneven gaps between keys, pauses between
words and before Enter, and a mistyped letter corrected with Backspace in
the Editor and in the Terminal. Before each move or resize the script read
the window's position from the board's own screen, so nothing was placed by
hand. The video replays those bytes through TinyDesk's terminal emulator
(`td_vterm`) in real time. A redraw appears once the serial line has been
idle for 8 ms, so no half-drawn redraw is shown. Nothing is sped up,
interpolated or edited.

Drawn on top: the header line, the mouse pointer (it follows the recorded
input path, which the board receives rounded to cells) and the click
ripples. Everything inside the terminal area comes from the board. Shade
characters such as the desktop pattern are drawn as a dot pattern, as a
terminal font shows them.

The MP4 is 1280×720 at 50 frames per second; the GIF is 1240×660 at 25. It
is a serial recording, not camera footage and not the host simulator.
There is no audio.
