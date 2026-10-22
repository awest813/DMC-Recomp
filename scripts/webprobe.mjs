// Drives the web build in headless Chrome over the DevTools protocol and samples the status line.
// usage: node scripts/webprobe.mjs <page url> <seconds> [--shot out.png] [--every ms] [--profile seconds-from-start] [--nogpu]
// --nogpu hides navigator.gpu from the page so it takes its no-WebGPU (software renderer) path.
// --profile samples every worker thread (the emulator, VU1 and GS threads are workers) with the
// DevTools CPU profiler from that point to the end and prints each worker's hottest functions.
// Needs Chrome already running with --remote-debugging-port=9333 (see scripts/webprobe.ps1).
const [url, seconds = '60', ...rest] = process.argv.slice(2);
const opt = (name, fallback) => (rest.includes(name) ? rest[rest.indexOf(name) + 1] : fallback);
const shot = opt('--shot', null);
const every = Number(opt('--every', '1000'));
const profileFrom = opt('--profile', null);

const targets = await (await fetch('http://127.0.0.1:9333/json/list')).json();
const page = targets.find((t) => t.type === 'page');
const ws = new WebSocket(page.webSocketDebuggerUrl);
await new Promise((r) => ws.addEventListener('open', r, { once: true }));
let id = 0;
const pending = new Map();
const logs = [];
ws.addEventListener('message', (e) => {
  const m = JSON.parse(e.data);
  if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
  if (m.method === 'Runtime.consoleAPICalled') logs.push(m.params.args.map((a) => a.value ?? a.description).join(' '));
  if (m.method === 'Runtime.exceptionThrown') logs.push('EXCEPTION ' + m.params.exceptionDetails.text + ' ' + (m.params.exceptionDetails.exception?.description ?? ''));
});
const send = (method, params = {}, sessionId) => new Promise((r) => { const n = ++id; pending.set(n, r); ws.send(JSON.stringify({ id: n, method, params, ...(sessionId ? { sessionId } : {}) })); });
const workers = [];
ws.addEventListener('message', (e) => {
  const m = JSON.parse(e.data);
  if (m.method === 'Target.attachedToTarget' && m.params.targetInfo.type === 'worker') {
    workers.push(m.params.sessionId);
    send('Runtime.runIfWaitingForDebugger', {}, m.params.sessionId);
  }
});
const evaluate = async (expression) => (await send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true })).result?.result?.value;

await send('Runtime.enable');
await send('Page.enable');
if (profileFrom !== null) await send('Target.setAutoAttach', { autoAttach: true, waitForDebuggerOnStart: false, flatten: true });
if (rest.includes('--nogpu')) await send('Page.addScriptToEvaluateOnNewDocument', { source: "Object.defineProperty(Navigator.prototype, 'gpu', { get: () => undefined, configurable: true });" });
await send('Page.navigate', { url });
let started = false;
for (let attempt = 0; attempt < 150 && !started; ++attempt) {
  started = await evaluate(`(() => { const b = document.getElementById('start'); if (!b || b.disabled || b.dataset.retry) return false; b.click(); return true; })()`);
  if (!started) await new Promise((r) => setTimeout(r, 200));
}
if (!started) throw new Error('Game did not become ready within 30 seconds');
const start = Date.now();
let profiling = false;
while (Date.now() - start < Number(seconds) * 1000) {
  if (profileFrom !== null && !profiling && Date.now() - start >= Number(profileFrom) * 1000) {
    profiling = true;
    for (const w of workers) {
      await send('Profiler.enable', {}, w);
      await send('Profiler.setSamplingInterval', { interval: Number(opt('--interval', '1000')) }, w);
      await send('Profiler.start', {}, w);
    }
  }
  await new Promise((r) => setTimeout(r, every));
  const s = await evaluate(`document.getElementById('stats')?.textContent || '-'`);
  console.log(((Date.now() - start) / 1000).toFixed(1) + 's ' + s);
}
if (profiling) {
  for (const w of workers) {
    const r = await send('Profiler.stop', {}, w);
    const profile = r.result?.profile;
    if (!profile) continue;
    const self = new Map(), byId = new Map(profile.nodes.map((n) => [n.id, n]));
    const counts = new Map();
    for (const sample of profile.samples) counts.set(sample, (counts.get(sample) || 0) + 1);
    let total = 0;
    for (const [nodeId, n] of counts) {
      const name = byId.get(nodeId).callFrame.functionName || '(anonymous)';
      self.set(name, (self.get(name) || 0) + n);
      total += n;
    }
    const top = [...self].sort((a, b) => b[1] - a[1]).slice(0, 25);
    console.log(`--- worker ${w.slice(0, 8)}: ${total} samples ---`);
    for (const [name, n] of top) console.log(((100 * n) / total).toFixed(2).padStart(6) + '%  ' + name.slice(0, 140));
  }
}
if (shot) {
  const r = await send('Page.captureScreenshot', { format: 'png' });
  (await import('node:fs')).writeFileSync(shot, Buffer.from(r.result.data, 'base64'));
}
console.log('--- console (' + logs.length + ' lines, last 40) ---');
for (const l of logs.slice(-40)) console.log(l);
ws.close();
