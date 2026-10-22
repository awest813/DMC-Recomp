// Pages cannot configure response headers. A scoped worker supplies isolation and
// routes local file reads to the launcher tab, which retains the user's File objects.
self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', event => event.waitUntil(self.clients.claim()));
const scope = new URL(self.registration.scope);
const isolate = response => {
  if (response.type === 'opaque' || response.status === 0) return response;
  const headers = new Headers(response.headers);
  headers.set('Cross-Origin-Opener-Policy', 'same-origin');
  headers.set('Cross-Origin-Embedder-Policy', 'require-corp');
  headers.set('Cross-Origin-Resource-Policy', 'same-origin');
  return new Response(response.body, { status: response.status, statusText: response.statusText, headers });
};

async function localFile(request, path) {
  const match = /^play\/([a-f0-9-]{36})\/(ps2web\.(?:html|js|wasm|data|worker\.js)|disc\.iso)$/.exec(path);
  if (!match) return new Response('Local file not found.', { status: 404 });
  const clients = (await self.clients.matchAll({ type: 'window' })).filter(client => {
    const url = new URL(client.url);
    return url.origin === scope.origin && (url.pathname === scope.pathname || url.pathname === scope.pathname + 'index.html');
  });
  try {
    return await Promise.any(clients.map(client => new Promise((resolve, reject) => {
      const channel = new MessageChannel();
      const timer = setTimeout(() => { channel.port1.close(); reject(new Error('Launcher did not respond')); }, 8000);
      channel.port1.onmessage = event => {
        clearTimeout(timer); channel.port1.close();
        if (!event.data || event.data.missing) { reject(new Error('Session is no longer open')); return; }
        const { status, headers, body } = event.data;
        resolve(new Response(body, { status, headers }));
      };
      client.postMessage({ type: 'local-file', session: match[1], name: match[2], method: request.method, range: request.headers.get('Range') }, [channel.port2]);
    })));
  } catch {
    return new Response('This local session has ended. Return to the ISO picker and choose your files again.', { status: 503, headers: { 'Content-Type': 'text/plain; charset=utf-8' } });
  }
}

self.addEventListener('fetch', event => {
  const url = new URL(event.request.url);
  if (url.origin !== scope.origin || !url.pathname.startsWith(scope.pathname)) return;
  const path = url.pathname.slice(scope.pathname.length);
  event.respondWith((path.startsWith('play/') ? localFile(event.request, path) : fetch(event.request)).then(isolate));
});
