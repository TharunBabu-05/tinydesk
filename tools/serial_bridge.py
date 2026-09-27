#!/usr/bin/env python3
"""serial_bridge.py - use PuTTY with an ESP32 dev board without resetting it.

On boards with a USB-UART chip (CP2102, CH340) the chip's DTR and RTS lines
drive the ESP32's EN (reset) and IO0 pins, so that esptool can flash it.
PuTTY raises both lines when it opens the COM port, which resets the board.
PuTTY has no setting to leave them alone.

This bridge opens the COM port with DTR and RTS released (and never touches
them), listens on a local TCP port, and passes bytes between the board and
one Telnet client at a time (PuTTY in Telnet mode). The COM port stays open
while PuTTY comes and goes, so closing and reopening PuTTY does not reset
the board either.

    python serial_bridge.py COM10                  (then: putty -telnet 127.0.0.1 -P 2310)
    python serial_bridge.py COM10 --putty          (starts PuTTY as well)
    python serial_bridge.py COM10 --baud 921600 --listen 2310

Stop it with Ctrl+C (the board keeps running). Stop it before flashing:
esptool needs the COM port. Needs pyserial (pip install pyserial; it is also
in the ESP-IDF Python environment).
"""
import argparse
import os
import shutil
import socket
import subprocess
import sys
import threading
import time

import serial

IAC, DONT, DO, WONT, WILL, SB, SE = 255, 254, 253, 252, 251, 250, 240
ECHO, SGA, BINARY = 1, 3, 0


class TelnetInput:
    """Strips Telnet commands from what the client sends (PuTTY's option
    negotiation, subnegotiations) and CR NUL / CR LF line ends to CR."""

    def __init__(self):
        self.state = 0      # 0 data, 1 after IAC, 2 option byte, 3 in SB, 4 IAC in SB
        self.last_cr = False

    def feed(self, data):
        out = bytearray()
        for b in data:
            if self.state == 1:
                if b == IAC:
                    out.append(IAC)                 # IAC IAC = a 0xFF byte
                    self.state = 0
                elif b in (DO, DONT, WILL, WONT):
                    self.state = 2
                elif b == SB:
                    self.state = 3
                else:
                    self.state = 0                  # NOP, GA, ...
                continue
            if self.state == 2:
                self.state = 0                      # we already said what we want
                continue
            if self.state == 3:
                if b == IAC:
                    self.state = 4
                continue
            if self.state == 4:
                self.state = 0 if b == SE else 3
                continue
            if b == IAC:
                self.state = 1
                continue
            if self.last_cr and b in (0, 10):       # CR NUL or CR LF: just CR
                self.last_cr = False
                continue
            self.last_cr = b == 13
            out.append(b)
        return bytes(out)


def find_putty():
    for p in (shutil.which("putty"), r"C:\Program Files\PuTTY\putty.exe", r"C:\Program Files (x86)\PuTTY\putty.exe"):
        if p and os.path.exists(p):
            return p
    return None


def main():
    ap = argparse.ArgumentParser(description="COM port <-> Telnet bridge that never resets an ESP32 board")
    ap.add_argument("port", help="COM port, e.g. COM10")
    ap.add_argument("--baud", type=int, default=921600, help="line speed (default 921600, the ESP32 port's)")
    ap.add_argument("--listen", type=int, default=2310, help="local TCP port for PuTTY (default 2310)")
    ap.add_argument("--putty", action="store_true", help="start PuTTY connected to the bridge")
    args = ap.parse_args()

    ser = serial.Serial()
    ser.port = args.port
    ser.baudrate = args.baud
    ser.timeout = 0.05
    ser.dtr = False          # released before open: EN and IO0 stay high, no reset
    ser.rts = False
    try:
        ser.open()
    except serial.SerialException as e:
        sys.exit("cannot open %s: %s (is PuTTY or another program using it?)" % (args.port, e))

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", args.listen))
    srv.listen(1)
    print("%s at %d baud, DTR/RTS released. PuTTY: Telnet to 127.0.0.1 port %d. Ctrl+C stops."
          % (args.port, args.baud, args.listen))
    sys.stdout.flush()

    if args.putty:
        exe = find_putty()
        if exe:
            subprocess.Popen([exe, "-telnet", "127.0.0.1", "-P", str(args.listen)])
        else:
            print("putty.exe not found; start it yourself")

    client = {"sock": None}
    lock = threading.Lock()
    stop = threading.Event()

    def board_to_client():
        while not stop.is_set():
            try:
                data = ser.read(4096)
            except serial.SerialException as e:
                print("serial error: %s" % e)
                stop.set()
                return
            if not data:
                continue
            with lock:
                s = client["sock"]
            if s is None:
                continue                            # nobody watching: drop it
            try:
                s.sendall(data.replace(b"\xff", b"\xff\xff"))
            except OSError:
                pass

    threading.Thread(target=board_to_client, daemon=True).start()

    try:
        while not stop.is_set():
            srv.settimeout(0.5)
            try:
                s, peer = srv.accept()
            except socket.timeout:
                continue
            s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            # Character at a time, no local echo, 8-bit clean.
            s.sendall(bytes([IAC, WILL, ECHO, IAC, WILL, SGA, IAC, DO, SGA,
                             IAC, WILL, BINARY, IAC, DO, BINARY]))
            with lock:
                client["sock"] = s
            ser.write(b"\x1b[5000~")   # tinydesk: new terminal, redraw everything
            print("client %s:%d connected" % peer)
            sys.stdout.flush()
            tin = TelnetInput()
            s.settimeout(0.5)
            while not stop.is_set():
                try:
                    data = s.recv(4096)
                except socket.timeout:
                    continue
                except OSError:
                    break
                if not data:
                    break
                data = tin.feed(data)
                if data:
                    ser.write(data)
            with lock:
                client["sock"] = None
            s.close()
            print("client disconnected (the board keeps running; %s stays open)" % args.port)
            sys.stdout.flush()
    except KeyboardInterrupt:
        pass
    stop.set()
    time.sleep(0.1)
    ser.close()          # closing leaves DTR/RTS as they were: no reset
    print("bridge stopped")


if __name__ == "__main__":
    main()
