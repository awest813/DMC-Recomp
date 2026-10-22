import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

// Execute the actual page worklet with a deterministic producer and render quanta.
const html = readFileSync(new URL('../web/shell.html', import.meta.url), 'utf8');
const source = html.match(/const workletSource = `([\s\S]*?)`;/)[1];
let Output;
vm.runInNewContext(source, {
  AudioWorkletProcessor: class {},
  registerProcessor: (name, processor) => { Output = processor; },
  Uint32Array, Int16Array, Atomics, Math,
});

function setup(counter = 0) {
  const capacity = 8192;
  const buffer = new SharedArrayBuffer(16 + capacity * 4);
  const ring = new Uint32Array(buffer, 0, 4);
  ring[0] = ring[1] = counter;
  ring[2] = capacity;
  const samples = new Int16Array(buffer, 16);
  const output = new Output({ processorOptions: { buffer, base: 0 } });
  function push(frames) {
    for (let n = 0; n < frames; ++n) {
      const at = (ring[0] & (capacity - 1)) * 2;
      samples[at] = 16384;
      samples[at + 1] = -8192;
      ring[0] = (ring[0] + 1) >>> 0;
    }
  }
  function render() {
    const left = new Float32Array(128), right = new Float32Array(128);
    assert.equal(output.process([], [[left, right]]), true);
    assert.ok(left.every(Number.isFinite));
    for (let n = 0; n < 128; ++n) assert.ok(right[n] === -left[n] / 2);
    return left;
  }
  return { ring, push, render };
}

for (const counter of [0, 0xFFFFFC00]) {
  const { ring, push, render } = setup(counter);
  assert.ok(render().every(v => v === 0), 'startup is silent before any PCM');
  push(32);
  assert.ok(render().every(v => v === 0), 'an isolated producer batch stays buffered');
  assert.equal(ring[1], counter, 'prebuffering must not discard PCM');
  push(2848);
  const first = render();
  assert.ok(Math.abs(first[0] - 0.5 / 240) < 1e-8, 'startup begins with a fade');
  for (let i = 1; i < first.length; ++i) assert.ok(first[i] > first[i - 1]);
  const second = render();
  assert.equal(second[127], 0.5, 'fade reaches the unmodified PCM level');
  for (let n = 0; n < 20; ++n) assert.ok(render().every(v => v === 0.5), 'steady PCM stays exact');
  const edge = render(); // 64 real frames remain, then the producer starves.
  assert.equal(edge[63], 0.5);
  assert.ok(Math.abs(edge[64] - edge[63]) < 0.003, 'underrun fades without a hard jump');
  render();
  assert.equal(render()[127], 0, 'underrun tail reaches silence');
  assert.equal(ring[1], ring[0], 'all available PCM was consumed exactly once');
  const stopped = ring[1];
  push(128);
  assert.ok(render().every(v => v === 0), 'recovery waits for a useful buffer');
  assert.equal(ring[1], stopped);
  push(2752);
  assert.ok(Math.abs(render()[0] - 0.5 / 240) < 1e-8, 'recovery fades in again');
  assert.equal(ring[3], 1, 'producer pacing remains active while buffering');
}
console.log('Web audio: startup, steady PCM, starvation, recovery, stereo and counter wrap passed');
