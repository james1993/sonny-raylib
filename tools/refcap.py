#!/usr/bin/env python3
"""Run the original under Ruffle and photograph it.

Checking the replica against the game means being able to see the game, so
this starts Ruffle on a virtual X display, drives it with synthetic clicks and
keys, and saves PNGs of whatever it is showing.

Two things stand in the way of getting past the title screen:

  * The Legacy Collection's SWF is a client of the Adobe AIR shell it shipped
    inside. Root frame 65 runs `if (!isDebugMode) stop();` and waits to be told
    to carry on, which never happens when the SWF runs on its own. Frame 2 sets
    `isDebugMode = false` outright, so a flashvar cannot win; this patches that
    one boolean in the bytecode and writes an uncompressed copy to run.
  * There is no audio device, so the intro cutscene -- which advances on the
    narration's playhead -- never moves. Click its SKIP.

    python3 tools/refcap.py SONNY1.swf script.txt --out shots

Script lines, one command each, '#' to end of line ignored:

    wait <seconds>      let the movie run
    click <x> <y>       click at stage coordinates
    move <x> <y>        move the pointer only
    key <keysym>        press and release a key, by X keysym name
    shot <name>         save <out>/<name>.png

Needs Ruffle, Xvfb, python-xlib and Pillow.
"""
import argparse
import os
import signal
import struct
import subprocess
import sys
import time
import zlib

STAGE_W, STAGE_H = 800, 575
DISPLAY = ':97'


def patch_debug_mode(src, dest):
    """Write an uncompressed copy with `isDebugMode = true`.

    The assignment is a constant-pool push of the name followed by a push of
    the boolean, so the byte to flip is found by walking the early frames'
    actions rather than by searching for a pattern."""
    raw = open(src, 'rb').read()
    body = bytearray(zlib.decompress(raw[8:]) if raw[:3] == b'CWS' else raw[8:])

    nbits = body[0] >> 3
    pos = ((5 + 4 * nbits) + 7) // 8 + 4
    frame, patched = 1, 0
    while pos < len(body):
        header = struct.unpack_from('<H', body, pos)[0]
        tag, length, size = header >> 6, header & 0x3f, 2
        if length == 0x3f:
            length = struct.unpack_from('<I', body, pos + 2)[0]
            size = 6
        start, end = pos + size, pos + size + length
        if tag == 12:                       # DoAction
            patched += patch_actions(body, start, end)
        elif tag == 1:                      # ShowFrame
            frame += 1
            if frame > 4:
                break
        elif tag == 0:
            break
        pos = end
    if not patched:
        raise SystemExit('no isDebugMode assignment found to patch')
    open(dest, 'wb').write(b'FWS' + bytes([raw[3]])
                          + struct.pack('<I', 8 + len(body)) + bytes(body))
    return dest


def patch_actions(body, start, end):
    """Flip the boolean pushed alongside the constant "isDebugMode"."""
    pool, patched = [], 0
    for push_pool in (True, False):
        pos = start
        while pos < end:
            op = body[pos]
            if op < 0x80:
                pos += 1
                continue
            length = struct.unpack_from('<H', body, pos + 1)[0]
            payload = body[pos + 3:pos + 3 + length]
            if push_pool and op == 0x88:            # ConstantPool
                count = struct.unpack_from('<H', payload, 0)[0]
                pool = [p.decode('latin1')
                        for p in bytes(payload[2:]).split(b'\x00')[:count]]
            elif not push_pool and op == 0x96 and 'isDebugMode' in pool:
                patched += patch_push(body, pos + 3, payload,
                                      pool.index('isDebugMode'))
            pos += 3 + length
    return patched


