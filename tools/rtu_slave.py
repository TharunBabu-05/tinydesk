#!/usr/bin/env python3
"""rtu_slave.py - a small Modbus RTU slave on one or more PC serial ports
(USB-RS485 converters), for testing tinydesk's `modbus ... rtu` commands.

    python rtu_slave.py COM6 COM7 [--baud 9600] [--parity N] [--unit 1] [--seconds 60]

Each port gets its own tables: holding and input registers start at
<port number> * 1000 (COM6: 6000, 6001, ...), coils alternate 1,0,1,0.
Writes are applied and every request is logged. Needs pyserial (it comes
with the ESP-IDF Python environment).
"""
import argparse
import re
import sys
import threading
import time

import serial


def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


class Slave:
    def __init__(self, port, baud, parity, unit):
        self.port, self.unit = port, unit
        base = int(re.sub(r"\D", "", port) or 0) * 1000
        self.holding = [base + i for i in range(128)]
        self.inputs = [base + 500 + i for i in range(128)]
        self.coils = [i % 2 == 0 for i in range(128)]
        self.ser = serial.Serial(port, baud, parity=parity, timeout=0)
        char_s = 11.0 / baud
        self.gap = max(3.5 * char_s, 0.002)  # end of frame
        self.requests = 0

    def log(self, msg):
        print("[%s] %s" % (self.port, msg), flush=True)

    def answer(self, req):
        unit, fc = req[0], req[1]
        if unit != self.unit:
            return None                      # another slave's frame
        addr = (req[2] << 8) | req[3]
        qty = (req[4] << 8) | req[5]
        def exc(code):
            return bytes([unit, fc | 0x80, code])
        if fc in (1, 2):
            if addr + qty > 128: return exc(2)
            bits = self.coils[addr:addr + qty]
            out = bytearray((qty + 7) // 8)
            for i, b in enumerate(bits):
                if b: out[i // 8] |= 1 << (i % 8)
            return bytes([unit, fc, len(out)]) + out
        if fc in (3, 4):
            if addr + qty > 128: return exc(2)
            regs = (self.holding if fc == 3 else self.inputs)[addr:addr + qty]
            return bytes([unit, fc, qty * 2]) + b"".join(r.to_bytes(2, "big") for r in regs)
        if fc == 5:
            self.coils[addr] = qty == 0xFF00
            return bytes(req[:6])
        if fc == 6:
            self.holding[addr] = qty
            return bytes(req[:6])
        if fc == 15:
            for i in range(qty):
                self.coils[addr + i] = bool(req[7 + i // 8] >> (i % 8) & 1)
            return bytes(req[:6])
        if fc == 16:
            for i in range(qty):
                self.holding[addr + i] = (req[7 + 2 * i] << 8) | req[8 + 2 * i]
            return bytes(req[:6])
        return exc(1)

    def run(self, until):
        buf, last = bytearray(), time.time()
        while time.time() < until:
            data = self.ser.read(256)
            now = time.time()
            if data:
                buf += data
                last = now
                continue
            if buf and now - last >= self.gap:
                frame, buf = bytes(buf), bytearray()
                if len(frame) < 4 or crc16(frame[:-2]) != (frame[-2] | frame[-1] << 8):
                    self.log("bad frame %s" % frame.hex(" "))
                    continue
                self.requests += 1
                resp = self.answer(frame[:-2])
                self.log("got %s" % frame.hex(" "))
                if resp:
                    c = crc16(resp)
                    resp += bytes([c & 0xFF, c >> 8])
                    self.ser.write(resp)
                    self.log("sent %s" % resp.hex(" "))
            time.sleep(0.0005)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ports", nargs="+")
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--parity", default="N", choices="NEO")
    ap.add_argument("--unit", type=int, default=1)
    ap.add_argument("--seconds", type=float, default=60)
    a = ap.parse_args()
    slaves = [Slave(p, a.baud, a.parity, a.unit) for p in a.ports]
    until = time.time() + a.seconds
    threads = [threading.Thread(target=s.run, args=(until,)) for s in slaves]
    for s in slaves:
        s.log("unit %d, %d %s, holding[0]=%d" % (a.unit, a.baud, a.parity, s.holding[0]))
    for t in threads: t.start()
    for t in threads: t.join()
    for s in slaves:
        s.log("%d requests answered" % s.requests)


if __name__ == "__main__":
    sys.exit(main())
