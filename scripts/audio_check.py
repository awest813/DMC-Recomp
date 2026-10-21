#!/usr/bin/env python3
"""Summarizes PS2_DUMP_AUDIO recordings: loudness over time, clipping, DC offset, dropouts, stereo, spectrogram.

usage: python scripts/audio_check.py file.wav [...] [--png out_dir]
"""

import argparse
import math
import os
import struct
import sys
import wave


def load(path):
    with wave.open(path, 'rb') as w:
        rate, channels, width, n = w.getframerate(), w.getnchannels(), w.getsampwidth(), w.getnframes()
        raw = w.readframes(n)
    if width != 2:
        sys.exit(f'{path}: expected 16-bit samples')
    samples = struct.unpack(f'<{n * channels}h', raw)
    left = samples[0::channels]
    right = samples[1::channels] if channels > 1 else left
    return rate, left, right


def db(v):
    return 20 * math.log10(v / 32768) if v > 0 else -120.0


def analyze(path, png_dir):
    rate, left, right = load(path)
    n = len(left)
    seconds = n / rate
    window = rate // 20
    levels = []
    for i in range(0, n - window + 1, window):
        acc = 0
        for k in range(i, i + window, 4):
            acc += left[k] * left[k] + right[k] * right[k]
        levels.append(math.sqrt(acc / (window // 4 * 2)))
    peak = max(max(abs(v) for v in left), max(abs(v) for v in right)) if n else 0
    clipped = sum(1 for v in left if abs(v) >= 32767) + sum(1 for v in right if abs(v) >= 32767)
    dc = (sum(left) / n, sum(right) / n) if n else (0, 0)
    silent = sum(1 for v in levels if db(v) < -60)
    # a dropout: a 50 ms window more than 30 dB below both neighbours while they are clearly audible
    dropouts = [i for i in range(1, len(levels) - 1)
                if db(levels[i - 1]) > -40 and db(levels[i + 1]) > -40 and db(levels[i]) < min(db(levels[i - 1]), db(levels[i + 1])) - 30]
    lr = sum(a * b for a, b in zip(left[::8], right[::8]))
    ll = sum(a * a for a in left[::8]) or 1
    rr = sum(b * b for b in right[::8]) or 1
    corr = lr / math.sqrt(ll * rr)
    print(f'{os.path.basename(path)}: {seconds:.1f}s @ {rate} Hz, peak {db(peak):.1f} dBFS, clipped samples {clipped}, '
          f'DC {dc[0]:.0f}/{dc[1]:.0f}, silent 50ms windows {silent}/{len(levels)}, dropouts {len(dropouts)}, L/R correlation {corr:.2f}')
    line = ''.join(' .:-=+*#%@'[min(9, max(0, int((db(v) + 70) / 7)))] for v in levels[::4])
    print('  loudness (each char 200 ms): ' + line)
    if dropouts:
        print('  dropouts at: ' + ', '.join(f'{i * 0.05:.2f}s' for i in dropouts[:12]))
    if png_dir:
        spectrogram(left, right, rate, os.path.join(png_dir, os.path.splitext(os.path.basename(path))[0] + '_spec.png'))


def spectrogram(left, right, rate, out):
    import numpy as np
    from PIL import Image
    mono = (np.asarray(left, dtype=np.float32) + np.asarray(right, dtype=np.float32)) / 65536.0
    size, hop = 2048, 1024
    frames = max(1, (len(mono) - size) // hop)
    win = np.hanning(size).astype(np.float32)
    cols = []
    for i in range(frames):
        spec = np.abs(np.fft.rfft(mono[i * hop:i * hop + size] * win))
        cols.append(20 * np.log10(spec + 1e-6))
    img = np.array(cols).T[::-1]
    img = np.clip((img + 100) / 100 * 255, 0, 255).astype(np.uint8)
    Image.fromarray(img).resize((min(1600, img.shape[1]), 400)).save(out)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('files', nargs='+')
    parser.add_argument('--png')
    args = parser.parse_args()
    for f in args.files:
        analyze(f, args.png)


if __name__ == '__main__':
    main()
