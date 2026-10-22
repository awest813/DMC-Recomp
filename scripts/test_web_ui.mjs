// Exercise the built player in an isolated Chrome profile, using its real runtime.
// Chrome must already expose CDP. Usage: node scripts/test_web_ui.mjs <url> [port]
import assert from 'node:assert/strict';
import { writeFileSync } from 'node:fs';
const [url, port = '9333'] = process.argv.slice(2);
if (!url) throw new Error('Usage: node scripts/test_web_ui.mjs <page-url> [debug-port]');
const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
const ws = new WebSocket(targets.find(t => t.type === 'page').webSocketDebuggerUrl);
await new Promise(resolve => ws.addEventListener('open', resolve, { once: true }));
let serial = 0;
const pending = new Map(), exceptions = [];
ws.addEventListener('message', event => {
  const m = JSON.parse(event.data);
  if (m.id) { pending.get(m.id)?.(m); pending.delete(m.id); }
  if (m.method === 'Runtime.exceptionThrown') exceptions.push(m.params.exceptionDetails);
});
const send = (method, params = {}) => new Promise((resolve, reject) => {
  const id = ++serial;
  const timer = setTimeout(() => { pending.delete(id); reject(new Error('CDP timed out: ' + method)); }, 15000);
  pending.set(id, m => { clearTimeout(timer); m.error ? reject(new Error(JSON.stringify(m.error))) : resolve(m.result); });
  ws.send(JSON.stringify({ id, method, params }));
});
const run = async expression => {
  const result = await send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true, userGesture: true });
  if (result.exceptionDetails) throw new Error(result.exceptionDetails.exception?.description || result.exceptionDetails.text);
  return result.result?.value;
};
const wait = async expression => {
  for (let i = 0; i < 200; ++i) {
    if (await run(expression)) return;
    await new Promise(resolve => setTimeout(resolve, 200));
  }
  throw new Error('Timed out: ' + expression);
};
try {
  await send('Runtime.enable');
  await send('Page.enable');
  await send('Emulation.setDeviceMetricsOverride', { width: 1280, height: 900, deviceScaleFactor: 1, mobile: false });
  await send('Page.navigate', { url });
  await wait(`document.getElementById('start') && !document.getElementById('start').disabled`);
  assert.equal(await run('!!Module.failed'), false);
  const shot = await send('Page.captureScreenshot', { format: 'png' });
  writeFileSync('build/browser-player-ready.png', Buffer.from(shot.data, 'base64'));
  await run(`document.getElementById('settings-open').click()`);
  assert.equal(await run(`document.getElementById('settings-dialog').matches(':modal')`), true);
  assert.equal(await run(`document.getElementById('apply-display').disabled`), true);
  await run(`scaleSelect.value = scale === '1' ? '2' : '1'; scaleSelect.dispatchEvent(new Event('change'))`);
  assert.equal(await run('applyDisplay.disabled'), false);
  assert.equal(await run('Module.running === true'), false);
  await run(`scaleSelect.value = scale; scaleSelect.dispatchEvent(new Event('change')); volumeInput.value = 35; volumeInput.dispatchEvent(new Event('input'))`);
  assert.equal(await run(`document.getElementById('volume-value').textContent`), '35%');
  await run(`note('Test storage notification')`);
  assert.equal(await run(`document.getElementById('toast').matches(':popover-open')`), true);
  await run(`document.getElementById('toast').hidePopover(); document.getElementById('toast').hidden=true`);
  await run(`window.cardTest = { saved: FS.syncfs, flushes: [], results: [] }; FS.syncfs = (_, done) => cardTest.flushes.push(done); storeCard(err => cardTest.results.push(err ? 'error' : 'import')); storeCard()`);
  await wait('cardTest.flushes.length === 1');
  await run(`storeCard(err => cardTest.results.push(err ? 'error' : 'second'))`);
  await new Promise(resolve => setTimeout(resolve, 450));
  assert.equal(await run('cardTest.flushes.length'), 1, 'Storage flushes overlapped');
  await run(`cardTest.flushes[0](null)`);
  await wait('cardTest.flushes.length === 2');
  assert.deepEqual(await run('cardTest.results'), ['import'], 'Autosave cancelled the import callback');
  await run(`cardTest.flushes[1](new Error('Test storage failure')); FS.syncfs = cardTest.saved`);
  assert.deepEqual(await run('cardTest.results'), ['import', 'error']);
  for (const [width, height] of [[390, 844], [844, 390]]) {
    await send('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 1, mobile: false });
    assert.equal(await run(`document.documentElement.scrollWidth <= innerWidth`), true);
    assert.equal(await run(`(() => {const r=openPanel.getBoundingClientRect();return r.left>=0 && r.right<=innerWidth && r.top>=0 && r.bottom<=innerHeight})()`), true);
    const mobileShot = await send('Page.captureScreenshot', { format: 'png' });
    writeFileSync(`build/browser-settings-${width}.png`, Buffer.from(mobileShot.data, 'base64'));
  }
  await run(`document.getElementById('settings-dialog').close()`);
  await wait('openPanel === null');
  await send('Emulation.setDeviceMetricsOverride', { width: 390, height: 600, deviceScaleFactor: 1, mobile: false });
  await run(`document.body.classList.add('touch'); updateControls()`);
  assert.equal(await run(`getComputedStyle(document.body).overflowY`), 'auto', 'Short touch layouts cannot scroll');
  await run(`document.body.classList.remove('touch'); updateControls()`);
  await send('Emulation.setDeviceMetricsOverride', { width: 1280, height: 900, deviceScaleFactor: 1, mobile: false });
  await run(`document.getElementById('controls-open').click()`);
  assert.match(await run(`document.querySelector('[data-binding="circle"]').textContent`), /X/);
  await run(`document.getElementById('controls-dialog').close()`);
  await wait('openPanel === null');
  await run(`document.getElementById('start').click()`);
  await wait('Module.running && Module._ps2web_frames() > 17010');
  assert.ok(Math.abs(await run('gainNode.gain.value') - 0.35) < 0.00001);
  assert.equal(await run(`startAudio() === startAudio()`), true, 'Audio starts were not serialized');
  await run(`audio.suspend()`);
  await wait(`Atomics.load(new Uint32Array(Module.HEAPU8.buffer, Module._ps2web_audio_ring(), 4), 3) === 0`);
  const suspendedFrame = await run('Module._ps2web_frames()');
  await wait(`Module._ps2web_frames() > ${suspendedFrame + 10}`);
  await run(`audio.resume()`);
  await wait(`Atomics.load(new Uint32Array(Module.HEAPU8.buffer, Module._ps2web_audio_ring(), 4), 3) === 1`);
  await run(`window.testPad = {buttons:[{pressed:true,value:1}],axes:[0,0,0,0]}; pollPlayer(1, testPad, false); pollPlayer(1, testPad, false, true)`);
  assert.equal(await run('players[1].plugged'), true, 'Menu disconnected player 2');
  assert.equal(await run('players[1].pad % 65536'), 0, 'Menu retained controller buttons');
  await run('pollInput()');
  await run(`canvas.dispatchEvent(new KeyboardEvent('keydown', {code:'KeyF', bubbles:true, cancelable:true}))`);
  assert.equal(await run('fullscreen'), false);
  assert.equal(await run(`keys.has('KeyF')`), true);
  await run(`document.getElementById('settings-open').click()`);
  assert.equal(await run('keys.size'), 0);
  await run(`volumeInput.dispatchEvent(new KeyboardEvent('keydown', {code:'ArrowRight', bubbles:true, cancelable:true}))`);
  assert.equal(await run('keys.size'), 0);
  const before = await run('Module._ps2web_frames()');
  await wait(`Module._ps2web_frames() > ${before + 30}`);
  await run(`document.getElementById('settings-dialog').close()`);
  await wait('openPanel === null');
  await run(`muteButton.click()`);
  await wait(`muteButton.getAttribute('aria-pressed') === 'true'`);
  await run(`muteButton.click()`);
  await wait(`muteButton.getAttribute('aria-pressed') === 'false'`);
  assert.equal(await run('document.activeElement === canvas'), true, 'Sound toolbar retained game keyboard focus');
  await run(`fullscreenButton.click()`);
  await wait('fullscreen');
  assert.equal(await run(`getComputedStyle(document.getElementById('bar')).display`), 'none');
  await run(`document.getElementById('exit-fullscreen').click()`);
  await wait('!fullscreen');
  assert.equal(await run('document.activeElement === canvas'), true, 'Fullscreen toolbar retained game keyboard focus');
  await run(`audio.close(); node = null; gainNode = null; muted = false; updateSound(); window.savedAudioContext = AudioContext; window.AudioContext = function () { throw new Error('Test audio device failure'); }`);
  assert.equal(await run('muteButton.textContent'), 'Retry sound');
  await run('muteButton.click()');
  await wait('!muteButton.disabled && audioStarting === null');
  assert.equal(await run('node'), null);
  await run('window.AudioContext = window.savedAudioContext; muteButton.click()');
  await wait('!!node && !muteButton.disabled');
  assert.equal(await run('muted'), false, 'Recovered sound was immediately muted');
  assert.ok(Math.abs(await run('gainNode.gain.value') - 0.35) < 0.00001);
  assert.deepEqual(exceptions, []);
  console.log('Browser UI: ready screen, dialogs, responsive layout, storage serialization, audio suspend/retry, volume, mute, controller/input isolation and live gameplay passed');
} finally { ws.close(); }
