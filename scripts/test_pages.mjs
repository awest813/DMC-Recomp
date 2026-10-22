import assert from 'node:assert/strict';
import { fileReply, inspectIso } from '../web/pages/files.mjs';

const file = new Blob([Uint8Array.from({ length: 100 }, (_, i) => i)]);
for (const [range, status, start, length] of [
  [null, 200, 0, 100], ['bytes=10-19', 206, 10, 10], ['bytes=90-999', 206, 90, 10],
  ['bytes=95-', 206, 95, 5], ['bytes=-3', 206, 97, 3], ['bytes=-999', 206, 0, 100],
]) {
  const reply = fileReply(file, 'disc.iso', 'GET', range);
  assert.equal(reply.status, status);
  assert.equal(reply.headers['Content-Length'], String(length));
  const body = new Uint8Array(await reply.body.arrayBuffer());
  assert.equal(body.length, length); assert.equal(body[0], start);
  const head = fileReply(file, 'disc.iso', 'HEAD', range);
  assert.equal(head.body, null); assert.deepEqual(head.headers, reply.headers);
}
for (const range of ['bytes=100-', 'bytes=20-10', 'bytes=-0', 'bytes=-', 'bytes=1-2,4-5', 'bad', 'bytes=999999999999999999999-']) {
  const reply = fileReply(file, 'disc.iso', 'GET', range);
  assert.equal(reply.status, 416, range); assert.equal(reply.headers['Content-Range'], 'bytes */100');
}
assert.equal(fileReply(file, 'disc.iso', 'POST').status, 405);
assert.equal(fileReply(new Blob(), 'disc.iso', 'GET', 'bytes=0-').status, 416);
assert.equal(fileReply(file, 'ps2web.wasm', 'HEAD').headers['Content-Type'], 'application/wasm');

function iso(serial = 'SLUS_202.16') {
  const bytes = new Uint8Array(22 * 2048), view = new DataView(bytes.buffer), encode = new TextEncoder();
  const pvd = 16 * 2048, root = 20 * 2048, cnf = 21 * 2048;
  bytes[pvd] = 1; bytes.set(encode.encode('CD001'), pvd + 1);
  view.setUint32(pvd + 158, 20, true); view.setUint32(pvd + 166, 2048, true);
  const name = encode.encode('SYSTEM.CNF;1'), config = encode.encode(`BOOT2 = cdrom0:\\${serial};1\r\nVER = 1.00\r\n`);
  bytes[root] = 46; view.setUint32(root + 2, 21, true); view.setUint32(root + 10, config.length, true);
  bytes[root + 32] = name.length; bytes.set(name, root + 33); bytes.set(config, cnf);
  return bytes;
}
const bytes = iso();
let readBytes = 0;
const bounded = { size: bytes.length, slice(start, end) { readBytes += end - start; return new Blob([bytes.slice(start, end)]); } };
assert.match(await inspectIso(bounded), /SLUS_202.16/);
assert.ok(readBytes < 5000, 'ISO selection must read only metadata');
await assert.rejects(inspectIso(new Blob([iso('SLUS_000.00')])), /supports Devil May Cry/);
await assert.rejects(inspectIso(new Blob(['not an ISO'])), /uncompressed/);
const damaged = iso(); new DataView(damaged.buffer).setUint32(16 * 2048 + 166, 0xffffffff, true);
await assert.rejects(inspectIso(new Blob([damaged])), /root directory/);
console.log('Pages: bounded ISO validation, wrong-disc rejection, byte ranges, HEAD, MIME and invalid requests passed');
