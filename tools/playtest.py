#!/usr/bin/env python3
"""Play the original and the replica through the same script, and compare.

The point is to stop finding differences by eye. One script drives both -- the
real SWF under Ruffle and `build/sonny` -- through the same clicks at the same
moments on the same 800x575 virtual display, photographs both at the same
points, and reports where they disagree.

    python3 tools/playtest.py tests/play/zone1.txt

Script commands, one per line, '#' starts a comment:

    wait <seconds>        let it run
    click <x> <y>         press at a stage coordinate
    move <x> <y>          move the pointer only
    key <keysym>          press and release a key by X keysym name
    shot <name>           photograph both, and compare
    note <text>           a line in the report, to say what is going on

A line may be addressed to one side only, for the places where the two must be
driven differently -- the original has a preloader to click through and the
replica does not:

    ref:  click 400 300
    mine: wait 1

Needs Xvfb, python-xlib and Pillow, plus (for the original) Ruffle and a copy
of the SWF with frame 2's isDebugMode patched true, which tools/refcap.py
explains.
"""
import argparse
import json
import os
import signal
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
STAGE_W, STAGE_H = 800, 575

# How much of a difference is worth a line in the report. A byte or two per
# pixel is the text rasteriser and the odd sub-pixel edge; past that something
# is actually missing.
NOTICEABLE = 3.0
CELL = 25                      # the grid the report locates differences on


def parse(path):
    """-> [(target, command, args)], target being 'both', 'ref' or 'mine'."""
    out = []
    for raw in open(path, encoding='utf-8'):
        line = raw.split('#')[0].strip()
        if not line:
            continue
        target = 'both'
        for prefix in ('ref:', 'mine:'):
            if line.startswith(prefix):
                target, line = prefix[:-1], line[len(prefix):].strip()
        if not line:
            continue
        parts = line.split()
        out.append((target, parts[0], parts[1:]))
    return out


