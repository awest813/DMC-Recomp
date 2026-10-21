#!/usr/bin/env python3
"""Greedy explorer for Devil May Cry: walks Dante into places he has not been, from a save state.

usage: python scripts/dmc_explore.py <start state> <steps> [--out dir] [--step-frames n]

Each step loads the current state, tries the four stick directions (each with a jump, or tapping Cross to open doors) in parallel
for --step-frames frames, and keeps the run whose end position is farthest from every position seen so
far. Its end state becomes the next checkpoint. Frames are dumped every 60 frames, and any run that
crashes, stops early or reports missing code is printed, since that is what this is for.
"""

import argparse
import concurrent.futures
import math
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.path.join(ROOT, 'games', 'dmc')
RUNNER = os.path.join(ROOT, 'build', 'native-dmc', 'ps2run.exe' if os.name == 'nt' else 'ps2run')
ELF = os.path.join(GAME, 'disc', 'SLUS_202.16')
ISO = os.path.join(os.path.dirname(ROOT), 'Devil May Cry.iso')
# Life_gauge_move shows the HUD when this game flag word has 0x80000 set: enemies are active.
BATTLE_FLAGS = '754130'
BATTLE_BIT = 0x80000


def state_frame(path):
    with open(path, 'rb') as f:
        head = f.read(64)
    m = re.search(rb'(\d{4,7})', os.path.basename(path).encode())
    return int(m.group(1)) if m else None


def run(state, start, frames, presses, out, save):
    os.makedirs(out, exist_ok=True)
    end = start + frames
    cmd = [RUNNER, ELF, '--disc', ISO, '--memcard', os.path.join(out, 'card.ps2'), '--load-state', state,
           '--frames', str(end + 1), '--save-state', f'{end}:{save}', '--frames-dir', out, '--frame-every', '60',
           '--misses', os.path.join(out, 'misses.txt')]
    for p in presses:
        cmd += ['--press', p]
    proc = subprocess.run(cmd, cwd=ROOT, env=dict(os.environ, PS2_PEEK_FRAME='5B72C0,3', PS2_PEEK_WORDS=BATTLE_FLAGS), capture_output=True, text=True, errors='replace')
    log = proc.stderr
    with open(os.path.join(out, 'run.log'), 'w', encoding='utf-8') as f:
        f.write(log)
    pos = {}
    for m in re.finditer(r'^\[peek\] (\d+) (\S+) (\S+) (\S+)', log, re.M):
        pos[int(m.group(1))] = tuple(float(v) for v in m.groups()[1:])
    battle = None
    for m in re.finditer(r'^\[peekw\] (\d+) ([0-9A-F]{8})', log, re.M):
        if int(m.group(2), 16) & BATTLE_BIT and battle is None:
            battle = int(m.group(1))
    problems = []
    if 'halted: frame limit reached' not in log:
        problems.append('stopped early: ' + (re.search(r'\[ee\] halted: (.*)', log) or re.search(r'\[crash\].*', log) or re.match('', '')).group(0))
    if os.path.exists(os.path.join(out, 'misses.txt')) and os.path.getsize(os.path.join(out, 'misses.txt')):
        problems.append('missing code')
    for pattern in (r'\[sys\] unhandled syscall.*', r'\[iop\] missing .*', r'\[crash\].*'):
        m = re.search(pattern, log)
        if m:
            problems.append(m.group(0))
    return pos, problems, battle


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('state')
    parser.add_argument('steps', type=int)
    parser.add_argument('--start-frame', type=int, help='frame number the state was saved at (default: digits in its name)')
    parser.add_argument('--out', default=os.path.join(GAME, 'explore'))
    parser.add_argument('--step-frames', type=int, default=300)
    parser.add_argument('--stop-at-battle', action='store_true', help='stop at the first step where an enemy fight starts')
    args = parser.parse_args()
    state = os.path.abspath(args.state)
    frame = args.start_frame or state_frame(state)
    if frame is None:
        sys.exit('pass --start-frame')
    os.makedirs(args.out, exist_ok=True)
    seen = []
    options = [[d, 'open'] for d in ('lup', 'ldown', 'lleft', 'lright')] + [[d, 'jump'] for d in ('lup', 'ldown', 'lleft', 'lright')]
    for step in range(args.steps):
        jobs = {}
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            for option in options:
                name = f'step{step:02d}_' + '_'.join(option)
                out = os.path.join(args.out, name)
                presses = [f'{frame + 2}:{option[0]}:{args.step_frames - 4}']
                if 'jump' in option:
                    presses += [f'{frame + 2 + k}:triangle:8' for k in range(40, args.step_frames - 40, 70)]
                if 'open' in option:
                    presses += [f'{frame + 2 + k}:cross:6' for k in range(20, args.step_frames - 20, 40)]
                save = os.path.join(args.out, name + '.ps2')
                jobs[pool.submit(run, state, frame, args.step_frames, presses, out, save)] = (name, save)
        best, best_score = None, -1.0
        fights = []
        for future, (name, save) in jobs.items():
            pos, problems, battle = future.result()
            if battle is not None:
                fights.append((battle, name, save))
            for p in problems:
                print(f'  !! {name}: {p}')
            end = pos.get(frame + args.step_frames)
            if not end or problems or not os.path.exists(save):
                continue
            # height counts four times: stairs and ledges lead on, and flat wandering does not
            score = min((math.dist((end[0], 4 * end[1], end[2]), (s[0], 4 * s[1], s[2])) for s in seen), default=1e9)
            if end[1] < -40000:
                score = -1
            if score > best_score:
                best, best_score, best_pos = (name, save), score, end
        if fights:
            fights.sort()
            print(f'battle started at frame {fights[0][0]} in {fights[0][1]}; state {fights[0][2]}')
            if args.stop_at_battle:
                break
        if not best:
            print('no usable run; stopping')
            break
        seen.append(best_pos)
        state = best[1]
        frame += args.step_frames
        print(f'step {step:2d}: {best[0]:<28} -> ({best_pos[0]:.0f}, {best_pos[1]:.0f}, {best_pos[2]:.0f}) novelty {best_score:.0f}', flush=True)
    print('last state:', state)


if __name__ == '__main__':
    main()
