#!/usr/bin/env python3
"""Regression checks for the Devil May Cry native build.

usage: python scripts/dmc_regress.py [--record] [--only name,...] [--keep]

Each case boots fresh or loads a save state from games/dmc, runs the native runner headless for a
fixed number of frames with scripted input, then checks:
  - the run ends at the frame limit (no crash, no deadlock, no halt for another reason),
  - no missing-code file and no unhandled syscall / missing IOP import in the log,
  - per-case probes (player position via PS2_PEEK_FRAME),
  - screenshots against golden frames in games/dmc/golden (mean and 99th-percentile channel error).
--record replaces the golden frames with the current output. Golden frames are game output: keep them local.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.path.join(ROOT, 'games', 'dmc')
RUNNER = os.path.join(ROOT, 'build', 'native-dmc', 'ps2run.exe' if os.name == 'nt' else 'ps2run')
ELF = os.path.join(GAME, 'disc', 'SLUS_202.16')
ISO = os.path.join(os.path.dirname(ROOT), 'Devil May Cry.iso')
GOLDEN = os.path.join(GAME, 'golden')
WORK = os.path.join(GAME, 'regress-out')
PLAYER_POS = '5B72C0,3'
TITLE = ['300:start', '600:cross', '660:cross', '720:cross', '780:cross', '840:cross', '900:cross']


def player_y(peeks, frame):
    return peeks.get(frame, (None, None, None))[1]


def check_landed(peeks, args):
    start, end = args
    ys = [peeks[f][1] for f in range(start, end) if f in peeks]
    if not ys:
        return 'no position samples'
    ground = ys[0]
    if min(ys) < ground - 200:
        return f'fell through the floor: y went from {ground:.0f} to {min(ys):.0f}'
    if abs(ys[-1] - ground) > 20:
        return f'did not land: y {ys[-1]:.0f}, ground {ground:.0f}'
    if max(ys) < ground + 300:
        return f'did not jump: max y {max(ys):.0f}, ground {ground:.0f}'
    return None


def check_moved(peeks, args):
    start, end, distance = args
    a, b = peeks.get(start), peeks.get(end)
    if not a or not b:
        return 'no position samples'
    moved = ((a[0] - b[0]) ** 2 + (a[2] - b[2]) ** 2) ** 0.5
    return None if moved >= distance else f'moved {moved:.0f}, expected at least {distance}'


CASES = [
    # name, state (None = fresh boot), frames, presses, screenshot frames, checks
    dict(name='boot-title', state=None, frames=1001, presses=TITLE, shots=[450, 750], checks=[]),
    dict(name='options-menu', state='options1300.ps2', frames=1501, presses=['1310:down:6', '1350:cross:6'], shots=[1500], checks=[]),
    dict(name='cutscene-shop', state='cut9000.ps2', frames=9601, presses=[], shots=[9300, 9600], checks=[]),
    dict(name='mission1-idle', state='ground17000.ps2', frames=17061, presses=[], shots=[17060], checks=[]),
    dict(name='mission1-jump', state='ground17000.ps2', frames=17151, presses=['17010:triangle:10'], shots=[17150],
         checks=[(check_landed, (17005, 17150))]),
    dict(name='mission1-walk', state='ground17000.ps2', frames=17201, presses=['17010:lup:180'], shots=[17200],
         checks=[(check_moved, (17005, 17200, 300))]),
    dict(name='castle-hall', state='hall21900.ps2', frames=21961, presses=['21905:lup:50'], shots=[21960], checks=[]),
    dict(name='castle-door', state='hall22800.ps2', frames=22831, presses=[], shots=[22830], checks=[]),
    dict(name='castle-upstairs', state='upstairs28380.ps2', frames=28441, presses=[], shots=[28440], checks=[]),
]


def mean_p99(a, b):
    from PIL import Image, ImageChops
    if a.size != b.size:
        return 255.0, 255
    d = ImageChops.difference(a.convert('RGB'), b.convert('RGB')).convert('L')
    hist = d.histogram()
    total = sum(hist)
    mean = sum(i * n for i, n in enumerate(hist)) / total
    acc, p99 = 0, 0
    for i, n in enumerate(hist):
        acc += n
        if acc >= total * 0.99:
            p99 = i
            break
    return mean, p99


def run_case(case, record):
    from PIL import Image
    out = os.path.join(WORK, case['name'])
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    misses = os.path.join(out, 'misses.txt')
    cmd = [RUNNER, ELF, '--disc', ISO, '--memcard', os.path.join(out, 'card.ps2'), '--frames', str(case['frames']),
           '--frames-dir', out, '--frame-every', '1', '--misses', misses]
    if case['state']:
        cmd += ['--load-state', os.path.join(GAME, case['state'])]
    for p in case['presses']:
        cmd += ['--press', p]
    env = dict(os.environ, PS2_PEEK_FRAME=PLAYER_POS, PS2_FRAMES_ONLY=','.join(str(f) for f in case['shots']))
    started = time.time()
    proc = subprocess.run(cmd, cwd=ROOT, env=env, capture_output=True, text=True, errors='replace')
    seconds = time.time() - started
    log = proc.stderr
    with open(os.path.join(out, 'run.log'), 'w', encoding='utf-8') as f:
        f.write(log)
    problems = []
    if 'halted: frame limit reached' not in log:
        reason = re.search(r'\[ee\] halted: (.*)', log)
        problems.append('did not reach the frame limit' + (f' ({reason.group(1)})' if reason else f' (exit {proc.returncode})'))
    if '[crash]' in log:
        problems.append('crashed: ' + re.search(r'\[crash\].*', log).group(0))
    for pattern in (r'\[sys\] unhandled syscall.*', r'\[iop\] missing .*'):
        m = re.search(pattern, log)
        if m:
            problems.append(m.group(0))
    if os.path.exists(misses) and os.path.getsize(misses):
        problems.append('missing code: ' + open(misses).read().split()[0] + ' ...')
    peeks = {}
    for m in re.finditer(r'^\[peek\] (\d+) (\S+) (\S+) (\S+)', log, re.M):
        peeks[int(m.group(1))] = tuple(float(v) for v in m.groups()[1:])
    for check, args in case['checks']:
        result = check(peeks, args)
        if result:
            problems.append(result)
    shots = []
    for frame in case['shots']:
        ppm = os.path.join(out, f'frame_{frame:05d}.ppm')
        if not os.path.exists(ppm):
            problems.append(f'no screenshot for frame {frame}')
            continue
        image = Image.open(ppm)
        golden = os.path.join(GOLDEN, f"{case['name']}_{frame:05d}.png")
        if record:
            os.makedirs(GOLDEN, exist_ok=True)
            image.save(golden)
            shots.append(f'{frame}: recorded')
            continue
        if not os.path.exists(golden):
            problems.append(f'no golden frame for {frame} (run with --record)')
            continue
        mean, p99 = mean_p99(image, Image.open(golden))
        shots.append(f'{frame}: mean {mean:.2f} p99 {p99}')
        if mean > 0.25 or p99 > 12:
            image.save(os.path.join(out, f'actual_{frame:05d}.png'))
            problems.append(f'frame {frame} differs from golden (mean {mean:.2f}, p99 {p99}); see {os.path.relpath(out, ROOT)}')
    return problems, shots, seconds


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--record', action='store_true', help='replace the golden frames with this run')
    parser.add_argument('--only', help='comma-separated case names')
    parser.add_argument('--keep', action='store_true', help='keep every dumped frame')
    args = parser.parse_args()
    for need in (RUNNER, ELF, ISO):
        if not os.path.exists(need):
            sys.exit(f'missing {need}')
    selected = [c for c in CASES if not args.only or c['name'] in args.only.split(',')]
    failed = 0
    for case in selected:
        if case['state'] and not os.path.exists(os.path.join(GAME, case['state'])):
            print(f"SKIP {case['name']}: no save state {case['state']}")
            continue
        problems, shots, seconds = run_case(case, args.record)
        status = 'FAIL' if problems else 'ok  '
        failed += bool(problems)
        print(f"{status} {case['name']:<16} {seconds:5.1f}s  {'; '.join(shots)}")
        for p in problems:
            print(f'       - {p}')
        if not args.keep:
            out = os.path.join(WORK, case['name'])
            for name in os.listdir(out):
                if name.endswith('.ppm'):
                    os.remove(os.path.join(out, name))
    print(f'{len(selected) - failed}/{len(selected)} passed')
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
