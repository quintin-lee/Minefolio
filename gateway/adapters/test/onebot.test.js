/**
 * OneBot adapter bridge test (node:test, no real IM runtime).
 *
 * Spins up a tiny mock backend (http) that answers /api/im/qq/chat, points
 * MINEFOLIO_API at it, then feeds OneBot events through createOneBotAdapter
 * with a mock transport and asserts the reply is delivered to the right uid.
 */

const { test, before, after } = require('node:test');
const assert = require('node:assert');
const http = require('node:http');

// Point the gateway core at the mock backend BEFORE it is required.
let server;
let apiBase;
before(async () => {
  server = http.createServer((req, res) => {
    let body = '';
    req.on('data', (c) => (body += c));
    req.on('end', () => {
      const u = new URL(req.url, 'http://x');
      if (req.method === 'POST' && u.pathname === '/api/im/qq/chat') {
        let parsed = {};
        try {
          parsed = JSON.parse(body || '{}');
        } catch {}
        // mock reply echoes whether a bound token was presented
        const bound = (req.headers.authorization || '').startsWith('Bearer ');
        res.setHeader('Content-Type', 'application/json');
        if (bound && parsed.im_user_id === '77777') {
          res.end(
            JSON.stringify({
              code: 0,
              data: { task_id: 88, task_type: 'im_chat', status: 'pending', bound: 1 },
            }),
          );
          return;
        }
        res.end(JSON.stringify({ code: 0, data: { reply: bound ? `MOCK_REPLY:${parsed.im_user_id}` : 'UNBOUND', bound: bound ? 1 : 0 } }));
      } else if (req.method === 'GET' && u.pathname === '/api/im/tasks/88') {
        res.setHeader('Content-Type', 'application/json');
        res.end(
          JSON.stringify({
            code: 0,
            data: {
              id: 88,
              task_type: 'im_chat',
              status: 'completed',
              progress: 100,
              result: { reply: 'ASYNC_MOCK_REPLY:77777', bound: 1 },
            },
          }),
        );
      } else {
        res.statusCode = 404;
        res.end('{}');
      }
    });
  });
  await new Promise((r) => server.listen(0, r));
  apiBase = `http://localhost:${server.address().port}`;
  process.env.MINEFOLIO_API = apiBase;
});
after(() => server && server.close());

const { createOneBotAdapter } = require('../onebot.js');
const { createMockTransport, makePrivateEvent, makeGroupEvent } = require('../mock-events.js');

test('bridges a private message to the backend and replies to the same uid', async () => {
  const transport = createMockTransport();
  const adapter = createOneBotAdapter({ getBindToken: async () => 'SOME_BIND_TOKEN', transport });
  await adapter.onEvent(makePrivateEvent({ userId: '12345', text: '我最近一个月花了多少' }));
  assert.strictEqual(transport.sent.length, 1);
  assert.strictEqual(transport.sent[0].uid, '12345');
  assert.match(transport.sent[0].text, /^MOCK_REPLY:12345$/);
});

test('polls /api/im/tasks/:id when backend chat returns an async task_id', async () => {
  const transport = createMockTransport();
  const adapter = createOneBotAdapter({ getBindToken: async () => 'SOME_BIND_TOKEN', transport });
  await adapter.onEvent(makePrivateEvent({ userId: '77777', text: '查询资产汇总' }));
  assert.strictEqual(transport.sent.length, 1);
  assert.strictEqual(transport.sent[0].uid, '77777');
  assert.match(transport.sent[0].text, /^ASYNC_MOCK_REPLY:77777$/);
});

test('unbound user (no bind token) gets the guidance reply', async () => {
  const transport = createMockTransport();
  const adapter = createOneBotAdapter({ getBindToken: async () => '', transport });
  await adapter.onEvent(makePrivateEvent({ userId: '99999', text: '查账' }));
  assert.strictEqual(transport.sent.length, 1);
  assert.strictEqual(transport.sent[0].uid, '99999');
  assert.match(transport.sent[0].text, /UNBOUND|尚未绑定/);
});

test('group messages are ignored (P1 = private only)', async () => {
  const transport = createMockTransport();
  const adapter = createOneBotAdapter({ getBindToken: async () => 'T', transport });
  await adapter.onEvent(makeGroupEvent({ groupId: 1, userId: '123', text: 'hi group' }));
  assert.strictEqual(transport.sent.length, 0);
});

test('non-message post_type is ignored', async () => {
  const transport = createMockTransport();
  const adapter = createOneBotAdapter({ getBindToken: async () => 'T', transport });
  await adapter.onEvent({ post_type: 'meta_event' });
  assert.strictEqual(transport.sent.length, 0);
});

test('redacts /bind credentials before logging', async () => {
  const transport = createMockTransport();
  const logged = [];
  const log = {
    info: (...args) => logged.push(args.map(String).join(' ')),
    warn: (...args) => logged.push(args.map(String).join(' ')),
  };
  const adapter = createOneBotAdapter({ getBindToken: async () => '', transport, log });
  await adapter.onEvent(makePrivateEvent({ userId: '54321', text: '  /bind alice SuperSecretPass!99  ' }));
  assert.strictEqual(transport.sent.length, 1);
  assert.strictEqual(logged.length, 1);
  assert.ok(!logged[0].includes('SuperSecretPass!99'), 'password must not appear in OneBot logs');
  assert.ok(!logged[0].includes('alice'), '/bind arguments must be redacted from logs');
  assert.ok(logged[0].includes('/bind [REDACTED]'));
});
