#!/usr/bin/env python3
"""Compares two PS2_PROBE_COUNT logs from a probe build, naming each address from the game's symbols.

usage: python scripts/probediff.py a.log b.log [--syms games/dmc/syms.txt]
Prints address, symbol, count in a, count in b; sorted by count in b. Make the symbol file with
llvm-nm -n <elf> > games/dmc/syms.txt.
"""

import argparse
import re


def load(path):
    counts = {}
    for line in open(path, errors='replace'):
        m = re.match(r'\[probe\] 0x([0-9A-F]+) (\d+)$', line.strip())
        if m:
            counts[int(m.group(1), 16)] = int(m.group(2))
    return counts


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('a')
    parser.add_argument('b')
    parser.add_argument('--syms', default='games/dmc/syms.txt')
    args = parser.parse_args()
    names = {}
    for line in open(args.syms):
        p = line.split()
        if len(p) == 3:
            names.setdefault(int(p[0], 16), p[2])
    a, b = load(args.a), load(args.b)
    for k in sorted(set(a) | set(b), key=lambda k: -b.get(k, 0)):
        print(f'{k:08x} {names.get(k, "?"):40s} {a.get(k, 0):8d} {b.get(k, 0):8d}')


if __name__ == '__main__':
    main()
