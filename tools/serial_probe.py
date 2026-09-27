#!/usr/bin/env python3
"""serial_probe.py - drive tinydesk on real hardware from a script.

Opens the board's serial port like a terminal would, answers the terminal
size query (as an 80x25 terminal unless --size=COLSxROWS is given; --baud=N
sets the line speed, 921600 for the classic ESP32 port), sends keys and
mouse clicks, and saves everything the board sent. Render a capture with
tools/vtshot:  vtshot capture_1.bin 80 25

  python serial_probe.py COM3 out wait=1500 key=Enter wait=500 shot type="ls\\r" wait=800 shot

Actions: wait=MS, type=TEXT (\\r \\n \\t \\e \\xHH escapes), key=NAME,
click=X,Y (0-based cells), shot (write out_<n>.bin with all output so far),
reset (pulse the chip reset through the RTS/DTR control lines).
Needs pyserial (included in the ESP-IDF Python environment).
"""
import sys
import time

import serial

KEYS = {
    "Enter": b"\r", "Esc": b"\x1b", "Tab": b"\t", "Up": b"\x1b[A", "Down": b"\x1b[B",
    "Right": b"\x1b[C", "Left": b"\x1b[D", "F6": b"\x1b[17~", "F10": b"\x1b[21~",
    "F11": b"\x1b[23~", "Bksp": b"\x7f", "CtrlL": b"\x0c", "CtrlC": b"\x03",
    "CtrlQ": b"\x11", "F4": b"\x1b[14~", "Del": b"\x1b[3~",
}


def unescape(text):
    return text.replace("\\e", "\\x1b").encode().decode("unicode_escape").encode("latin-1")


def main():
    args = sys.argv[1:]
    cols, rows = 80, 25
    baud = 115200        # the C6's USB Serial/JTAG ignores it; the classic ESP32's UART0 runs at 921600
    while args and args[0].startswith("--"):
        opt = args.pop(0)
        if opt.startswith("--size="):
            cols, rows = (int(v) for v in opt[7:].split("x"))
        elif opt.startswith("--baud="):
            baud = int(opt[7:])
        else:
            sys.exit("unknown option " + opt)
    port, prefix, actions = args[0], args[1], args[2:]

    ser = serial.Serial()
    ser.port = port
    ser.baudrate = baud
    ser.timeout = 0
    ser.dtr = False      # equal DTR/RTS levels never reset the chip
    ser.rts = False
    ser.open()

    capture = bytearray()
    shots = 0

    def pump(seconds):
        end = time.time() + seconds
        while time.time() < end:
            data = ser.read(8192)
            if data:
                capture.extend(data)
                # Answer "where is the cursor" the way a real terminal does
                # after tinydesk's ESC[999;999H: bottom-right corner.
                for _ in range(data.count(b"\x1b[6n")):
                    ser.write(b"\x1b[%d;%dR" % (rows, cols))
            else:
                time.sleep(0.005)

    pump(0.2)
    for a in actions:
        if a.startswith("wait="):
            pump(int(a[5:]) / 1000.0)
        elif a.startswith("type="):
            ser.write(unescape(a[5:]))
            pump(0.1)
        elif a.startswith("key="):
            ser.write(KEYS[a[4:]])
            pump(0.1)
        elif a.startswith("click="):
            x, y = (int(v) for v in a[6:].split(","))
            ser.write(b"\x1b[<0;%d;%dM" % (x + 1, y + 1))
            pump(0.03)
            ser.write(b"\x1b[<0;%d;%dm" % (x + 1, y + 1))
            pump(0.1)
        elif a == "reset":
            # RTS high with DTR low resets the chip. Windows' usbser.sys only
            # passes an RTS change on when DTR is written too (as esptool does).
            ser.dtr = False
            ser.rts = True
            ser.dtr = ser.dtr
            time.sleep(0.1)
            ser.rts = False
            ser.dtr = ser.dtr
            pump(0.1)
        elif a == "shot":
            shots += 1
            name = "%s_%d.bin" % (prefix, shots)
            with open(name, "wb") as f:
                f.write(capture)
            print("%s: %d bytes" % (name, len(capture)))
        else:
            sys.exit("unknown action " + a)
    ser.close()


if __name__ == "__main__":
    main()