class Display:
    """An Xvfb with something running in it, driven by synthetic input."""

    def __init__(self, number, launch, cwd=None, env=None, log=None):
        self.number = number
        self.xvfb = subprocess.Popen(
            ['Xvfb', ':%d' % number, '-screen', '0',
             '%dx%dx24' % (STAGE_W, STAGE_H), '-nolisten', 'tcp'],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(2)
        run_env = dict(os.environ, DISPLAY=':%d' % number)
        run_env.update(env or {})
        sink = open(log, 'w') if log else subprocess.DEVNULL
        self.app = subprocess.Popen(launch, cwd=cwd or ROOT, env=run_env,
                                    stdout=sink, stderr=subprocess.STDOUT)
        time.sleep(6)
        from Xlib import display as xdisplay
        self.x = xdisplay.Display(':%d' % number)
        self.root = self.x.screen().root

    def grab(self):
        from Xlib import X
        from PIL import Image
        raw = self.root.get_image(0, 0, STAGE_W, STAGE_H, X.ZPixmap,
                                  0xffffffff)
        return Image.frombytes('RGB', (STAGE_W, STAGE_H), raw.data, 'raw',
                               'BGRX')

    def move(self, x, y):
        from Xlib import X
        from Xlib.ext import xtest
        xtest.fake_input(self.x, X.MotionNotify, x=int(x), y=int(y))
        self.x.sync()

    def click(self, x, y):
        from Xlib import X
        from Xlib.ext import xtest
        self.move(x, y)
        time.sleep(0.3)
        xtest.fake_input(self.x, X.ButtonPress, 1)
        self.x.sync()
        time.sleep(0.12)
        xtest.fake_input(self.x, X.ButtonRelease, 1)
        self.x.sync()
        time.sleep(0.6)

    def key(self, name):
        from Xlib import X, XK
        from Xlib.ext import xtest
        code = self.x.keysym_to_keycode(XK.string_to_keysym(name))
        xtest.fake_input(self.x, X.KeyPress, code)
        self.x.sync()
        time.sleep(0.08)
        xtest.fake_input(self.x, X.KeyRelease, code)
        self.x.sync()
        time.sleep(0.4)

    def close(self):
        for p in (self.app, self.xvfb):
            try:
                p.send_signal(signal.SIGTERM)
                p.wait(timeout=5)
            except Exception:
                p.kill()


def run(script, display, side, out_dir):
    """Replay the script, saving each shot. -> [name] in order."""
    shots = []
    for target, command, args in script:
        if target != 'both' and target != side:
            continue
        if command == 'wait':
            time.sleep(float(args[0]))
        elif command == 'click':
            display.click(float(args[0]), float(args[1]))
        elif command == 'move':
            display.move(float(args[0]), float(args[1]))
        elif command == 'key':
            display.key(args[0])
        elif command == 'shot':
            name = args[0]
            display.grab().save(os.path.join(out_dir,
                                             '%s.%s.png' % (name, side)))
            shots.append(name)
            print('  %s: %s' % (side, name), flush=True)
        elif command == 'note':
            pass
        else:
            print('unknown command: %s' % command, file=sys.stderr)
    return shots


def compare(name, out_dir):
    """-> (mean difference, [(x, y, w, h, score)] worst regions)."""
    from PIL import Image, ImageChops
    a = Image.open(os.path.join(out_dir, '%s.ref.png' % name)).convert('RGB')
    b = Image.open(os.path.join(out_dir, '%s.mine.png' % name)).convert('RGB')
    diff = ImageChops.difference(a, b)
    grey = diff.convert('L')
    hist = grey.histogram()
    mean = sum(i * n for i, n in enumerate(hist)) / max(1, sum(hist))

    # Where. The frame is scored on a coarse grid and neighbouring hot cells
    # are merged, so the report can say "the bottom panel" rather than a
    # number.
    cells = {}
    small = grey.resize((STAGE_W // CELL, STAGE_H // CELL), Image.BOX)
    px = small.load()
    for cy in range(small.height):
        for cx in range(small.width):
            if px[cx, cy] >= NOTICEABLE:
                cells[(cx, cy)] = px[cx, cy]
    regions = []
    seen = set()
    for start in sorted(cells, key=lambda c: -cells[c]):
        if start in seen:
            continue
        stack, group = [start], []
        seen.add(start)
        while stack:
            cx, cy = stack.pop()
            group.append((cx, cy))
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                n = (cx + dx, cy + dy)
                if n in cells and n not in seen:
                    seen.add(n)
                    stack.append(n)
        xs = [c[0] for c in group]
        ys = [c[1] for c in group]
        score = sum(cells[c] for c in group) / len(group)
        regions.append((min(xs) * CELL, min(ys) * CELL,
                        (max(xs) - min(xs) + 1) * CELL,
                        (max(ys) - min(ys) + 1) * CELL, round(score, 1),
                        len(group)))
    regions.sort(key=lambda r: -r[4] * r[5])

    side = Image.new('RGB', (STAGE_W, STAGE_H * 3 + 8), (255, 0, 255))
    side.paste(a, (0, 0))
    side.paste(b, (0, STAGE_H + 4))
    side.paste(ImageChops.multiply(diff, Image.new('RGB', diff.size,
                                                   (4, 4, 4))),
               (0, STAGE_H * 2 + 8))
    side.save(os.path.join(out_dir, '%s.side.png' % name))
    return mean, regions[:6]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('script')
    ap.add_argument('--out', default=os.path.join(ROOT, 'build/playtest'))
    ap.add_argument('--ruffle', default=os.environ.get('SONNY_RUFFLE'),
                    help='the Ruffle binary')
    ap.add_argument('--swf', default=os.environ.get('SONNY_SWF'),
                    help='the patched SWF (see tools/refcap.py)')
    ap.add_argument('--game', default=os.path.join(ROOT, 'build/sonny'))
    ap.add_argument('--only', choices=('ref', 'mine'),
                    help='run one side and keep the other side\'s shots')
    args = ap.parse_args(argv)

    script = parse(args.script)
    os.makedirs(args.out, exist_ok=True)

    if args.only != 'mine':
        if not args.ruffle or not args.swf:
            raise SystemExit('the original needs --ruffle and --swf')
        print('the original:', flush=True)
        d = Display(91, [args.ruffle, '-g', 'gl', '--width', str(STAGE_W),
                         '--height', str(STAGE_H), '--no-gui',
                         '-PisDebugMode=1', args.swf],
                    log=os.path.join(args.out, 'ruffle.log'))
        try:
            run(script, d, 'ref', args.out)
        finally:
            d.close()

    if args.only != 'ref':
        print('the replica:', flush=True)
        d = Display(92, [args.game],
                    env={'SONNY_SILENT': '1'},
                    log=os.path.join(args.out, 'sonny.log'))
        try:
            run(script, d, 'mine', args.out)
        finally:
            d.close()

    names = [a[0] for t, c, a in script if c == 'shot']
    notes = {}
    last_note = None
    for target, command, a in script:
        if command == 'note':
            last_note = ' '.join(a)
        elif command == 'shot':
            notes[a[0]] = last_note
    report = ['# Playtest: %s' % os.path.basename(args.script), '',
              'Both driven through the same script on an %dx%d display. '
              'A mean difference under %.1f is the text rasteriser; past that '
              'something is missing.' % (STAGE_W, STAGE_H, NOTICEABLE), '']
    worst = []
    for name in names:
        if not (os.path.exists(os.path.join(args.out, '%s.ref.png' % name))
                and os.path.exists(os.path.join(args.out,
                                                '%s.mine.png' % name))):
            continue
        mean, regions = compare(name, args.out)
        worst.append((mean, name))
        report.append('## %s%s' % (name, ' -- %s' % notes[name]
                                   if notes.get(name) else ''))
        report.append('')
        report.append('mean difference **%.2f**  (`%s.side.png`)' % (mean,
                                                                     name))
        if regions:
            report.append('')
            report.append('| where | size | strength |')
            report.append('| --- | --- | --- |')
            for x, y, w, h, score, n in regions:
                report.append('| %d,%d | %dx%d | %.1f |' % (x, y, w, h, score))
        report.append('')
    report.insert(3, 'Worst first: %s\n' % ', '.join(
        '%s (%.1f)' % (n, m) for m, n in sorted(worst, reverse=True)[:10]))
    path = os.path.join(args.out, 'report.md')
    with open(path, 'w', encoding='utf-8') as fh:
        fh.write('\n'.join(report))
    print('\n%s' % path)
    for mean, name in sorted(worst, reverse=True):
        print('  %-22s %6.2f' % (name, mean))


if __name__ == '__main__':
    main()
