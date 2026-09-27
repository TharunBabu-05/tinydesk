# TinyDesk launch kit

## The story

**A tiny board. A real desktop. Inside your terminal.**

TinyDesk runs a mouse-operated, windowed desktop on an ESP32. Your computer
displays its terminal output and sends keyboard and mouse input. Open Files,
edit and save text, run a shell command, then connect a real project through
MQTT or Modbus. Start with a developer preview for people comfortable flashing
boards and reporting bugs.

## The 30-second film

| Time | Actual footage | Voice / caption |
| --- | --- | --- |
| 0–3 s | One continuous shot: connected ESP32 next to the terminal; immediately drag a window | “This desktop is running on this ESP32.” |
| 3–8 s | Open Files, overlap two windows, drag one by its title bar | “Windows. A mouse. Inside a terminal.” |
| 8–15 s | Create a short text file in Editor, save it, reopen it from Files | “And it actually does things.” |
| 15–22 s | Show one real MQTT value or Modbus test-device reading changing; include the source device in frame | “A tiny workbench for real hardware.” |
| 22–27 s | Open System Monitor; label board, firmware version, free internal RAM and PSRAM separately | “TinyDesk — developer preview.” |
| 27–30 s | Exact supported board and verified install URL | “Try it on your bench. Tell me what breaks.” |

If no sensor/test device is connected, replace 15–22 s with a shell command
that reads the saved file. Do not present simulated data as a live measurement.
Record the board and terminal together before making a close-up cut. Keep the
mouse visible and use readable 80×25 cells; preserve normal playback speed for
the interaction that proves responsiveness. The existing SVG art is an
illustration, not evidence of physical-board operation.

## Record once, make three useful cuts

1. **30 seconds, vertical:** the sequence above, one hook and one install CTA.
2. **60–90 seconds, landscape:** uncut boot, drag, save/reopen, shell and RAM.
3. **3–5 minutes:** exact board and terminal setup, installation, first password,
   one practical project, current limits. Link this from the short clip.

Export a clean version without burned-in captions as well as captioned clips.
Keep the original recordings and serial captures alongside the tested firmware
hash. Capture a thumbnail from the actual board footage: board in foreground,
two overlapping windows behind it, **“ESP32 DESKTOP”** in large text. Avoid
imaginary performance, compatibility, download counts, or fabricated reactions.

## Ready-to-edit launch copy

**Video title:** I put a mouse-operated desktop on an ESP32

**Short post:**

> An ESP32, a USB cable, and a terminal. TinyDesk gives it draggable windows,
> Files, an Editor, and a shell. The board runs the desktop; the computer displays
> the terminal. I’m opening a developer preview and looking for fresh-board
> installation reports. Board requirements, source, and setup are in the release.

Link the tested release at https://github.com/schikani/tinydesk/releases. Include the actual board/firmware
used in the clip and link the longer installation walkthrough. Don't announce a
public installer until an independent tester has completed it from the live site.

**Technical community title:** Show HN: TinyDesk, an ESP32 desktop displayed in a terminal

**Opening comment:**

> I wanted the surprise of a desktop UI on a small microcontroller without a
> display attached. TinyDesk draws text-mode windows and receives terminal mouse
> input. The core is C11; host builds are useful for development. The preview
> includes Files, Editor, a shell, MQTT and Modbus apps. Telnet desktop access is
> opt-in and unencrypted; SSH is a separate shell. I’d appreciate reports about
> installation, terminal compatibility, and what you’d build with it.

## Preview rollout

Ask 3–5 people to use fresh boards and the written instructions without live help.
Record board SKU, flash/PSRAM, OS, terminal/version, installer/browser, time to
first draggable window, failure point, and whether save/reopen worked. Fix the
first-run failures before posting the short video broadly. Share in communities
where the project fits their posting rules; tailor the practical example to the
audience.

Judge the launch by completed installations and useful issue reports. Compare
two real-footage openings using viewer retention if the platform provides it;
there is no reliable promise of virality. Keep the hook visual and the claims
small enough to prove in the same shot.
