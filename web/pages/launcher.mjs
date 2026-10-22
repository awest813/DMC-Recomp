import { allowedNames, runtimeNames, inspectIso, fileReply } from './files.mjs';
const byId = id => document.getElementById(id);
const files = new Map();
let session = crypto.randomUUID(), ready = false, validating = false, selection = 0;
const status = text => { byId('status').textContent = text; };
const update = () => {
  const complete = files.has('disc.iso') && runtimeNames.every(name => files.has(name));
  byId('play').disabled = !ready || validating || !complete;
  if (ready) status(validating ? 'Checking your disc…' : complete ? 'Ready. Your files stay on this device.' : 'Choose your disc and browser build to continue.');
};

navigator.serviceWorker?.addEventListener('message', event => {
  const request = event.data, port = event.ports[0];
  if (!port || request?.type !== 'local-file') return;
  const file = request.session === session && allowedNames.has(request.name) ? files.get(request.name) : null;
  try { port.postMessage(file ? fileReply(file, request.name, request.method, request.range) : { missing: true }); }
  catch { port.postMessage({ missing: true }); }
  finally { port.close(); }
});

byId('iso').addEventListener('change', async event => {
  const version = ++selection, file = event.target.files[0];
  files.delete('disc.iso'); validating = !!file; update();
  if (!file) { byId('iso-status').textContent = 'No disc selected'; return; }
  try {
    const title = await inspectIso(file);
    if (version !== selection) return;
    files.set('disc.iso', file);
    byId('iso-status').textContent = `${title} · ${(file.size / 1073741824).toFixed(2)} GB`;
  } catch (error) {
    if (version !== selection) return;
    byId('iso-status').textContent = error.message;
  } finally { if (version === selection) { validating = false; update(); } }
});

byId('runtime').addEventListener('change', event => {
  for (const name of allowedNames) if (name !== 'disc.iso') files.delete(name);
  for (const file of event.target.files) if (allowedNames.has(file.name) && file.name !== 'disc.iso' && file.size) files.set(file.name, file);
  const missing = runtimeNames.filter(name => !files.has(name));
  byId('runtime-status').textContent = missing.length ? 'Still needed: ' + missing.join(', ') : 'Browser build selected · ' + (runtimeNames.reduce((n, name) => n + files.get(name).size, 0) / 1048576).toFixed(1) + ' MB';
  update();
});

byId('play').addEventListener('click', () => {
  if (byId('play').disabled) return;
  byId('game').src = new URL(`play/${session}/ps2web.html`, location.href).href;
  byId('picker').hidden = true; byId('session').hidden = false;
  byId('game').focus();
});
byId('back').addEventListener('click', () => {
  if (!confirm('Close the game? Save in-game first.')) return;
  byId('game').src = 'about:blank';
  session = crypto.randomUUID();
  byId('session').hidden = true; byId('picker').hidden = false;
  byId('play').focus();
});

async function prepare() {
  if (!isSecureContext || !('serviceWorker' in navigator)) throw new Error('Open this page over HTTPS in a current Chrome or Edge browser.');
  if (typeof WebAssembly.Suspending !== 'function') throw new Error('This browser lacks WebAssembly JSPI. Use a current desktop Chrome or Edge.');
  await navigator.serviceWorker.register(new URL('service-worker.js', location.href), { scope: './', updateViaCache: 'none' });
  await navigator.serviceWorker.ready;
  if (!navigator.serviceWorker.controller) {
    await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('Browser setup timed out. Reload this page to retry.')), 10000);
      navigator.serviceWorker.addEventListener('controllerchange', () => { clearTimeout(timer); resolve(); }, { once: true });
      if (navigator.serviceWorker.controller) { clearTimeout(timer); resolve(); }
    });
  }
  if (!crossOriginIsolated) {
    if (sessionStorage.getItem('dmc-isolation-reload')) throw new Error('Browser isolation is unavailable. Close other tabs for this site and reload.');
    sessionStorage.setItem('dmc-isolation-reload', '1');
    location.reload(); return;
  }
  sessionStorage.removeItem('dmc-isolation-reload');
  ready = true; update();
}
prepare().catch(error => status(error.message));
