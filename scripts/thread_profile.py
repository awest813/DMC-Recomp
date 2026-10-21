#!/usr/bin/env python3
"""Symbolize a PS2_THREAD_PROFILE dump from the native window (host/thread_profile.cpp).

usage: thread_profile.py <ps2run.exe> <profile.txt> [top] [--thread name]
"""
import bisect
import collections
import subprocess
import sys


def symbols(exe):
    out = subprocess.run(['llvm-nm', '-C', '--defined-only', '--numeric-sort', exe], capture_output=True, text=True, check=True).stdout
    addrs, names = [], []
    for line in out.splitlines():
        parts = line.split(' ', 2)
        if len(parts) < 3 or parts[1] not in 'tTwW':
            continue
        addrs.append(int(parts[0], 16))
        names.append(parts[2])
    return addrs, names


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    only = None
    if '--thread' in sys.argv:
        only = sys.argv[sys.argv.index('--thread') + 1]
        args.remove(only)
    exe, path = args[0], args[1]
    top = int(args[2]) if len(args) > 2 else 40
    addrs, names = symbols(exe)
    image = 0x140000000
    exe_name = exe.replace('\\', '/').rsplit('/', 1)[-1].lower()
    per = collections.defaultdict(collections.Counter)
    totals = collections.Counter()
    for line in open(path):
        if line.startswith('#'):
            continue
        thread, module, rva, count = line.split()
        if only and thread != only:
            continue
        count = int(count)
        totals[thread] += count
        if module.lower() == exe_name:
            i = bisect.bisect_right(addrs, image + int(rva, 16)) - 1
            name = names[i] if i >= 0 else '?'
            if name.startswith('gen::vu1_') and '--programs' not in sys.argv:
                name = 'gen::vu1_* (AOT VU1 programs)'
            elif name.startswith('gen::') and not name.startswith('gen::vu1_'):
                name = 'gen::* (EE code)'
        else:
            name = '[' + module + ']'
        per[thread][name] += count
    for thread, counter in per.items():
        print(f'== {thread}: {totals[thread]} samples')
        for name, n in counter.most_common(top):
            print(f'{100.0 * n / totals[thread]:6.2f}%  {name[:150]}')


if __name__ == '__main__':
    main()
