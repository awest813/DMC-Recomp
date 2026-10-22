// Keep disc reads bounded: the runtime requests small byte ranges, never the entire ISO.
export const runtimeNames = ['ps2web.html', 'ps2web.js', 'ps2web.wasm', 'ps2web.data'];
export const allowedNames = new Set([...runtimeNames, 'ps2web.worker.js', 'disc.iso']);

export function fileReply(file, name, method, range) {
  const headers = { 'Content-Type': ({ html: 'text/html; charset=utf-8', js: 'text/javascript', wasm: 'application/wasm' })[name.split('.').pop()] || 'application/octet-stream', 'Accept-Ranges': 'bytes', 'Cache-Control': 'no-store' };
  if (!['GET', 'HEAD'].includes(method)) return { status: 405, headers: { ...headers, Allow: 'GET, HEAD' }, body: null };
  let start = 0, end = file.size - 1, status = 200;
  if (range) {
    const match = /^bytes=(\d*)-(\d*)$/.exec(range);
    const invalid = () => ({ status: 416, headers: { ...headers, 'Content-Range': `bytes */${file.size}` }, body: null });
    if (!match || (!match[1] && !match[2])) return invalid();
    if (!match[1]) {
      const suffix = Number(match[2]);
      if (!Number.isSafeInteger(suffix) || suffix <= 0) return invalid();
      start = Math.max(0, file.size - suffix);
    } else {
      start = Number(match[1]);
      end = match[2] ? Math.min(Number(match[2]), end) : end;
    }
    if (!Number.isSafeInteger(start) || !Number.isSafeInteger(end) || start > end || start >= file.size) return invalid();
    status = 206;
    headers['Content-Range'] = `bytes ${start}-${end}/${file.size}`;
  }
  headers['Content-Length'] = String(Math.max(0, end - start + 1));
  return { status, headers, body: method === 'HEAD' ? null : file.slice(start, end + 1) };
}

export async function inspectIso(file) {
  const read = async (offset, size) => new Uint8Array(await file.slice(offset, offset + size).arrayBuffer());
  const text = bytes => new TextDecoder().decode(bytes);
  const pvd = await read(16 * 2048, 2048);
  if (pvd.length !== 2048 || pvd[0] !== 1 || text(pvd.slice(1, 6)) !== 'CD001') throw new Error('Choose an uncompressed PS2 .iso disc image.');
  const view = new DataView(pvd.buffer);
  const offset = view.getUint32(158, true) * 2048, size = view.getUint32(166, true);
  if (!size || size > 1048576 || offset + size > file.size) throw new Error('The ISO root directory is damaged or unsupported.');
  const directory = await read(offset, size);
  for (let i = 0; i < directory.length;) {
    const length = directory[i];
    if (!length) { i = (Math.floor(i / 2048) + 1) * 2048; continue; }
    if (length < 34 || i + length > directory.length || 33 + directory[i + 32] > length) break;
    const name = text(directory.slice(i + 33, i + 33 + directory[i + 32]));
    if (name === 'SYSTEM.CNF;1' || name === 'SYSTEM.CNF') {
      const record = new DataView(directory.buffer, i, length);
      const at = record.getUint32(2, true) * 2048, count = record.getUint32(10, true);
      if (!count || count > 65536 || at + count > file.size) break;
      const config = text(await read(at, count));
      if (!/^BOOT2\s*=\s*cdrom0:\\+SLUS_202\.16;1\s*$/im.test(config)) throw new Error('This build supports Devil May Cry, NTSC-U (SLUS_202.16). Choose that disc.');
      return 'Devil May Cry · NTSC-U · SLUS_202.16';
    }
    i += length;
  }
  throw new Error('SYSTEM.CNF could not be read from this ISO.');
}
