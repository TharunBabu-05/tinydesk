#!/usr/bin/env python3
"""
serve_bin.py - host a firmware .bin on your local network (stdlib only).

Usage:
    python tools/serve_bin.py ports/esp32c6/build/tinydesk.bin     # on port 8000
    python tools/serve_bin.py ports/esp32/build/tinydesk.bin -p 8080
    python serve_bin.py                          # ./tinydesk.bin

Endpoints:
    /               web page with size, SHA-256, build time and a download link
    /<name>.bin     the firmware itself (supports HTTP Range for partial downloads)
    /info.json      {"name", "size", "sha256", "modified"} for OTA version checks

The file is re-read on every request, so after `idf.py build` the new
firmware is served immediately without restarting this script.
"""

import argparse
import hashlib
import html
import json
import os
import socket
import sys
import time
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

CHUNK = 64 * 1024
_hash_cache = {}  # path -> (mtime, size, sha256)


def lan_ip():
    """Best-guess LAN IP (no traffic is actually sent)."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("10.255.255.255", 1))
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


def file_info(path):
    st = os.stat(path)
    cached = _hash_cache.get(path)
    if cached and cached[0] == st.st_mtime and cached[1] == st.st_size:
        digest = cached[2]
    else:
        h = hashlib.sha256()
        with open(path, "rb") as f:
            for block in iter(lambda: f.read(CHUNK), b""):
                h.update(block)
        digest = h.hexdigest()
        _hash_cache[path] = (st.st_mtime, st.st_size, digest)
    return {
        "name": os.path.basename(path),
        "size": st.st_size,
        "sha256": digest,
        "modified": time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(st.st_mtime)),
    }


def make_handler(bin_path):
    bin_name = os.path.basename(bin_path)

    class Handler(BaseHTTPRequestHandler):
        server_version = "tinydesk-bin-server/1.0"

        def log_message(self, fmt, *args):
            sys.stdout.write("[%s] %s  %s\n" % (time.strftime("%H:%M:%S"),
                                                 self.client_address[0], fmt % args))

        def _send_bytes(self, body, ctype, status=HTTPStatus.OK):
            self.send_response(status)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            if self.command != "HEAD":
                self.wfile.write(body)

        def do_HEAD(self):
            self.do_GET()

        def do_GET(self):
            path = self.path.split("?", 1)[0]
            if not os.path.isfile(bin_path):
                self._send_bytes(b"firmware file not found\n", "text/plain",
                                 HTTPStatus.NOT_FOUND)
                return
            if path in ("/", "/index.html"):
                self._send_page()
            elif path == "/info.json":
                body = json.dumps(file_info(bin_path), indent=2).encode()
                self._send_bytes(body, "application/json")
            elif path == "/" + bin_name:
                self._send_firmware()
            else:
                self._send_bytes(b"not found\n", "text/plain", HTTPStatus.NOT_FOUND)

        def _send_page(self):
            info = file_info(bin_path)
            host = self.headers.get("Host", "localhost")
            url = "http://%s/%s" % (host, bin_name)
            page = f"""<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{html.escape(bin_name)}</title>
<style>
 body{{font-family:system-ui,sans-serif;max-width:640px;margin:40px auto;padding:0 16px;
      background:#f4f6f6;color:#172120}}
 .card{{background:#fff;border:1px solid #d3dcd9;border-radius:8px;padding:20px}}
 td{{padding:4px 12px 4px 0;vertical-align:top}} td:first-child{{color:#56645f}}
 code{{font-family:ui-monospace,Consolas,monospace;word-break:break-all}}
 a.btn{{display:inline-block;margin-top:14px;padding:10px 16px;background:#0b6776;
       color:#fff;border-radius:6px;text-decoration:none}}
 @media (prefers-color-scheme:dark){{body{{background:#101615;color:#e2ebe8}}
  .card{{background:#172120;border-color:#2b3836}}}}
</style></head><body><div class="card">
<h2>{html.escape(bin_name)}</h2>
<table>
<tr><td>Size</td><td>{info['size']:,} bytes</td></tr>
<tr><td>Built</td><td>{info['modified']}</td></tr>
<tr><td>SHA-256</td><td><code>{info['sha256']}</code></td></tr>
<tr><td>OTA URL</td><td><code>{html.escape(url)}</code></td></tr>
</table>
<a class="btn" href="/{html.escape(bin_name)}" download>Download</a>
</div></body></html>"""
            self._send_bytes(page.encode(), "text/html; charset=utf-8")

        def _send_firmware(self):
            size = os.path.getsize(bin_path)
            start, end = 0, size - 1
            status = HTTPStatus.OK

            rng = self.headers.get("Range")
            if rng and rng.startswith("bytes="):
                try:
                    a, b = rng[6:].split(",")[0].strip().split("-")
                    if a == "":                      # bytes=-N (last N bytes)
                        start = max(0, size - int(b))
                    else:
                        start = int(a)
                        if b:
                            end = min(int(b), size - 1)
                    if start > end or start >= size:
                        raise ValueError
                    status = HTTPStatus.PARTIAL_CONTENT
                except ValueError:
                    self.send_response(HTTPStatus.REQUESTED_RANGE_NOT_SATISFIABLE)
                    self.send_header("Content-Range", "bytes */%d" % size)
                    self.end_headers()
                    return

            length = end - start + 1
            self.send_response(status)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Disposition", 'attachment; filename="%s"' % bin_name)
            self.send_header("Content-Length", str(length))
            self.send_header("Accept-Ranges", "bytes")
            self.send_header("Cache-Control", "no-store")
            if status == HTTPStatus.PARTIAL_CONTENT:
                self.send_header("Content-Range", "bytes %d-%d/%d" % (start, end, size))
            self.end_headers()
            if self.command == "HEAD":
                return

            with open(bin_path, "rb") as f:
                f.seek(start)
                remaining = length
                try:
                    while remaining > 0:
                        block = f.read(min(CHUNK, remaining))
                        if not block:
                            break
                        self.wfile.write(block)
                        remaining -= len(block)
                except (BrokenPipeError, ConnectionResetError):
                    self.log_message("client disconnected after %d bytes",
                                     length - remaining)

    return Handler


def main():
    ap = argparse.ArgumentParser(description="Host a firmware .bin on the local network.")
    ap.add_argument("bin", nargs="?", default="tinydesk.bin", help="path to the .bin file")
    ap.add_argument("-p", "--port", type=int, default=8000, help="port (default 8000)")
    ap.add_argument("--host", default="0.0.0.0", help="bind address (default all interfaces)")
    args = ap.parse_args()

    bin_path = os.path.abspath(args.bin)
    if not os.path.isfile(bin_path):
        sys.exit("File not found: %s" % bin_path)

    info = file_info(bin_path)
    server = ThreadingHTTPServer((args.host, args.port), make_handler(bin_path))
    ip = lan_ip()
    print("Serving  %s  (%s bytes)" % (bin_path, format(info["size"], ",")))
    print("SHA-256  %s" % info["sha256"])
    print()
    print("Web page  http://%s:%d/" % (ip, args.port))
    print("OTA URL   http://%s:%d/%s" % (ip, args.port, info["name"]))
    print("Info      http://%s:%d/info.json" % (ip, args.port))
    print("\nPress Ctrl+C to stop.\n")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
