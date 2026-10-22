// Real static hosting smoke test. The supplied ISO remains local.
// node scripts/test_pages_browser.mjs <picker-url> <disc.iso> [cdp-port]
import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { writeFileSync } from 'node:fs';
const [url, iso, port = '9333'] = process.argv.slice(2);
if (!iso) throw new Error('Supply picker URL and local ISO');
const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
const ws = new WebSocket(targets.find(t => t.type === 'page').webSocketDebuggerUrl);
await new Promise(done => ws.addEventListener('open', done, { once: true }));
let serial = 0;
const pending = new Map();
ws.addEventListener('message', event => {
  const message = JSON.parse(event.data);
  if (message.id) { pending.get(message.id)?.(message); pending.delete(message.id); }
});
const send = (method, params = {}) => new Promise((done, reject) => {
  const id = ++serial, timer = setTimeout(() => { pending.delete(id); reject(new Error('CDP timeout: ' + method)); }, 20000);
  pending.set(id, message => { clearTimeout(timer); message.error ? reject(new Error(JSON.stringify(message.error))) : done(message.result); });
  ws.send(JSON.stringify({ id, method, params }));
});
const run = async expression => {
  const result = await send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true, userGesture: true });
  if (result.exceptionDetails) throw new Error(result.exceptionDetails.exception?.description || result.exceptionDetails.text);
  return result.result?.value;
};
const wait = async expression => {
  for (let i = 0; i < 240; ++i) {
    try { if (await run(expression)) return; } catch (e) { if (!/context|navigat/i.test(e.message)) throw e; }
    await new Promise(done => setTimeout(done, 250));
  }
  throw new Error('Timed out: ' + expression + '\n' + await run('document.body.innerText'));
};
const pick = async (selector, paths) => {
  const { root } = await send('DOM.getDocument');
  const { nodeId } = await send('DOM.querySelector', { nodeId: root.nodeId, selector });
  await send('DOM.setFileInputFiles', { nodeId, files: paths.map(path => resolve(path)) });
};
try {
  await send('Page.enable'); await send('Runtime.enable');
  await send('Page.navigate', { url });
  await wait(`document.getElementById('status')?.textContent.includes('Choose your disc to continue')`);
  assert.equal(await run('crossOriginIsolated'), true, 'Pages must work without server isolation headers');
  const shot = await send('Page.captureScreenshot', { format: 'png' });
  writeFileSync('build/pages-picker.png', Buffer.from(shot.data, 'base64'));
  await send('Emulation.setDeviceMetricsOverride', { width: 390, height: 844, deviceScaleFactor: 1, mobile: false });
  assert.equal(await run('document.documentElement.scrollWidth <= innerWidth'), true);
  await send('Emulation.clearDeviceMetricsOverride');
  await pick('#iso', [iso]);
  await wait(`document.getElementById('iso-status').textContent.includes('SLUS_202.16')`);
  assert.equal(await run(`document.getElementById('runtime') === null`), true, 'Player files must be included');
  await wait(`!document.getElementById('play').disabled`);
  await run(`document.getElementById('play').click()`);
  await wait(`document.getElementById('game').contentWindow.Module?.discSize > 0 && !document.getElementById('game').contentDocument.getElementById('start').disabled`);
  assert.equal(await run(`document.getElementById('game').contentWindow.crossOriginIsolated`), true);
  const base = await run(`document.getElementById('game').src.replace(/ps2web.html$/, '')`);
  const range = await run(`fetch(${JSON.stringify(base + 'disc.iso')}, {headers:{Range:'bytes=32769-32773'}}).then(async r => ({status:r.status,body:await r.text(),range:r.headers.get('Content-Range')}))`);
  assert.equal(range.status, 206); assert.equal(range.body, 'CD001'); assert.match(range.range, /^bytes 32769-32773\//);
  await send('ServiceWorker.enable');
  await send('ServiceWorker.stopAllWorkers');
  assert.equal(await run(`fetch(${JSON.stringify(base + 'disc.iso')}, {method:'HEAD'}).then(r => r.status)`), 200, 'Local files must survive service worker termination');
  assert.equal(await run(`fetch(${JSON.stringify(base + 'disc.iso')}, {headers:{Range:'bytes=999999999999-'}}).then(r => r.status)`), 416);
  assert.equal(await run(`fetch(${JSON.stringify(base + 'vu1/test')}, {method:'PUT',body:'test'}).then(r => r.status)`), 404, 'Recording requests must not leave the local session');
  await run(`document.getElementById('game').contentDocument.getElementById('start').click()`);
  await wait(`document.getElementById('game').contentWindow.Module?._ps2web_frames() > 120`);
  await run(`window.confirm = () => true; document.getElementById('back').click()`);
  assert.equal(await run(`fetch(${JSON.stringify(base + 'disc.iso')}, {method:'HEAD'}).then(r => r.status)`), 503, 'Closed session must not retain access to files');
  console.log('Pages browser: header-free isolation, subpath hosting, mobile layout, local ISO, range reads and real gameplay passed');
} finally { ws.close(); }
