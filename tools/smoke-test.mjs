// Run against the example: node tools/smoke-test.mjs 192.168.1.50 [502]
// Writes registers 1 to 3. Use only a test device where these writes are safe.
import net from 'node:net';
import assert from 'node:assert/strict';

const host = process.argv[2];
if (!host || host === '--help') {
  console.log('Usage: node tools/smoke-test.mjs ESP32_IP [PORT]\nWrites holding registers 1 to 3 on the HoldingRegisters example.');
  process.exit(host ? 0 : 1);
}
const port = Number(process.argv[3] ?? 502);
assert(Number.isInteger(port) && port > 0 && port <= 65535, 'Invalid port');
const socket = net.createConnection({ host, port });
let received = Buffer.alloc(0);
let waiter;
let failure;
socket.on('data', chunk => { received = Buffer.concat([received, chunk]); waiter?.(); });
socket.on('error', error => { failure = error; waiter?.(); });
socket.on('close', () => { failure ??= new Error('Connection closed'); waiter?.(); });
socket.setTimeout(5000, () => socket.destroy(new Error('Socket timeout')));

async function response() {
  const deadline = Date.now() + 5000;
  for (;;) {
    if (failure) throw failure;
    if (received.length >= 6) {
      const length = received.readUInt16BE(4);
      assert(length >= 2 && length <= 254, 'Invalid MBAP length');
      if (received.length >= 6 + length) {
        const frame = received.subarray(0, 6 + length);
        received = received.subarray(6 + length);
        assert.equal(frame.readUInt16BE(2), 0);
        assert.equal(frame[6], 1);
        return frame;
      }
    }
    const remaining = deadline - Date.now();
    if (remaining <= 0) throw new Error('Response deadline exceeded');
    await new Promise(resolve => {
      const timer = setTimeout(done, remaining);
      function done() { clearTimeout(timer); waiter = undefined; resolve(); }
      waiter = done;
    });
  }
}
function request(id, fn, address, value) {
  const frame = Buffer.from([0,0,0,0,0,6,1,fn,0,0,0,0]);
  frame.writeUInt16BE(id, 0);
  frame.writeUInt16BE(address, 8);
  frame.writeUInt16BE(value, 10);
  return frame;
}
try {
  const write = request(1, 6, 1, 0x5aa5);
  // Split both the header and payload across writes.
  socket.write(write.subarray(0, 3));
  await new Promise(resolve => setTimeout(resolve, 30));
  socket.write(write.subarray(3, 9));
  await new Promise(resolve => setTimeout(resolve, 30));
  socket.write(write.subarray(9));
  assert.deepEqual(await response(), write);
  // Two ADUs in one write: responses must retain transaction order.
  socket.write(Buffer.concat([request(2, 3, 1, 1), request(3, 3, 16, 1)]));
  const read = await response();
  assert.deepEqual(read, Buffer.from([0,2,0,0,0,5,1,3,2,0x5a,0xa5]));
  assert.deepEqual(await response(), Buffer.from([0,3,0,0,0,3,1,0x83,2]));
  // Function 16: write registers 2 and 3, then read them back.
  socket.write(Buffer.from([0,4,0,0,0,11,1,16,0,2,0,2,4,0x12,0x34,0xbe,0xef]));
  assert.deepEqual(await response(), Buffer.from([0,4,0,0,0,6,1,16,0,2,0,2]));
  socket.write(request(5, 3, 2, 2));
  assert.deepEqual(await response(), Buffer.from([0,5,0,0,0,7,1,3,4,0x12,0x34,0xbe,0xef]));
  // Register 0 is read-only in the example.
  socket.write(request(6, 6, 0, 1));
  assert.deepEqual(await response(), Buffer.from([0,6,0,0,0,3,1,0x86,2]));
  console.log('PASS: fragmented write, pipelined read, transaction IDs, function 16, read-only and address exceptions');
} catch (error) {
  console.error('FAIL:', error.message);
  process.exitCode = 1;
} finally {
  socket.destroy();
}
