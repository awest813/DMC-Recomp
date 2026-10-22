import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const html = readFileSync(new URL('../web/shell.html', import.meta.url), 'utf8');
const body = html.match(/preRun: \[\(\) => \{([\s\S]*?)\n  \}\],/)?.[1];
assert.ok(body, 'Could not find the browser startup hook');
for (const failure of ['none', 'mount', 'sync-throw', 'sync-callback', 'network', 'no-disc', 'no-card']) {
  const dependencies = new Set(), elements = new Map();
  const element = id => { if (!elements.has(id)) elements.set(id, {}); return elements.get(id); };
  let aborted = false, completed = 0;
  const Module = {
    ENV: {}, noCard: failure === 'no-card',
    addRunDependency: name => { assert.ok(!dependencies.has(name)); dependencies.add(name); },
    removeRunDependency: name => {
      assert.ok(dependencies.delete(name), 'Dependency removed twice: ' + name);
      if (!dependencies.size) { ++completed; assert.ok(Module.gpuInfo || aborted, 'Runtime became ready before disc preparation'); }
    },
    onAbort: () => { aborted = true; },
  };
  const hook = vm.runInNewContext(`(() => {${body}\n})`, {
    Module, CARD_DIR: '/memcard', IDBFS: {}, scale: '1', params: new URLSearchParams(), missing: [],
    document: { getElementById: element }, console: { warn() {} },
    chooseGpu: async () => 'Test GPU',
    fetch: async () => {
      if (failure === 'network') throw new Error('Test network failure');
      return { ok: failure !== 'no-disc', headers: { get: () => '10' } };
    },
    FS: {
      mkdir() {},
      mount() { if (failure === 'mount') throw new Error('Test mount failure'); },
      syncfs(_, done) {
        if (failure === 'sync-throw') throw new Error('Test storage failure');
        done(failure === 'sync-callback' ? new Error('Test storage failure') : null);
      },
    },
  });
  hook();
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(dependencies.size, 0, failure + ' left loading stuck');
  assert.equal(completed, 1, failure + ' completed initialization incorrectly');
  assert.equal(aborted, failure === 'network');
  if (failure !== 'network') assert.equal(Module.discSize, failure === 'no-disc' ? 0 : 10);
  if (['mount', 'sync-throw', 'sync-callback'].includes(failure)) assert.match(element('card-status').textContent, /unavailable|could not/i);
}
console.log('Browser startup: storage mount/read failures, dependency ordering, missing disc and network failure passed');
