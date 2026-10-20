#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Receive a hardware test run from the Wii: logs, timings and framebuffer shots.

  beacon.py --port 47110 --out DIR [--timeout 240]

Exits 0 once the Wii sends END (it then returns to the Homebrew Channel), 1 on timeout or a dropped link.
"""
import argparse
import os
import socket
import sys
import time

from PIL import Image


def yuyv_to_png(data, width, height, path):
    # The Wii's display copy is BT.601 limited range.
    rgb = bytearray(width * height * 3)
    for i in range(0, width * height // 2):
        y0, u, y1, v = data[i * 4], data[i * 4 + 1] - 128, data[i * 4 + 2], data[i * 4 + 3] - 128
        for k, y in enumerate((y0, y1)):
            c = (y - 16) * 1.164
            r, g, b = c + 1.596 * v, c - 0.392 * u - 0.813 * v, c + 2.017 * u
            o = (i * 2 + k) * 3
            rgb[o], rgb[o + 1], rgb[o + 2] = (max(0, min(255, int(x))) for x in (r, g, b))
    Image.frombytes('RGB', (width, height), bytes(rgb)).save(path)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, default=47110)
    parser.add_argument('--out', required=True)
    parser.add_argument('--timeout', type=float, default=240)
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)
    log = open(os.path.join(args.out, 'beacon.log'), 'w')
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(('0.0.0.0', args.port))
    server.listen(1)
    server.settimeout(args.timeout)
    deadline = time.time() + args.timeout
    try:
        conn, addr = server.accept()
    except socket.timeout:
        print('beacon: nobody called', file=sys.stderr)
        return 1
    conn.settimeout(max(5, deadline - time.time()))
    stream = conn.makefile('rb')
    print(f'beacon: connected from {addr[0]}', flush=True)
    while True:
        try:
            line = stream.readline()
        except socket.timeout:
            print('beacon: timed out', file=sys.stderr)
            return 1
        if not line:
            print('beacon: link dropped before END', file=sys.stderr)
            return 1
        text = line.decode('utf-8', 'replace').rstrip('\n')
        if text == 'END':
            print('beacon: END', flush=True)
            return 0
        if text.startswith('LOG '):
            stamp = time.strftime('%H:%M:%S')
            print(f'{stamp} {text[4:]}', flush=True)
            log.write(f'{stamp} {text[4:]}\n')
            log.flush()
        elif text.startswith('SHOT '):
            # A scene's name can hold spaces ("digital rain"); its size and byte count are always the last three fields.
            name, w, h, n = text[5:].rsplit(' ', 3)
            data = stream.read(int(n))
            path = os.path.join(args.out, f"{name.replace(' ', '-')}.png")
            yuyv_to_png(data, int(w), int(h), path)
            print(f'beacon: shot {path}', flush=True)


if __name__ == '__main__':
    sys.exit(main())