def patch_push(body, base, payload, want):
    """Inside one ActionPush, set the boolean that follows constant `want`."""
    items, pos = [], 0
    while pos < len(payload):
        kind, pos = payload[pos], pos + 1
        if kind == 0:                                   # string
            pos = bytes(payload).index(b'\x00', pos) + 1
            items.append(('str', None, pos))
        elif kind in (2, 3):                            # null, undefined
            items.append(('none', None, pos))
        elif kind in (1, 7):                            # float, int
            items.append(('num', None, pos))
            pos += 4
        elif kind == 6:                                 # double
            items.append(('num', None, pos))
            pos += 8
        elif kind in (4, 5, 8):                         # register, bool, c8
            items.append(('reg' if kind == 4 else 'bool' if kind == 5 else 'c8',
                          payload[pos], pos))
            pos += 1
        elif kind == 9:                                 # c16
            items.append(('c16', None, pos))
            pos += 2
        else:
            return 0
    patched = 0
    for index, (kind, value, offset) in enumerate(items):
        if (kind == 'c8' and value == want and index + 1 < len(items)
                and items[index + 1][0] == 'bool'):
            body[base + items[index + 1][2]] = 1
            patched += 1
    return patched


def run(ruffle, swf, script, outdir):
    os.makedirs(outdir, exist_ok=True)
    xvfb = subprocess.Popen(
        ['Xvfb', DISPLAY, '-screen', '0', '%dx%dx24' % (STAGE_W, STAGE_H),
         '-nolisten', 'tcp'],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(2)
    player = subprocess.Popen(
        [ruffle, '-g', 'gl', '--width', str(STAGE_W), '--height', str(STAGE_H),
         '--no-gui', swf],
        env=dict(os.environ, DISPLAY=DISPLAY),
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(6)
        from PIL import Image
        from Xlib import X, XK, display as xdisplay
        from Xlib.ext import xtest
        screen = xdisplay.Display(DISPLAY)
        root = screen.screen().root

        def move(x, y):
            xtest.fake_input(screen, X.MotionNotify, x=int(x), y=int(y))
            screen.sync()

        def click(x, y):
            move(x, y)
            time.sleep(0.3)
            xtest.fake_input(screen, X.ButtonPress, 1)
            screen.sync()
            time.sleep(0.12)
            xtest.fake_input(screen, X.ButtonRelease, 1)
            screen.sync()
            time.sleep(0.6)

        def key(name):
            code = screen.keysym_to_keycode(XK.string_to_keysym(name))
            xtest.fake_input(screen, X.KeyPress, code)
            screen.sync()
            time.sleep(0.08)
            xtest.fake_input(screen, X.KeyRelease, code)
            screen.sync()
            time.sleep(0.4)

        def shot(name):
            raw = root.get_image(0, 0, STAGE_W, STAGE_H, X.ZPixmap, 0xffffffff)
            path = os.path.join(outdir, name + '.png')
            Image.frombytes('RGB', (STAGE_W, STAGE_H), raw.data,
                            'raw', 'BGRX').save(path)
            print(path)

        for line in script:
            line = line.split('#')[0].strip()
            if not line:
                continue
            word = line.split()
            print('>>', line, flush=True)
            if word[0] == 'wait':
                time.sleep(float(word[1]))
            elif word[0] == 'click':
                click(float(word[1]), float(word[2]))
            elif word[0] == 'move':
                move(float(word[1]), float(word[2]))
            elif word[0] == 'key':
                key(word[1])
            elif word[0] == 'shot':
                shot(word[1])
            else:
                raise SystemExit('unknown command: %s' % line)
    finally:
        for process in (player, xvfb):
            try:
                process.send_signal(signal.SIGTERM)
                process.wait(timeout=5)
            except Exception:
                process.kill()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('swf')
    ap.add_argument('script')
    ap.add_argument('--ruffle', default='ruffle')
    ap.add_argument('--out', default='reference')
    ap.add_argument('--patched', default=None,
                    help='where to write the runnable copy of the SWF')
    args = ap.parse_args()

    patched = args.patched or os.path.join(args.out, 'runnable.swf')
    os.makedirs(os.path.dirname(patched) or '.', exist_ok=True)
    patch_debug_mode(args.swf, patched)
    with open(args.script, encoding='utf-8') as fh:
        run(args.ruffle, patched, fh.read().splitlines(), args.out)


if __name__ == '__main__':
    main()
