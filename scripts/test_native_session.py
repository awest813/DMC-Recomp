"""Game-dependent native pacing, quick-save/resume and frame-equivalence checks.

Requires the local DMC build, disc and ground17000.ps2. Outputs remain under games/.
The bounded window run also logs pacing; --profile adds a separate sampling run.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--profile', action='store_true')
parser.add_argument('--seconds', type=int, default=12, help='window pacing run duration in nominal game seconds')
args = parser.parse_args()
if args.seconds < 3:
    parser.error('--seconds must be at least 3 so the quick-save point is reached')
root = Path(__file__).resolve().parents[1]
out = root / 'games/dmc/native-stability'
out.mkdir(parents=True, exist_ok=True)
runner = root / 'build/native-dmc/ps2run.exe'
base = [str(runner), str(root / 'games/dmc/disc/SLUS_202.16'), '--disc', str(root.parent / 'Devil May Cry.iso'), '--memcard', str(out / 'test-card.ps2')]
env = os.environ.copy()
for name in ('PS2_TIME', 'PS2_ONE_THREAD', 'PS2_NO_VU_THREAD', 'PS2_THREAD_PROFILE', 'PS2_QUICKSAVE_AT', 'PS2_QUICKSAVE', 'PS2_KEYS', 'PS2_FRAMES_ONLY', 'PS2_DUMP', 'PS2_DUMP_AUDIO', 'PS2_DUMP_VRAM'):
    env.pop(name, None)
env.update(PS2_SCALE='2', PS2_GS='gpu')
card_fixture = out / 'protocol-card.ps2'
card_fixture.write_bytes(bytes([0x12, 0x34, 0x56]) + bytes([0xFF]) * (8650752 - 3))
subprocess.run([str(root / 'build/native-dmc/ps2_native_host.exe'), '--read-card', str(card_fixture)], cwd=root, check=True, timeout=10)

def run(name, options, extra=None, expect_success=True):
    run_env = env | (extra or {})
    start = time.monotonic()
    with (out / (name + '.log')).open('w', encoding='utf-8') as log:
        result = subprocess.run(base + options, cwd=root, env=run_env, stdout=log, stderr=subprocess.STDOUT, timeout=max(90, args.seconds + 30))
    text = (out / (name + '.log')).read_text(encoding='utf-8', errors='replace')
    if expect_success:
        assert result.returncode == 0 and 'frame limit reached' in text, name + ' did not finish normally'
        assert 'unhandled syscall' not in text and 'missing code' not in text, name + ' encountered unimplemented code'
    else:
        assert result.returncode != 0, name + ' should refuse an invalid checkpoint'
    print(f'{name}: {time.monotonic() - start:.2f}s', flush=True)
    return text

checkpoint = out / 'quicksave.ps2'
text = run('window', ['--window', '--load-state', str(root / 'games/dmc/ground17000.ps2'), '--frames', str(17001 + args.seconds * 5994 // 100)],
           {'PS2_QUICKSAVE': str(checkpoint), 'PS2_QUICKSAVE_AT': '17100', 'PS2_PACE_STATS': '1'})
assert re.search(r'\[state\] saved frame 17\d+ to ', text), 'F5 request path did not save'
assert checkpoint.stat().st_size > 32 * 1024 * 1024, 'incomplete checkpoint'
assert '[pace]' in text and 'clock audio' in text, 'audio pacing was not measured'
for line in text.splitlines():
    if line.startswith('[pace]'):
        print(line, flush=True)
for name, state in [('reference', root / 'games/dmc/ground17000.ps2'), ('resumed', checkpoint)]:
    frames = out / name
    frames.mkdir(exist_ok=True)
    run(name, ['--load-state', str(state), '--frames', '17221', '--frames-dir', str(frames), '--frame-every', '1'], {'PS2_FRAMES_ONLY': '17220'})
assert (out / 'reference/frame_17220.ppm').read_bytes() == (out / 'resumed/frame_17220.ppm').read_bytes(), 'resumed frame differs from uninterrupted execution'
invalid = out / 'invalid.ps2'
invalid.write_bytes(b'incomplete checkpoint')
run('invalid', ['--load-state', str(invalid), '--frames', '17221'], expect_success=False)
if args.profile:
    profile = out / 'threads.txt'
    run('profile', ['--window', '--load-state', str(root / 'games/dmc/ground17000.ps2'), '--frames', '17901'], {'PS2_THREAD_PROFILE': str(profile), 'PS2_PACE_STATS': '1'})
    with (out / 'profile-summary.txt').open('w', encoding='utf-8') as summary:
        subprocess.run(['python', str(root / 'scripts/thread_profile.py'), str(runner), str(profile), '12'], cwd=root, stdout=summary, check=True)
    print((out / 'profile-summary.txt').read_text(encoding='utf-8'), flush=True)
print('Native session: threaded quick-save, exact resumed frame, invalid-load refusal and bounded pacing passed', flush=True)
